/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * ctime.h - dates and times for the calendar.
 *
 * A moment is a CTIME: minutes since 1970-01-01 00:00. In 32 bits that
 * reaches the year 6000 either way, so repeating events far in the
 * future can't overflow the way seconds would in 2038. Most CTIMEs are
 * UTC; "wall" times (what a clock in some zone shows) are the same type,
 * and the comments say which is which.
 *
 * Also the Hebrew calendar, for showing Hebrew dates next to the civil
 * ones, as the Mac's Calendar can.
 */
#ifndef CTIME_H
#define CTIME_H

typedef long CTIME;

#define MIN_PER_DAY 1440L
#define CT_NONE (-0x7fffffffL)		/* no time at all */

typedef struct {
	short year, mon, day;		/* mon 1-12 */
	short hour, min;
	short wday;			/* 0 Sunday .. 6 Saturday */
} DT;

long  days_from_civil(int y, int m, int d);	/* days since 1970-01-01 */
void  civil_from_days(long days, int *y, int *m, int *d);
int   wday_of(long days);			/* 0 = Sunday */
int   is_leap(int y);
int   days_in_month(int y, int m);
CTIME ct_make(int y, int mo, int d, int h, int mi);
void  ct_split(CTIME t, DT *out);
long  ct_days(CTIME t);				/* the day a moment falls on (floor) */
CTIME ct_day_start(CTIME t);			/* 00:00 of that day */
/* add months to a day, keeping the day of the month where possible */
long  days_add_months(long days, int months);

/* parse "20261010", "20261010T093000", "20261010T093000Z"; returns
   1 (date only), 2 (date-time), 3 (UTC date-time) or 0 */
int   ct_parse(const char *s, CTIME *out);
/* format as iCalendar: date only, local date-time, or UTC with 'Z' */
void  ct_format_ics(CTIME t, int kind, char *out, int size);

/* names */
extern const char *const month_names[12];	/* "January" */
extern const char *const month_short[12];	/* "Jan" */
extern const char *const wday_names[7];		/* "Sunday" */
extern const char *const wday_short[7];		/* "Sun" */
/* "9:30" or "9:30am"; h24 picks the style */
void  fmt_time(int hour, int min, int h24, char *out, int size);

/* ---- the Hebrew calendar ----
 * Months are numbered as in "Calendrical Calculations": Nisan = 1 ...
 * Adar = 12, Adar II = 13; the year starts with Tishri (7). */
typedef struct {
	long year;
	short mon, day;
} HDATE;

void  hebrew_from_days(long days, HDATE *h);
long  days_from_hebrew(long y, int m, int d);
int   hebrew_leap(long y);
int   hebrew_month_days(long y, int m);
/* "י״ב תשרי תשפ״ז" in the Atari character set (Hebrew at 0xC2-0xDC),
   in logical (reading) order; with_year 0 leaves the year out */
void  hebrew_format(const HDATE *h, int with_year, char *out, int size);
/* the day of the month alone as Hebrew numerals: "י״ב" */
void  hebrew_numeral(long n, char *out, int size);

#endif
