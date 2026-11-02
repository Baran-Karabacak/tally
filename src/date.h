#ifndef TALLY_DATE_H
#define TALLY_DATE_H

#include <stdbool.h>
#include <stdint.h>
#include "nob.h"

typedef struct {
    int year; // 1..9999
    int month;
    int day;
} Date;

typedef enum {
    DATE_OK = 0,
    DATE_ERR_EMPTY,
    DATE_ERR_FORMAT, // not shaped like YYYY-MM-DD
    DATE_ERR_RANGE, // shaped right, but not a real calendar date (2026-02-31)
} Date_Err;

// Returned by value, same trick as Money_Str.
typedef struct {
    char text[16];
} Date_Str;

bool date_is_leap(int year);
int date_days_in_month(int year, int month);

// "YYYY-MM-DD"
Date_Err date_parse(Nob_String_View sv, Date *out);

// Like date_parse, but also accepts "today" and "yesterday" (relative to `today`).
Date_Err date_parse_user(Nob_String_View sv, Date today, Date *out);

Date_Str date_format(Date d);
const char *date_err_str(Date_Err err);

// Local calendar date of this machine
Date date_today(void);

// Days since 1970-01-01 (negative before it) and back.
int64_t date_to_days(Date d);
Date date_from_days(int64_t days);

int date_weekday(Date d); // Monday = 0 ... Sunday = 6
int date_compare(Date a, Date b); // <0, 0, >0
Date date_add_days(Date d, int64_t n);
Date date_week_start(Date d); // the Monday on or before d
Date date_month_start(Date d);
Date date_month_end(Date d);

#endif // TALLY_DATE_H
