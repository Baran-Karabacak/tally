#include <inttypes.h>
#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "nob.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static const char *test_conninfo(void)
{
    const char *env = getenv("TALLY_TEST_DB_URL");
    return (env != NULL && env[0] != '\0')
        ? env
        : "postgresql://tally:tally_dev@127.0.0.1:5432/tally_test";
}

// Wipes the test database. Refuses to touch any database whose name
// does not contain "test".
static bool reset_test_database(void)
{
    bool result = true;
    PGresult *res = NULL;
    PGconn *conn = PQconnectdb(test_conninfo());

    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "cannot connect to the test database: %s", PQerrorMessage(conn));
        nob_return_defer(false);
    }

    const char *name = PQdb(conn);
    if (name == NULL || strstr(name, "test") == NULL) {
        fprintf(stderr, "refusing to reset database \"%s\": name must contain \"test\"\n",
                name ? name : "(null)");
        nob_return_defer(false);
    }

    res = PQexec(conn,
        "SET client_min_messages TO warning;"
        "DROP SCHEMA public CASCADE;"
        "CREATE SCHEMA public;");
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "reset failed: %s", PQerrorMessage(conn));
        nob_return_defer(false);
    }

defer:
    PQclear(res);
    PQfinish(conn);
    return result;
}

// Fresh, empty, fully migrated database for every test.
static Db *open_fresh(void)
{
    if (!reset_test_database()) {
        failures++;
        return NULL;
    }

    Db *db = NULL;
    Db_Err err = db_open(test_conninfo(), &db);
    if (err != DB_OK) {
        fprintf(stderr, "FAIL open: %s\n", db_errmsg(db));
        failures++;
        db_close(db);
        return NULL;
    }
    return db;
}

static void test_add_and_list(void)
{
    Db *db = open_fresh();
    if (!db) return;

    int64_t id1 = 0, id2 = 0;
    CHECK(db_add_transaction(db, "2026-10-25", -4550, "food", "lunch", &id1) == DB_OK);
    CHECK(db_add_transaction(db, "2026-10-20", 300000, "scholarship", NULL, &id2) == DB_OK);
    CHECK(id1 == 1);
    CHECK(id2 == 2);

    Transactions txs = {0};
    CHECK(db_list_transactions(db, NULL, &txs) == DB_OK);
    CHECK(txs.count == 2);
    if (txs.count == 2) {
        // Ordered by date: the Oct 20 entry comes first even though it was added second.
        CHECK(strcmp(txs.items[0].date, "2026-10-20") == 0);
        CHECK(txs.items[0].amount == 300000);
        CHECK(strcmp(txs.items[0].category, "scholarship") == 0);
        CHECK(strcmp(txs.items[0].note, "") == 0);          // NULL note -> ""

        CHECK(strcmp(txs.items[1].date, "2026-10-25") == 0);
        CHECK(txs.items[1].amount == -4550);
        CHECK(strcmp(txs.items[1].category, "food") == 0);
        CHECK(strcmp(txs.items[1].note, "lunch") == 0);
    }

    nob_da_free(txs);
    db_close(db);
}

static void test_special_characters(void)
{
    Db *db = open_fresh();
    if (!db) return;

    // Quotes, an injection attempt and non-ASCII text must round-trip as plain data.
    const char *nasty = "it's \"quoted\"; DROP TABLE transactions;--";
    const char *turkish = "öğle yemeği";
    CHECK(db_add_transaction(db, "2026-10-25", -100, "food", nasty, NULL) == DB_OK);
    CHECK(db_add_transaction(db, "2026-10-26", -200, "food", turkish, NULL) == DB_OK);

    Transactions txs = {0};
    CHECK(db_list_transactions(db, NULL, &txs) == DB_OK);   // the table must still exist
    CHECK(txs.count == 2);
    if (txs.count == 2) {
        CHECK(strcmp(txs.items[0].note, nasty) == 0);
        CHECK(strcmp(txs.items[1].note, turkish) == 0);
    }

    nob_da_free(txs);
    db_close(db);
}

static void test_bad_date_rejected(void)
{
    Db *db = open_fresh();
    if (!db) return;

    CHECK(db_add_transaction(db, "garbage",    -100, "food", NULL, NULL) == DB_ERR_CONSTRAINT);
    CHECK(db_add_transaction(db, "2026-02-31", -100, "food", NULL, NULL) == DB_ERR_CONSTRAINT);

    // A failed insert must leave the connection usable and the table empty.
    Transactions txs = {0};
    CHECK(db_list_transactions(db, NULL, &txs) == DB_OK);
    CHECK(txs.count == 0);

    nob_da_free(txs);
    db_close(db);
}

static void test_persistence(void)
{
    Db *db = open_fresh();
    if (!db) return;
    CHECK(db_add_transaction(db, "2026-10-25", -4550, "food", "lunch", NULL) == DB_OK);
    db_close(db);

    // Second open takes the "schema already at version 1" path.
    db = NULL;
    CHECK(db_open(test_conninfo(), &db) == DB_OK);
    Transactions txs = {0};
    CHECK(db_list_transactions(db, NULL, &txs) == DB_OK);
    CHECK(txs.count == 1);
    if (txs.count == 1) CHECK(txs.items[0].amount == -4550);

    nob_da_free(txs);
    db_close(db);
}

static void test_filter(void)
{
    Db *db = open_fresh();
    if (!db) return;

    CHECK(db_add_transaction(db, "2026-10-20", -1000, "food",      NULL, NULL) == DB_OK);
    CHECK(db_add_transaction(db, "2026-10-25", -2000, "food",      NULL, NULL) == DB_OK);
    CHECK(db_add_transaction(db, "2026-10-25", -3000, "transport", NULL, NULL) == DB_OK);
    CHECK(db_add_transaction(db, "2026-11-01", -4000, "food",      NULL, NULL) == DB_OK);

    Transactions txs = {0};

    Tx_Filter range = { .from = "2026-10-25", .to = "2026-10-31" };
    CHECK(db_list_transactions(db, &range, &txs) == DB_OK);
    CHECK(txs.count == 2);                                  // both Oct 25 entries
    txs.count = 0;

    Tx_Filter food = { .category = "food" };
    CHECK(db_list_transactions(db, &food, &txs) == DB_OK);
    CHECK(txs.count == 3);
    txs.count = 0;

    Tx_Filter from_only = { .from = "2026-10-26" };
    CHECK(db_list_transactions(db, &from_only, &txs) == DB_OK);
    CHECK(txs.count == 1);                                  // Nov 1
    txs.count = 0;

    Tx_Filter to_only = { .to = "2026-10-20" };
    CHECK(db_list_transactions(db, &to_only, &txs) == DB_OK);
    CHECK(txs.count == 1);                                  // the end date is inclusive
    txs.count = 0;

    Tx_Filter combined = { .from = "2026-10-21", .to = "2026-11-01", .category = "food" };
    CHECK(db_list_transactions(db, &combined, &txs) == DB_OK);
    CHECK(txs.count == 2);
    txs.count = 0;

    Tx_Filter future = { .from = "2027-01-01" };
    CHECK(db_list_transactions(db, &future, &txs) == DB_OK);
    CHECK(txs.count == 0);

    Tx_Filter bad = { .from = "garbage" };
    CHECK(db_list_transactions(db, &bad, &txs) == DB_ERR_CONSTRAINT);

    nob_da_free(txs);
    db_close(db);
}

int main(void)
{
    test_add_and_list();
    test_special_characters();
    test_bad_date_rejected();
    test_persistence();
    test_filter();

    if (failures > 0) {
        fprintf(stderr, "db: %d failure(s)\n", failures);
        return 1;
    }
    puts("db: all tests passed");
    return 0;
}