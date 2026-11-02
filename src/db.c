#include "db.h"

#include <inttypes.h>
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nob.h"

#define SCHEMA_VERSION 1
#define DEFAULT_CONNINFO "postgresql://tally:tally_dev@127.0.0.1:5432/tally"

struct Db {
    PGconn *conn;
    char errmsg[512];
};

#define SCHEMA_V1_SQL                                                           \
    "CREATE TABLE transactions ("                                               \
    "  id         BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,"             \
    "  date       DATE        NOT NULL,"                                        \
    "  amount     BIGINT      NOT NULL,"                                        \
    "  category   TEXT        NOT NULL DEFAULT 'general',"                      \
    "  note       TEXT,"                                                        \
    "  created_at TIMESTAMPTZ NOT NULL DEFAULT now()"                           \
    ");"                                                                        \
    "CREATE INDEX idx_tx_date ON transactions (date);"                          \
    "CREATE TABLE limits ("                                                     \
    "  id       BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,"               \
    "  category TEXT,"                                                          \
    "  period   TEXT   NOT NULL CHECK (period IN ('weekly', 'monthly')),"       \
    "  amount   BIGINT NOT NULL CHECK (amount > 0)"                             \
    ");"

const char *db_default_conninfo(void)
{
    const char *env = getenv("TALLY_DB_URL");
    return (env != NULL && env[0] != '\0') ? env : DEFAULT_CONNINFO;
}

// Copies libpq's last error message for this connection into our own buffer
// (libpq's copy can be overwritten by the next call) and returns `code`.
static Db_Err fail(Db *db, Db_Err code)
{
    snprintf(db->errmsg, sizeof db->errmsg, "%s", PQerrorMessage(db->conn));
    // libpq messages end with a newline; trim it so our output stays tidy.
    size_t n = strlen(db->errmsg);
    while (n > 0 && (db->errmsg[n - 1] == '\n' || db->errmsg[n - 1] == '\r')) {
        db->errmsg[--n] = '\0';
    }
    return code;
}

// SQLSTATE class 22 = data exception (e.g. invalid date),
// class 23 = integrity constraint violation. Everything else is a plain query error.
static Db_Err classify(const PGresult *res)
{
    const char *state = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    if (state != NULL && (strncmp(state, "22", 2) == 0 || strncmp(state, "23", 2) == 0)) {
        return DB_ERR_CONSTRAINT;
    }
    return DB_ERR_QUERY;
}

// Runs SQL that has no parameters. Several ';'-separated statements are fine.
static Db_Err exec_simple(Db *db, const char *sql, Db_Err on_error)
{
    Db_Err result = DB_OK;
    PGresult *res = PQexec(db->conn, sql);
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        nob_return_defer(fail(db, on_error));
    }
defer:
    PQclear(res);   // PQclear(NULL) is a harmless no-op
    return result;
}

static Db_Err get_schema_version(Db *db, int *out)
{
    Db_Err result = DB_OK;
    PGresult *res = PQexec(db->conn, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        nob_return_defer(fail(db, DB_ERR_SCHEMA));
    }
    *out = atoi(PQgetvalue(res, 0, 0));
defer:
    PQclear(res);
    return result;
}

static Db_Err apply_schema_v1(Db *db)
{
    // PostgreSQL has transactional DDL: either the whole schema and the
    // migration record land, or nothing does.
    const char *sql =
        "BEGIN;"
        SCHEMA_V1_SQL
        "INSERT INTO schema_migrations (version) VALUES (1);"
        "COMMIT;";

    Db_Err err = exec_simple(db, sql, DB_ERR_SCHEMA);
    if (err != DB_OK) {
        // The failed BEGIN block leaves the session in an aborted transaction.
        // Our error message is already copied, so rolling back is safe.
        PQclear(PQexec(db->conn, "ROLLBACK"));
    }
    return err;
}

Db_Err db_open(const char *conninfo, Db **out)
{
    *out = NULL;
    Db *db = calloc(1, sizeof *db);
    if (db == NULL) return DB_ERR_NOMEM;

    db->conn = PQconnectdb(conninfo);
    if (db->conn == NULL) {      // only happens when libpq itself is out of memory
        free(db);
        return DB_ERR_NOMEM;
    }
    *out = db;

    if (PQstatus(db->conn) != CONNECTION_OK) return fail(db, DB_ERR_OPEN);
    if (PQsetClientEncoding(db->conn, "UTF8") != 0) return fail(db, DB_ERR_OPEN);

    Db_Err err = exec_simple(db, "SET client_min_messages TO warning", DB_ERR_OPEN);
    if (err != DB_OK) return err;

    err = exec_simple(db,
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version    INTEGER PRIMARY KEY,"
        "  applied_at TIMESTAMPTZ NOT NULL DEFAULT now())",
        DB_ERR_SCHEMA);
    if (err != DB_OK) return err;

    int version = 0;
    err = get_schema_version(db, &version);
    if (err != DB_OK) return err;

    if (version == 0) return apply_schema_v1(db);   // brand new database
    if (version == SCHEMA_VERSION) return DB_OK;

    snprintf(db->errmsg, sizeof db->errmsg,
             "database schema version %d is newer than this program supports (%d)",
             version, SCHEMA_VERSION);
    return DB_ERR_VERSION;
}

void db_close(Db *db)
{
    if (db == NULL) return;
    PQfinish(db->conn);   // accepts NULL
    free(db);
}

const char *db_errmsg(const Db *db)
{
    return (db != NULL) ? db->errmsg : "out of memory";
}

Db_Err db_add_transaction(Db *db, const char *date, int64_t amount,
                          const char *category, const char *note,
                          int64_t *out_id)
{
    static const char *sql =
        "INSERT INTO transactions (date, amount, category, note) "
        "VALUES ($1, $2, $3, $4) RETURNING id";

    Db_Err result = DB_OK;
    PGresult *res = NULL;

    // Parameters travel as text. A NULL pointer becomes SQL NULL.
    char amount_text[32];
    snprintf(amount_text, sizeof amount_text, "%" PRId64, amount);
    const char *params[4] = { date, amount_text, category, note };

    // paramTypes = NULL: the server infers the types from the columns.
    // Last argument 0 = we want the result in text format.
    res = PQexecParams(db->conn, sql, 4, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        nob_return_defer(fail(db, classify(res)));
    }

    if (out_id != NULL) *out_id = strtoll(PQgetvalue(res, 0, 0), NULL, 10);

defer:
    PQclear(res);
    return result;
}

Db_Err db_list_transactions(Db *db, const Tx_Filter *filter, Transactions *out)
{
    // One fixed SQL text. When a parameter is NULL its condition is
    // switched off, so no SQL is ever assembled from user input.
    // The ::date / ::text casts tell the server which type a NULL parameter has.
    static const char *sql =
        "SELECT id, to_char(date, 'YYYY-MM-DD'), amount, category, note "
        "FROM transactions "
        "WHERE ($1::date IS NULL OR date >= $1::date) "
        "  AND ($2::date IS NULL OR date <= $2::date) "
        "  AND ($3::text IS NULL OR category = $3::text) "
        "ORDER BY date, id";

    const char *params[3] = {NULL, NULL, NULL};
    if (filter != NULL) {
        params[0] = filter->from;
        params[1] = filter->to;
        params[2] = filter->category;
    }

    Db_Err result = DB_OK;
    PGresult *res = PQexecParams(db->conn, sql, 3, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        nob_return_defer(fail(db, classify(res)));
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; ++i) {
        Transaction tx = {0};
        tx.id     = strtoll(PQgetvalue(res, i, 0), NULL, 10);
        snprintf(tx.date, sizeof tx.date, "%s", PQgetvalue(res, i, 1));
        tx.amount = strtoll(PQgetvalue(res, i, 2), NULL, 10);
        snprintf(tx.category, sizeof tx.category, "%s", PQgetvalue(res, i, 3));
        if (!PQgetisnull(res, i, 4)) {
            snprintf(tx.note, sizeof tx.note, "%s", PQgetvalue(res, i, 4));
        }   // else: tx.note stays "" thanks to the {0} initializer

        nob_da_append(out, tx);
    }

defer:
    PQclear(res);
    return result;
}