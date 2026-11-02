#ifndef TALLY_MONEY_H
#define TALLY_MONEY_H

#include <stdint.h>
#include "nob.h"

typedef enum {
    MONEY_OK = 0,
    MONEY_ERR_EMPTY,
    MONEY_ERR_FORMAT,
    MONEY_ERR_TOO_MANY_DECIMALS,
    MONEY_ERR_OVERFLOW,
} Money_Err;

// A small fixed buffer returned BY VALUE, so the caller needs no malloc/free.
typedef struct {
    char text[32];
} Money_Str;

// Parses "45", "45.5", "45.50" or "45,50" into cents (4500, 4550, 4550, 4550).
// Leading/trailing whitespace is ignored. Only non-negative amounts are accepted.
Money_Err money_parse(Nob_String_View sv, int64_t *out_cents);

// 4550 -> "45.50", -5 -> "-0.05"
Money_Str money_format(int64_t cents);

const char *money_err_str(Money_Err err);

#endif // TALLY_MONEY_H