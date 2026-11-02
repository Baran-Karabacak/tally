#include "money.h"

#include <inttypes.h>
#include <stdio.h>

// Predicate for nob_sv_chop_while(). We write our own instead of using isdigit()
// because isdigit() on a negative 'char' (e.g. a UTF-8 byte) is undefined behavior.
static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

// `digits` must contain only '0'..'9' (the caller guarantees it).
static Money_Err parse_digits(Nob_String_View digits, int64_t *out) {
    int64_t value = 0;
    for (size_t i = 0; i < digits.count; ++i) {
        int d = digits.data[i] - '0';
        // Would value*10 + d exceed INT64_MAX? Check BEFORE doing the math,
        // because signed overflow is undefined behavior in C.
        if (value > (INT64_MAX - d) / 10) return MONEY_ERR_OVERFLOW;
        value = value * 10 + d;
    }
    *out = value;
    return MONEY_OK;
}

Money_Err money_parse(Nob_String_View sv, int64_t *out_cents) {
    sv = nob_sv_trim(sv);
    if (sv.count == 0) return MONEY_ERR_EMPTY;

    // chop_while returns the prefix that matches and advances `sv` past it.
    Nob_String_View whole = nob_sv_chop_while(&sv, is_digit);
    if (whole.count == 0) return MONEY_ERR_FORMAT;

    Nob_String_View frac = {0};
    if (sv.count > 0) {
        char sep = sv.data[0];
        if (sep != '.' && sep != ',') return MONEY_ERR_FORMAT;
        nob_sv_chop_left(&sv, 1);

        frac = nob_sv_chop_while(&sv, is_digit);
        if (frac.count == 0) return MONEY_ERR_FORMAT;   // "45."
        if (sv.count != 0)   return MONEY_ERR_FORMAT;   // trailing garbage
        if (frac.count > 2)  return MONEY_ERR_TOO_MANY_DECIMALS;
    }

    int64_t whole_units = 0;
    Money_Err err = parse_digits(whole, &whole_units);
    if (err != MONEY_OK) return err;

    // "5" means 50 cents, "05" means 5 cents.
    int64_t frac_cents = 0;
    if (frac.count >= 1) frac_cents = (frac.data[0] - '0') * 10;
    if (frac.count == 2) frac_cents += (frac.data[1] - '0');

    // whole_units * 100 + frac_cents must fit in int64_t.
    if (whole_units > (INT64_MAX - frac_cents) / 100) return MONEY_ERR_OVERFLOW;

    *out_cents = whole_units * 100 + frac_cents;
    return MONEY_OK;
}

Money_Str money_format(int64_t cents) {
    Money_Str out = {{0}};
    // Negating INT64_MIN in signed arithmetic overflows, so go through unsigned.
    uint64_t mag = (cents < 0) ? (uint64_t)0 - (uint64_t)cents : (uint64_t)cents;
    snprintf(out.text, sizeof out.text, "%s%" PRIu64 ".%02" PRIu64,
            cents < 0 ? "-": "", mag / 100, mag % 100);
    return out;
}

const char *money_err_str(Money_Err err) {
    switch(err) {
        case MONEY_OK:                    return "ok";
        case MONEY_ERR_EMPTY:             return "amount is empty";
        case MONEY_ERR_FORMAT:            return "invalid amount (expected e.g. 45, 45.5, 45.50 or 45,50)";
        case MONEY_ERR_TOO_MANY_DECIMALS: return "at most 2 decimal places allowed";
        case MONEY_ERR_OVERFLOW:          return "amount is too large";
    }
    return "unknown error";
}