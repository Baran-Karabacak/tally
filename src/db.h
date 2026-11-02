#ifndef TALLY_DB_H
#define TALLY_DB_H

#include <stddef.h>
#include <stdint.h>

typedef struct Db Db;

typedef enum {
    DB_OK = 0,
    DB_ERR_NOMEM,
    DB_ERR_OPEN,
    DB_ERR_SCHEMA,
    DB_ERR_VERSION,
    DB_ERR_QUERY,
    DB_ERR_CONSTRAINT,   // bad data: invalid date (SQLSTATE 22xxx) or violated constraint (23xxx)
} Db_Err;

typedef struct {
    int64_t id;
    char date[11];      // "YYYY-MM-DD"
    int64_t amount;     // cents; expenses are neg, income is pos
    char category[64];
    char note[256];     // "" when there is no note
} Transaction;


typedef struct {
    Transaction * items;
    size_t count;
    size_t capacity;
} Transactions;

// Connects to PostgreSQL and, if needed, creates/migrates the schema.
// `conninfo` is a libpq connection string or URI, e.g.
//   "postgresql://tally:tally_dev@127.0.0.1:5432/tally"
Db_Err db_open(const char *path, Db **out);

// The TALLY_DB_URL environment variable if set, otherwise the local
// development default (the one from compose.yaml).
const char *db_default_conninfo(void);

void db_close(Db *db);
const char *db_errmsg(const Db *db);

// Inserts a transaction. `note` may be NULL. `out_id` may be NULL.
Db_Err db_add_transaction(Db *db, const char *date, int64_t amount,
                          const char *category, const char *note,
                          int64_t *out_id);

// Optional filters for db_list_transactions. A NULL field means "no limit".
// `from` and `to` are inclusive "YYYY-MM-DD" dates.
typedef struct {
    const char *from;
    const char *to;
    const char *category;
} Tx_Filter;

// Appends the matching transactions, ordered by date then id, to `out`.
// `filter` may be NULL (= everything). A malformed date in the filter
// returns DB_ERR_CONSTRAINT. The caller owns `out` and must free it
// even if an error is returned.
Db_Err db_list_transactions(Db *db, const Tx_Filter *filter, Transactions *out);

#endif // TALLY_DB_H