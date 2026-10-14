/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * tz.h - time zones.
 *
 * A zone is a standard offset and, where clocks change, the rule for
 * when summer time starts and ends ("the Friday before the last Sunday
 * of March at 2:00" for Israel). The common zones are built in under
 * their IANA names, which iCloud, Google and Fastmail use; others come
 * from the VTIMEZONE blocks in the calendar data (tz_learn).
 */
#ifndef TZ_H
#define TZ_H

#include "ctime.h"

typedef struct {
	unsigned char mon;	/* 1-12; 0 = no change */
	unsigned char wday;	/* 0 Sunday */
	signed char day;	/* the first wday on or after this day; 0 = the last one */
	unsigned char utc;	/* 1: min is UTC, 0: the clock time before the change */
	short min;		/* minutes after midnight (may be 1440: the next day) */
} TZCHANGE;

typedef struct {
	char name[40];
	short std;		/* minutes east of UTC */
	short dst;		/* extra minutes in summer, 0 = none */
	TZCHANGE on, off;	/* summer time starts, ends */
} TZONE;

/* the zone for an IANA or Windows name ("Asia/Jerusalem", "Israel
   Standard Time", "UTC", "Etc/GMT-2"); NULL if unknown */
const TZONE *tz_find(const char *name);
const TZONE *tz_utc(void);
/* only the built-in zones and their other names */
const TZONE *tz_find_builtin(const char *name);
/* the n-th built-in zone, for the zone picker; NULL past the end */
const TZONE *tz_index(int n);
int tz_count(void);

/* remember a zone defined in calendar data (VTIMEZONE), so later
   events naming it work; returns the stored zone */
const TZONE *tz_learn(const TZONE *z);

/* minutes east of UTC in zone z at the moment utc */
int   tz_offset(const TZONE *z, CTIME utc);
CTIME tz_to_local(const TZONE *z, CTIME utc);
/* wall clock -> UTC; a time skipped in spring counts as after the
   change, one that happens twice in autumn as the first */
CTIME tz_to_utc(const TZONE *z, CTIME local);
/* "GMT+2", "GMT-3:30" for the moment utc */
void  tz_label(const TZONE *z, CTIME utc, char *out, int size);
/* the zone as a VTIMEZONE block (CRLF lines) for an outgoing event */
void  tz_vtimezone(const TZONE *z, char *out, int size);
/* "Jerusalem" from "Asia/Jerusalem" */
const char *tz_city(const TZONE *z);

#endif
