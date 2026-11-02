#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "money.h"

static int failures = 0;

static void expect_ok(const char *input, int64_t expected)
{
    int64_t got = -1;
    Money_Err err = money_parse(nob_sv_from_cstr(input), &got);
    if (err != MONEY_OK || got != expected) {
        fprintf(stderr, "FAIL parse \"%s\": err=%s got=%" PRId64 " expected=%" PRId64 "\n",
                input, money_err_str(err), got, expected);
        failures++;
    }
}

static void expect_err(const char *input, Money_Err expected)
{
    int64_t got = -1;
    Money_Err err = money_parse(nob_sv_from_cstr(input), &got);
    if (err != expected) {
        fprintf(stderr, "FAIL parse \"%s\": err=%d expected err=%d\n", input, err, expected);
        failures++;
    }
}

static void expect_fmt(int64_t cents, const char *expected)
{
    Money_Str s = money_format(cents);
    if (strcmp(s.text, expected) != 0) {
        fprintf(stderr, "FAIL format %" PRId64 ": got \"%s\" expected \"%s\"\n",
                cents, s.text, expected);
        failures++;
    }
}

int main(void)
{
    // valid input
    expect_ok("0",        0);
    expect_ok("0.00",     0);
    expect_ok("45",       4500);
    expect_ok("45.5",     4550);
    expect_ok("45.50",    4550);
    expect_ok("45,50",    4550);
    expect_ok("  12.05 ", 1205);
    expect_ok("0.99",     99);
    expect_ok("0.05",     5);
    expect_ok("007",      700);
    expect_ok("1000000",  100000000);

    // invalid input
    expect_err("",          MONEY_ERR_EMPTY);
    expect_err("   ",       MONEY_ERR_EMPTY);
    expect_err("abc",       MONEY_ERR_FORMAT);
    expect_err("-5",        MONEY_ERR_FORMAT);
    expect_err("4 5",       MONEY_ERR_FORMAT);
    expect_err("45.",       MONEY_ERR_FORMAT);
    expect_err(".5",        MONEY_ERR_FORMAT);
    expect_err("1.2.3",     MONEY_ERR_FORMAT);
    expect_err("1,000.00",  MONEY_ERR_FORMAT);
    expect_err("45.5x",     MONEY_ERR_FORMAT);
    expect_err("45.123",    MONEY_ERR_TOO_MANY_DECIMALS);

    // overflow boundaries (INT64_MAX = 9223372036854775807 cents)
    expect_ok ("92233720368547758.07",   INT64_MAX);
    expect_err("92233720368547758.08",   MONEY_ERR_OVERFLOW);
    expect_err("92233720368547759",      MONEY_ERR_OVERFLOW);
    expect_err("99999999999999999999",   MONEY_ERR_OVERFLOW);

    // formatting
    expect_fmt(0,          "0.00");
    expect_fmt(5,          "0.05");
    expect_fmt(99,         "0.99");
    expect_fmt(100,        "1.00");
    expect_fmt(4550,       "45.50");
    expect_fmt(-4550,      "-45.50");
    expect_fmt(-5,         "-0.05");
    expect_fmt(INT64_MAX,  "92233720368547758.07");
    expect_fmt(INT64_MIN,  "-92233720368547758.08");

    if (failures > 0) {
        fprintf(stderr, "money: %d failure(s)\n", failures);
        return 1;
    }
    puts("money: all tests passed");
    return 0;
}