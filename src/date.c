#include "date.h"

#include <stdio.h>
#include <time.h>

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

// `p` must point at `n` ASCII digits (the caller checked).
static int digits_to_int(const char *p, size_t n) {
    int value = 0;
    for (size_t i = 0; i < n; ++i) value = value * 10 + (p[i] - '0');
    return value;
}

bool date_is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int date_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && date_is_leap(year)) return 29;
    return days[month - 1];
}

Date_Err date_parse(Nob_String_View sv, Date *out) {
    sv = nob_sv_trim(sv);
    if (sv.count == 0) return DATE_ERR_EMPTY;
    if (sv.count != 10 || sv.data[4] != '-' || sv.data[7] != '-') return DATE_ERR_FORMAT;

    for (size_t i = 0; i < sv.count; ++i) {
        if (i == 4 || i == 7) continue;
        if (!is_digit(sv.data[i])) return DATE_ERR_FORMAT;
    }

    Date d = {
        .year = digits_to_int(sv.data + 0, 4),
        .month = digits_to_int(sv.data + 5, 2),
        .day = digits_to_int(sv.data + 8, 2),
    };

    if (d.year < 1) return DATE_ERR_RANGE;
    if (d.month < 1 || d.month > 12) return DATE_ERR_RANGE;
    if (d.day < 1 || d.day > date_days_in_month(d.year, d.month)) return DATE_ERR_RANGE;

    *out = d;
    return DATE_OK;
}

Date_Err date_parse_user(Nob_String_View sv, Date today, Date *out) {
    sv = nob_sv_trim(sv);
    if (sv.count == 0) return DATE_ERR_EMPTY;

    if (nob_sv_eq(sv, nob_sv_from_cstr("today"))) {
        *out = today;
        return DATE_OK;
    }
    if (nob_sv_eq(sv, nob_sv_from_cstr("yesterday"))) {
        *out = date_add_days(today, -1);
        return DATE_OK;
    }
    return date_parse(sv, out);
}

Date_Str date_format(Date d) {
    Date_Str out = {{0}};
    snprintf(out.text, sizeof out.text, "%04d-%02d-%02d", d.year, d.month, d.day);
    return out;
}

const char *date_err_str(Date_Err err) {
    switch (err) {
        case DATE_OK: return "ok";
        case DATE_ERR_EMPTY: return "date is empty";
        case DATE_ERR_FORMAT: return "invalid date (expected YYYY-MM-DD, \"today\", \"yesterday\")";
        case DATE_ERR_RANGE:  return "that date does not exist in the calendar";
    }
    return "unknown error";
}

Date date_today(void) {
    time_t now = time(NULL);
    struct tm tm = {0};
    if (localtime_r(&now, &tm) == NULL) gmtime_r(&now, &tm); // Should never happen
    return (Date){ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

// Days from civil algorithm
int64_t date_to_days(Date d) {
    int64_t y = d.year - (d.month <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;                   // 400-year cycle
    int64_t yoe = y - era * 400;                                  // [0, 399]
    int64_t mp  = (d.month > 2) ? d.month - 3 : d.month + 9;      // March = 0
    int64_t doy = (153 * mp + 2) / 5 + d.day - 1;                 // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;                           // 719468 = days to 1970-01-01
}

Date date_from_days(int64_t z) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                          // [0, 146096]
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;     // [0, 399]
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                   // [0, 365]
    int64_t mp  = (5 * doy + 2) / 153;                                       // [0, 11]
    int64_t day   = doy - (153 * mp + 2) / 5 + 1;
    int64_t month = (mp < 10) ? mp + 3 : mp - 9;
    int64_t year  = yoe + era * 400 + (month <= 2);
    return (Date){ (int)year, (int)month, (int)day };
}

int date_weekday(Date d) {
    // 1970-01-01 was a Thursday
    int64_t w = (date_to_days(d) + 3) % 7;
    if (w < 0) w += 7;
    return (int)w;
}

int date_compare(Date a, Date b) {
    if (a.year != b.year) return a.year - b.year;
    if (a.month != b.month) return a.month - b.month;
    return a.day - b.day;
}

Date date_add_days(Date d, int64_t n) {
    return date_from_days(date_to_days(d) + n);
}

Date date_week_start(Date d)
{
    return date_add_days(d, -date_weekday(d));
}

Date date_month_start(Date d)
{
    return (Date){ d.year, d.month, 1 };
}

Date date_month_end(Date d)
{
    return (Date){ d.year, d.month, date_days_in_month(d.year, d.month) };
}