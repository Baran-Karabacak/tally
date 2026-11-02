#include <stdio.h>
#include <string.h>

#include "date.h"
#include "nob.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static bool is(Date d, int y, int m, int day)
{
    return d.year == y && d.month == m && d.day == day;
}

static Date_Err parse(const char *s, Date *out)
{
    return date_parse(nob_sv_from_cstr(s), out);
}

static void test_parse(void)
{
    Date d = {0};

    CHECK(parse("2026-10-30", &d) == DATE_OK && is(d, 2026, 10, 30));
    CHECK(parse("2000-02-29",  &d) == DATE_OK && is(d, 2000, 2, 29));
    CHECK(parse("0001-01-01",  &d) == DATE_OK && is(d, 1, 1, 1));
    CHECK(parse("9999-12-31",  &d) == DATE_OK && is(d, 9999, 12, 31));
    CHECK(parse(" 2026-10-30 ", &d) == DATE_OK && is(d, 2026, 10, 30));

    CHECK(parse("",            &d) == DATE_ERR_EMPTY);
    CHECK(parse("   ",         &d) == DATE_ERR_EMPTY);
    CHECK(parse("20261030",    &d) == DATE_ERR_FORMAT);
    CHECK(parse("2026/10/30",  &d) == DATE_ERR_FORMAT);
    CHECK(parse("2026-1-30",   &d) == DATE_ERR_FORMAT);
    CHECK(parse("2026-10-3x",  &d) == DATE_ERR_FORMAT);
    CHECK(parse("2026-10-300", &d) == DATE_ERR_FORMAT);
    CHECK(parse("today",       &d) == DATE_ERR_FORMAT);   // only date_parse_user knows it

    CHECK(parse("2026-02-29",  &d) == DATE_ERR_RANGE);    // 2026 is not a leap year
    CHECK(parse("1900-02-29",  &d) == DATE_ERR_RANGE);    // divisible by 100 but not 400
    CHECK(parse("2026-04-31",  &d) == DATE_ERR_RANGE);
    CHECK(parse("2026-13-01",  &d) == DATE_ERR_RANGE);
    CHECK(parse("2026-00-10",  &d) == DATE_ERR_RANGE);
    CHECK(parse("2026-10-00",  &d) == DATE_ERR_RANGE);
    CHECK(parse("0000-01-01",  &d) == DATE_ERR_RANGE);
}

static void test_parse_user(void)
{
    Date today = {2026, 11, 2};
    Date d = {0};

    CHECK(date_parse_user(nob_sv_from_cstr("today"),      today, &d) == DATE_OK && is(d, 2026, 11, 2));
    CHECK(date_parse_user(nob_sv_from_cstr("yesterday"),  today, &d) == DATE_OK && is(d, 2026, 11, 1));
    CHECK(date_parse_user(nob_sv_from_cstr("2026-10-25"), today, &d) == DATE_OK && is(d, 2026, 10, 25));
    CHECK(date_parse_user(nob_sv_from_cstr("Today"),      today, &d) == DATE_ERR_FORMAT);
    CHECK(date_parse_user(nob_sv_from_cstr(""),           today, &d) == DATE_ERR_EMPTY);

    Date newyear = {2026, 1, 1};
    CHECK(date_parse_user(nob_sv_from_cstr("yesterday"), newyear, &d) == DATE_OK && is(d, 2025, 12, 31));
}

static void test_calendar(void)
{
    CHECK(date_days_in_month(2024, 2) == 29);
    CHECK(date_days_in_month(2026, 2) == 28);
    CHECK(date_days_in_month(2100, 2) == 28);
    CHECK(date_days_in_month(2000, 2) == 29);
    CHECK(date_days_in_month(2026, 10) == 31);
    CHECK(date_days_in_month(2026, 11) == 30);

    CHECK(date_to_days((Date){1970, 1, 1})  == 0);
    CHECK(date_to_days((Date){1970, 1, 2})  == 1);
    CHECK(date_to_days((Date){1969, 12, 31}) == -1);
    CHECK(date_to_days((Date){2000, 3, 1})  == 11017);

    CHECK(date_weekday((Date){2026, 11, 2}) == 0);   // Monday
    CHECK(date_weekday((Date){2026, 11, 1}) == 6);   // Sunday
    CHECK(date_weekday((Date){2026, 10, 30}) == 4);  // Friday
    CHECK(date_weekday((Date){1970, 1, 1})  == 3);   // Thursday

    CHECK(date_compare((Date){2026, 1, 1}, (Date){2026, 1, 2}) < 0);
    CHECK(date_compare((Date){2026, 2, 1}, (Date){2026, 1, 31}) > 0);
    CHECK(date_compare((Date){2026, 5, 5}, (Date){2026, 5, 5}) == 0);
}

static void test_ranges(void)
{
    CHECK(is(date_week_start((Date){2026, 11, 2}),  2026, 11, 2));    // already Monday
    CHECK(is(date_week_start((Date){2026, 11, 1}),  2026, 10, 26));   // Sunday -> previous Monday
    CHECK(is(date_week_start((Date){2026, 11, 8}),  2026, 11, 2));
    CHECK(is(date_week_start((Date){2026, 1, 1}),   2025, 12, 29));   // crosses the year

    CHECK(is(date_month_start((Date){2026, 10, 15}), 2026, 10, 1));
    CHECK(is(date_month_end((Date){2026, 10, 15}),   2026, 10, 31));
    CHECK(is(date_month_end((Date){2026, 2, 1}),     2026, 2, 28));
    CHECK(is(date_month_end((Date){2028, 2, 1}),     2028, 2, 29));

    CHECK(is(date_add_days((Date){2026, 10, 30}, 3),  2026, 11, 2));
    CHECK(is(date_add_days((Date){2026, 3, 1}, -1),   2026, 2, 28));
    CHECK(is(date_add_days((Date){2028, 3, 1}, -1),   2028, 2, 29));
    CHECK(is(date_add_days((Date){2026, 12, 31}, 1),  2027, 1, 1));
}

static void test_round_trip(void)
{
    int prev_weekday = -1;
    for (int64_t n = -200000; n <= 200000; ++n) {
        Date d = date_from_days(n);
        if (date_to_days(d) != n) { CHECK(!"round trip"); break; }
        if (d.month < 1 || d.month > 12 || d.day < 1 ||
            d.day > date_days_in_month(d.year, d.month)) { CHECK(!"invalid date produced"); break; }

        int wd = date_weekday(d);
        if (prev_weekday >= 0 && wd != (prev_weekday + 1) % 7) { CHECK(!"weekday sequence"); break; }
        prev_weekday = wd;
    }
}

static void test_format(void)
{
    CHECK(strcmp(date_format((Date){2026, 1, 5}).text, "2026-01-05") == 0);
    CHECK(strcmp(date_format((Date){7, 3, 9}).text,    "0007-03-09") == 0);
}

int main(void)
{
    test_parse();
    test_parse_user();
    test_calendar();
    test_ranges();
    test_round_trip();
    test_format();

    if (failures > 0) {
        fprintf(stderr, "date: %d failure(s)\n", failures);
        return 1;
    }
    puts("date: all tests passed");
    return 0;
}