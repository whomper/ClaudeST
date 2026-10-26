/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * src/ctime.c's quick ways (one divu.w where a quotient fits in 16 bits)
 * against the plain 32-bit arithmetic it had: every day from 2800 BC to
 * AD 10000, every minute's split near the ends of the quick range and a
 * spread of moments across all of CTIME. Here and as 68000 code under
 * qemu-m68k:
 *   make test
 */
#include <stdio.h>
#include "ctime.h"

static long checks, failed;

static void check(int ok, const char *what, long v)
{
	checks++;
	if (!ok && failed++ < 10)
		printf("FAIL %s at %ld\n", what, v);
}

/* the plain way, as ctime.c had it */
static long fdiv(long a, long b)
{
	long q = a / b;
	if ((a % b) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

static void ref_civil(long z, int *y, int *m, int *d)
{
	long era, doe, yoe, doy, mp, yy;
	z += 719468L;
	era = fdiv(z, 146097L);
	doe = z - era * 146097L;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	yy = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yy + (*m <= 2));
}

static int ref_wday(long days)
{
	long w = (days + 4) % 7;
	return (int)(w < 0 ? w + 7 : w);
}

static void one_moment(CTIME t)
{
	DT o;
	long days = fdiv(t, MIN_PER_DAY), m = t - days * MIN_PER_DAY;
	int y, mo, d;
	ref_civil(days, &y, &mo, &d);
	check(ct_days(t) == days, "ct_days", t);
	ct_split(t, &o);
	check(o.year == y && o.mon == mo && o.day == d && o.hour == m / 60 && o.min == m % 60 &&
	      o.wday == ref_wday(days), "ct_split", t);
}

int main(void)
{
	long days, t, i;
	for (days = -1740000L; days <= 3000000L; days++) {
		int y, m, d, ry, rm, rd;
		civil_from_days(days, &y, &m, &d);
		ref_civil(days, &ry, &rm, &rd);
		check(y == ry && m == rm && d == rd, "civil_from_days", days);
		check(wday_of(days) == ref_wday(days), "wday_of", days);
		check(days_from_civil(y, m, d) == days, "days_from_civil back", days);
	}
	/* every minute of the days around the quick range's ends */
	for (t = -3 * MIN_PER_DAY; t < 3 * MIN_PER_DAY; t++) {
		one_moment(t);
		one_moment(MIN_PER_DAY * 65536 + t);
	}
	/* a spread across all of CTIME, both signs */
	for (i = 0, t = 1; i < 200000; i++) {
		t = t * 1103515245L + 12345;
		one_moment(t & 0x7fffffffL);
		one_moment(-(t & 0x3fffffffL));
	}
	printf("%ld checks, %ld failed\n", checks, failed);
	return failed != 0;
}
