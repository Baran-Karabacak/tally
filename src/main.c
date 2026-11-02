#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "date.h"
#include "db.h"
#include "money.h"
#include "nob.h"

#define RC_OK    0
#define RC_USAGE 1
#define RC_DB    2

#define MAX_CATEGORY_LEN 63                    // Transaction.category is char[64]
#define MAX_NOTE_LEN     255                   // Transaction.note is char[256]
#define MAX_CENTS        INT64_C(100000000000) // 1 billion TL; keeps sums far from overflow

static void print_usage(FILE *out)
{
    fputs(
        "usage:\n"
        "  tally add <amount> [category] [--note TEXT] [--date DATE]\n"
        "  tally income <amount> [category] [--note TEXT] [--date DATE]\n"
        "  tally list [--week | --month | --all] [--from DATE] [--to DATE] [--category NAME]\n"
        "  tally help\n"
        "\n"
        "DATE is YYYY-MM-DD, \"today\" or \"yesterday\".\n"
        "list shows the current month unless told otherwise; options apply in order.\n",
        out);
}

// Reads the value after an option like "--note TEXT". Advances *i past it.
static bool option_value(int *i, int argc, char **argv, const char **out)
{
    if (*i + 1 >= argc) {
        fprintf(stderr, "tally: option %s needs a value\n", argv[*i]);
        return false;
    }
    *i += 1;
    *out = argv[*i];
    return true;
}

// Trims `raw`, rejects empty / too long values. The result lives in nob's
// temp storage, so there is nothing to free. Returns NULL after printing an error.
static const char *clean_field(const char *what, const char *raw, size_t max_len)
{
    Nob_String_View sv = nob_sv_trim(nob_sv_from_cstr(raw));
    if (sv.count == 0) {
        fprintf(stderr, "tally: %s must not be empty\n", what);
        return NULL;
    }
    if (sv.count > max_len) {
        fprintf(stderr, "tally: %s is too long (max %zu bytes)\n", what, max_len);
        return NULL;
    }
    return nob_temp_sv_to_cstr(sv);
}

static bool parse_date_arg(const char *what, const char *raw, Date today, Date *out)
{
    Date_Err err = date_parse_user(nob_sv_from_cstr(raw), today, out);
    if (err != DATE_OK) {
        fprintf(stderr, "tally: %s: %s\n", what, date_err_str(err));
        return false;
    }
    return true;
}

static bool open_db(Db **db)
{
    Db_Err err = db_open(db_default_conninfo(), db);
    if (err == DB_OK) return true;
    fprintf(stderr, "tally: %s\n", db_errmsg(*db));
    db_close(*db);
    *db = NULL;
    return false;
}

// "add" (expense) and "income" share everything except the sign.
static int cmd_record(bool income, int argc, char **argv)
{
    const char *amount_arg   = NULL;
    const char *category_arg = NULL;
    const char *note_arg     = NULL;
    const char *date_arg     = "today";

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--note") == 0) {
            if (!option_value(&i, argc, argv, &note_arg)) return RC_USAGE;
        } else if (strcmp(arg, "--date") == 0) {
            if (!option_value(&i, argc, argv, &date_arg)) return RC_USAGE;
        } else if (arg[0] == '-' && arg[1] == '-') {
            fprintf(stderr, "tally: unknown option %s\n", arg);
            return RC_USAGE;
        } else if (amount_arg == NULL) {
            amount_arg = arg;
        } else if (category_arg == NULL) {
            category_arg = arg;
        } else {
            fprintf(stderr, "tally: unexpected argument \"%s\" (quote multi-word values)\n", arg);
            return RC_USAGE;
        }
    }

    if (amount_arg == NULL) {
        print_usage(stderr);
        return RC_USAGE;
    }

    int64_t cents = 0;
    Money_Err merr = money_parse(nob_sv_from_cstr(amount_arg), &cents);
    if (merr != MONEY_OK) {
        fprintf(stderr, "tally: %s\n", money_err_str(merr));
        return RC_USAGE;
    }
    if (cents == 0) {
        fprintf(stderr, "tally: amount must be greater than zero\n");
        return RC_USAGE;
    }
    if (cents > MAX_CENTS) {
        fprintf(stderr, "tally: amount is too large\n");
        return RC_USAGE;
    }

    Date today = date_today();
    Date date  = today;
    if (!parse_date_arg("--date", date_arg, today, &date)) return RC_USAGE;

    const char *category = "general";
    if (category_arg != NULL) {
        category = clean_field("category", category_arg, MAX_CATEGORY_LEN);
        if (category == NULL) return RC_USAGE;
    }

    const char *note = NULL;
    if (note_arg != NULL) {
        note = clean_field("note", note_arg, MAX_NOTE_LEN);
        if (note == NULL) return RC_USAGE;
    }

    int64_t amount = income ? cents : -cents;
    Date_Str date_text = date_format(date);

    Db *db = NULL;
    if (!open_db(&db)) return RC_DB;

    int64_t id = 0;
    Db_Err err = db_add_transaction(db, date_text.text, amount, category, note, &id);
    if (err != DB_OK) {
        fprintf(stderr, "tally: could not save: %s\n", db_errmsg(db));
        db_close(db);
        return RC_DB;
    }

    printf("added #%" PRId64 ": %s TL  %s  %s\n",
           id, money_format(amount).text, category, date_text.text);
    db_close(db);
    return RC_OK;
}

static int cmd_list(int argc, char **argv)
{
    Date today = date_today();
    Date from  = date_month_start(today);
    Date to    = date_month_end(today);
    bool has_from = true;
    bool has_to   = true;
    const char *category = NULL;

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = NULL;

        if (strcmp(arg, "--week") == 0) {
            from = date_week_start(today);
            to   = date_add_days(from, 6);
            has_from = has_to = true;
        } else if (strcmp(arg, "--month") == 0) {
            from = date_month_start(today);
            to   = date_month_end(today);
            has_from = has_to = true;
        } else if (strcmp(arg, "--all") == 0) {
            has_from = has_to = false;
        } else if (strcmp(arg, "--from") == 0) {
            if (!option_value(&i, argc, argv, &value)) return RC_USAGE;
            if (!parse_date_arg("--from", value, today, &from)) return RC_USAGE;
            has_from = true;
        } else if (strcmp(arg, "--to") == 0) {
            if (!option_value(&i, argc, argv, &value)) return RC_USAGE;
            if (!parse_date_arg("--to", value, today, &to)) return RC_USAGE;
            has_to = true;
        } else if (strcmp(arg, "--category") == 0) {
            if (!option_value(&i, argc, argv, &value)) return RC_USAGE;
            category = clean_field("category", value, MAX_CATEGORY_LEN);
            if (category == NULL) return RC_USAGE;
        } else {
            fprintf(stderr, "tally: unknown argument \"%s\"\n", arg);
            return RC_USAGE;
        }
    }

    if (has_from && has_to && date_compare(from, to) > 0) {
        fprintf(stderr, "tally: the start date is after the end date\n");
        return RC_USAGE;
    }

    // These two must outlive the query: the filter only holds pointers.
    Date_Str from_text = date_format(from);
    Date_Str to_text   = date_format(to);
    Tx_Filter filter = {
        .from     = has_from ? from_text.text : NULL,
        .to       = has_to   ? to_text.text   : NULL,
        .category = category,
    };

    Db *db = NULL;
    if (!open_db(&db)) return RC_DB;

    Transactions txs = {0};
    Db_Err err = db_list_transactions(db, &filter, &txs);
    if (err != DB_OK) {
        fprintf(stderr, "tally: could not read: %s\n", db_errmsg(db));
        nob_da_free(txs);
        db_close(db);
        return RC_DB;
    }

    if (has_from && has_to)  printf("Period: %s .. %s\n", from_text.text, to_text.text);
    else if (has_from)       printf("Period: from %s\n", from_text.text);
    else if (has_to)         printf("Period: until %s\n", to_text.text);
    else                     printf("Period: all time\n");
    if (category != NULL)    printf("Category: %s\n", category);

    if (txs.count == 0) {
        puts("No transactions.");
    } else {
        printf("%4s  %-10s  %12s  %-14s  %s\n", "ID", "DATE", "AMOUNT", "CATEGORY", "NOTE");

        int64_t income = 0, expense = 0;
        nob_da_foreach(Transaction, tx, &txs) {
            printf("%4" PRId64 "  %-10s  %12s  %-14s  %s\n",
                   tx->id, tx->date, money_format(tx->amount).text, tx->category, tx->note);
            if (tx->amount > 0) income += tx->amount;
            else                expense += tx->amount;
        }

        printf("\n%-10s %12s TL\n", "Income:",  money_format(income).text);
        printf("%-10s %12s TL\n",   "Expenses:", money_format(expense).text);
        printf("%-10s %12s TL\n",   "Net:",      money_format(income + expense).text);
    }

    nob_da_free(txs);
    db_close(db);
    return RC_OK;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return RC_USAGE;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "add") == 0)    return cmd_record(false, argc - 2, argv + 2);
    if (strcmp(cmd, "income") == 0) return cmd_record(true,  argc - 2, argv + 2);
    if (strcmp(cmd, "list") == 0)   return cmd_list(argc - 2, argv + 2);
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(stdout);
        return RC_OK;
    }

    fprintf(stderr, "tally: unknown command \"%s\"\n", cmd);
    print_usage(stderr);
    return RC_USAGE;
}