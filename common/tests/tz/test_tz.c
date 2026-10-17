/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_tz.c - tz_offset's kept zone-years give what the rules give
 * (tz_offset_rules), for every built-in zone, every quarter-hour around
 * each change from 1990 to 2040 and at random moments, with the zones
 * asked in turn so the few kept years are pushed out again and again;
 * and a zone whose rules change at the same address.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tz.h"

static int fails, checks;

static void same(const TZONE *z, CTIME t)
{
	int a = tz_offset(z, t), b = tz_offset_rules(z, t);
	checks++;
	if (a != b && fails++ < 10)
		printf("FAIL %s at %ld: %d, rules say %d\n", z ? z->name : "(none)", (long)t, a, b);
}

int main(void)
{
	int n = tz_count(), i, y, k;
	CTIME t;
	TZONE z;
	unsigned long r = 1;

	for (y = 1990; y <= 2040; y++)
		for (i = 0; i < n; i++) {
			const TZONE *zi = tz_index(i);
			CTIME y0 = ct_make(y, 1, 1, 0, 0);
			/* every 15 minutes of the days the changes can fall on, and
			   the turn of the year either side, in every zone in turn */
			for (k = 0; k < 12; k++) {
				CTIME m = ct_make(y, k + 1, 1, 0, 0);
				for (t = m - 60 * 24; t < m + 31 * 24 * 60; t += 15)
					if (k == 0 || k == 2 || k == 3 || k == 8 || k == 9 || k == 10)
						same(zi, t);
			}
			same(zi, y0 - 1);
			same(zi, y0);
		}
	for (k = 0; k < 2000000; k++) {		/* random zones and moments */
		r = r * 1103515245UL + 12345UL;
		i = (int)(r >> 8) % n;
		r = r * 1103515245UL + 12345UL;
		t = ct_make(1990, 1, 1, 0, 0) + (CTIME)((r >> 4) % (50UL * 366 * 24 * 60));
		same(tz_index(i), t);
	}
	same(NULL, 0);
	same(tz_utc(), 12345);

	/* one address, two sets of rules: a zone freed and its memory used
	   again must not get the old zone's years */
	memcpy(&z, tz_find("Europe/Berlin"), sizeof z);
	t = ct_make(2026, 7, 1, 12, 0);
	same(&z, t);
	memcpy(&z, tz_find("Australia/Sydney"), sizeof z);
	same(&z, t);
	if (tz_offset(&z, t) != tz_find("Australia/Sydney")->std)
		printf("FAIL the old rules came back\n"), fails++;
	z.dst = 0;
	same(&z, t);

	/* a change on new year's day (no built-in zone has one): the year
	   kept must end where the next one's rules begin. 1 January 2023 is
	   a Sunday; summer time from 02:00 that day */
	memset(&z, 0, sizeof z);
	strcpy(z.name, "Test/NewYear");
	z.std = 60;
	z.dst = 60;
	z.on.mon = 1, z.on.day = 1, z.on.min = 120;
	z.off.mon = 6, z.off.day = 1, z.off.min = 180;
	same(&z, ct_make(2022, 12, 31, 12, 0) - 60);
	same(&z, ct_make(2023, 1, 1, 3, 0) - 60);
	same(&z, ct_make(2023, 1, 1, 1, 0) - 60);

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
