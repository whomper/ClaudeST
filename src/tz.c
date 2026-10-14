/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * tz.c - the built-in time zones and summer-time arithmetic.
 *
 * The rules are today's (2026). Past changes of rule are not kept: an
 * event from before a country changed its rules may show an hour off,
 * which a calendar can live with.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tz.h"
#include "util.h"

#define NONE   { 0, 0, 0, 0, 0 }
#define EU_ON  { 3, 0, 0, 1, 60 }	/* last Sunday of March, 01:00 UTC */
#define EU_OFF { 10, 0, 0, 1, 60 }	/* last Sunday of October, 01:00 UTC */
#define US_ON  { 3, 0, 8, 0, 120 }	/* second Sunday of March, 2:00 */
#define US_OFF { 11, 0, 1, 0, 120 }	/* first Sunday of November, 2:00 */
#define IL_ON  { 3, 5, 23, 0, 120 }	/* Friday before the last Sunday of March, 2:00 */
#define IL_OFF { 10, 0, 0, 0, 120 }	/* last Sunday of October, 2:00 */
#define AU_ON  { 10, 0, 1, 0, 120 }	/* first Sunday of October, 2:00 */
#define AU_OFF { 4, 0, 1, 0, 180 }	/* first Sunday of April, 3:00 */
#define NZ_ON  { 9, 0, 0, 0, 120 }	/* last Sunday of September */
#define NZ_OFF { 4, 0, 1, 0, 180 }
#define LB_ON  { 3, 0, 0, 0, 0 }	/* Lebanon: last Sundays, midnight */
#define LB_OFF { 10, 0, 0, 0, 0 }
#define EG_ON  { 4, 5, 0, 0, 0 }	/* Egypt: last Friday of April */
#define EG_OFF { 10, 4, 0, 0, 1440 }	/* to the end of the last Thursday of October */
#define CL_ON  { 9, 0, 2, 1, 240 }	/* Chile: first Sunday after the first Saturday of September */
#define CL_OFF { 4, 0, 2, 1, 180 }
#define CU_ON  { 3, 0, 8, 0, 0 }	/* Cuba */
#define CU_OFF { 11, 0, 1, 0, 60 }

static const TZONE zones[] = {
	{ "UTC", 0, 0, NONE, NONE },
	{ "Europe/London", 0, 60, EU_ON, EU_OFF },
	{ "Europe/Dublin", 0, 60, EU_ON, EU_OFF },
	{ "Europe/Lisbon", 0, 60, EU_ON, EU_OFF },
	{ "Atlantic/Reykjavik", 0, 0, NONE, NONE },
	{ "Atlantic/Azores", -60, 60, EU_ON, EU_OFF },
	{ "Europe/Paris", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Berlin", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Amsterdam", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Brussels", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Madrid", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Rome", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Vienna", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Zurich", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Stockholm", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Oslo", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Copenhagen", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Prague", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Warsaw", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Budapest", 60, 60, EU_ON, EU_OFF },
	{ "Europe/Belgrade", 60, 60, EU_ON, EU_OFF },
	{ "Africa/Lagos", 60, 0, NONE, NONE },
	{ "Africa/Casablanca", 60, 0, NONE, NONE },
	{ "Europe/Athens", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Helsinki", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Kyiv", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Bucharest", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Sofia", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Riga", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Tallinn", 120, 60, EU_ON, EU_OFF },
	{ "Europe/Vilnius", 120, 60, EU_ON, EU_OFF },
	{ "Asia/Nicosia", 120, 60, EU_ON, EU_OFF },
	{ "Asia/Jerusalem", 120, 60, IL_ON, IL_OFF },
	{ "Asia/Beirut", 120, 60, LB_ON, LB_OFF },
	{ "Africa/Cairo", 120, 60, EG_ON, EG_OFF },
	{ "Africa/Johannesburg", 120, 0, NONE, NONE },
	{ "Europe/Istanbul", 180, 0, NONE, NONE },
	{ "Europe/Moscow", 180, 0, NONE, NONE },
	{ "Asia/Amman", 180, 0, NONE, NONE },
	{ "Asia/Riyadh", 180, 0, NONE, NONE },
	{ "Africa/Nairobi", 180, 0, NONE, NONE },
	{ "Asia/Tehran", 210, 0, NONE, NONE },
	{ "Asia/Dubai", 240, 0, NONE, NONE },
	{ "Asia/Karachi", 300, 0, NONE, NONE },
	{ "Asia/Tashkent", 300, 0, NONE, NONE },
	{ "Asia/Kolkata", 330, 0, NONE, NONE },
	{ "Asia/Kathmandu", 345, 0, NONE, NONE },
	{ "Asia/Dhaka", 360, 0, NONE, NONE },
	{ "Asia/Bangkok", 420, 0, NONE, NONE },
	{ "Asia/Jakarta", 420, 0, NONE, NONE },
	{ "Asia/Shanghai", 480, 0, NONE, NONE },
	{ "Asia/Hong_Kong", 480, 0, NONE, NONE },
	{ "Asia/Singapore", 480, 0, NONE, NONE },
	{ "Asia/Taipei", 480, 0, NONE, NONE },
	{ "Australia/Perth", 480, 0, NONE, NONE },
	{ "Asia/Tokyo", 540, 0, NONE, NONE },
	{ "Asia/Seoul", 540, 0, NONE, NONE },
	{ "Australia/Darwin", 570, 0, NONE, NONE },
	{ "Australia/Adelaide", 570, 60, AU_ON, AU_OFF },
	{ "Australia/Brisbane", 600, 0, NONE, NONE },
	{ "Australia/Sydney", 600, 60, AU_ON, AU_OFF },
	{ "Australia/Melbourne", 600, 60, AU_ON, AU_OFF },
	{ "Australia/Hobart", 600, 60, AU_ON, AU_OFF },
	{ "Pacific/Auckland", 720, 60, NZ_ON, NZ_OFF },
	{ "Pacific/Honolulu", -600, 0, NONE, NONE },
	{ "America/Anchorage", -540, 60, US_ON, US_OFF },
	{ "America/Los_Angeles", -480, 60, US_ON, US_OFF },
	{ "America/Vancouver", -480, 60, US_ON, US_OFF },
	{ "America/Phoenix", -420, 0, NONE, NONE },
	{ "America/Denver", -420, 60, US_ON, US_OFF },
	{ "America/Edmonton", -420, 60, US_ON, US_OFF },
	{ "America/Chicago", -360, 60, US_ON, US_OFF },
	{ "America/Winnipeg", -360, 60, US_ON, US_OFF },
	{ "America/Mexico_City", -360, 0, NONE, NONE },
	{ "America/New_York", -300, 60, US_ON, US_OFF },
	{ "America/Toronto", -300, 60, US_ON, US_OFF },
	{ "America/Havana", -300, 60, CU_ON, CU_OFF },
	{ "America/Bogota", -300, 0, NONE, NONE },
	{ "America/Lima", -300, 0, NONE, NONE },
	{ "America/Halifax", -240, 60, US_ON, US_OFF },
	{ "America/Caracas", -240, 0, NONE, NONE },
	{ "America/Santiago", -240, 60, CL_ON, CL_OFF },
	{ "America/St_Johns", -210, 60, US_ON, US_OFF },
	{ "America/Sao_Paulo", -180, 0, NONE, NONE },
	{ "America/Argentina/Buenos_Aires", -180, 0, NONE, NONE },
	{ "America/Montevideo", -180, 0, NONE, NONE },
};
#define NZONES ((int)(sizeof(zones) / sizeof(zones[0])))

/* other names for the same zones: old IANA links and Windows names */
static const char *const aliases[][2] = {
	{ "Asia/Tel_Aviv", "Asia/Jerusalem" }, { "Israel", "Asia/Jerusalem" },
	{ "Israel Standard Time", "Asia/Jerusalem" }, { "Jerusalem Standard Time", "Asia/Jerusalem" },
	{ "Europe/Kiev", "Europe/Kyiv" }, { "Asia/Calcutta", "Asia/Kolkata" },
	{ "Asia/Katmandu", "Asia/Kathmandu" }, { "Asia/Saigon", "Asia/Bangkok" },
	{ "Asia/Ho_Chi_Minh", "Asia/Bangkok" }, { "Europe/Luxembourg", "Europe/Brussels" },
	{ "Europe/Monaco", "Europe/Paris" }, { "Europe/Malta", "Europe/Rome" },
	{ "Europe/Ljubljana", "Europe/Belgrade" }, { "Europe/Zagreb", "Europe/Belgrade" },
	{ "Europe/Bratislava", "Europe/Prague" }, { "Europe/Minsk", "Europe/Moscow" },
	{ "US/Pacific", "America/Los_Angeles" }, { "US/Mountain", "America/Denver" },
	{ "US/Central", "America/Chicago" }, { "US/Eastern", "America/New_York" },
	{ "US/Hawaii", "Pacific/Honolulu" }, { "US/Alaska", "America/Anchorage" },
	{ "US/Arizona", "America/Phoenix" }, { "Canada/Eastern", "America/Toronto" },
	{ "America/Montreal", "America/Toronto" }, { "America/Buenos_Aires", "America/Argentina/Buenos_Aires" },
	{ "Australia/Canberra", "Australia/Sydney" }, { "Australia/ACT", "Australia/Sydney" },
	{ "Asia/Chongqing", "Asia/Shanghai" }, { "PRC", "Asia/Shanghai" }, { "Japan", "Asia/Tokyo" },
	{ "GB", "Europe/London" }, { "Eire", "Europe/Dublin" }, { "Turkey", "Europe/Istanbul" },
	{ "Egypt", "Africa/Cairo" }, { "Iran", "Asia/Tehran" }, { "NZ", "Pacific/Auckland" },
	{ "Pacific Standard Time", "America/Los_Angeles" }, { "Mountain Standard Time", "America/Denver" },
	{ "US Mountain Standard Time", "America/Phoenix" }, { "Central Standard Time", "America/Chicago" },
	{ "Eastern Standard Time", "America/New_York" }, { "Atlantic Standard Time", "America/Halifax" },
	{ "Hawaiian Standard Time", "Pacific/Honolulu" }, { "Alaskan Standard Time", "America/Anchorage" },
	{ "GMT Standard Time", "Europe/London" }, { "Greenwich Standard Time", "Atlantic/Reykjavik" },
	{ "W. Europe Standard Time", "Europe/Berlin" }, { "Romance Standard Time", "Europe/Paris" },
	{ "Central Europe Standard Time", "Europe/Budapest" }, { "Central European Standard Time", "Europe/Warsaw" },
	{ "E. Europe Standard Time", "Europe/Bucharest" }, { "FLE Standard Time", "Europe/Kyiv" },
	{ "GTB Standard Time", "Europe/Athens" }, { "Middle East Standard Time", "Asia/Beirut" },
	{ "Egypt Standard Time", "Africa/Cairo" }, { "South Africa Standard Time", "Africa/Johannesburg" },
	{ "Russian Standard Time", "Europe/Moscow" }, { "Turkey Standard Time", "Europe/Istanbul" },
	{ "Arabian Standard Time", "Asia/Dubai" }, { "India Standard Time", "Asia/Kolkata" },
	{ "China Standard Time", "Asia/Shanghai" }, { "Singapore Standard Time", "Asia/Singapore" },
	{ "Tokyo Standard Time", "Asia/Tokyo" }, { "Korea Standard Time", "Asia/Seoul" },
	{ "AUS Eastern Standard Time", "Australia/Sydney" }, { "E. Australia Standard Time", "Australia/Brisbane" },
	{ "W. Australia Standard Time", "Australia/Perth" }, { "New Zealand Standard Time", "Pacific/Auckland" },
	{ "E. South America Standard Time", "America/Sao_Paulo" }, { "Argentina Standard Time", "America/Argentina/Buenos_Aires" },
	{ "SA Pacific Standard Time", "America/Bogota" }, { "Pacific SA Standard Time", "America/Santiago" },
	{ "Etc/UTC", "UTC" }, { "Etc/GMT", "UTC" }, { "GMT", "UTC" }, { "Z", "UTC" },
	{ "Etc/Universal", "UTC" }, { "Universal", "UTC" }, { "Coordinated Universal Time", "UTC" },
	{ "UTC Standard Time", "UTC" },
};
#define NALIASES ((int)(sizeof(aliases) / sizeof(aliases[0])))

/* zones defined in calendar data, and fixed offsets met on the way */
#define NLEARN 24
static TZONE learned[NLEARN];
static int nlearned, next_learn;

const TZONE *tz_utc(void)
{
	return &zones[0];
}

int tz_count(void)
{
	return NZONES;
}

const TZONE *tz_index(int n)
{
	return n >= 0 && n < NZONES ? &zones[n] : 0;
}

static void memo_clear(void);

const TZONE *tz_learn(const TZONE *z)
{
	int i;
	memo_clear();		/* a slot may be reused for another zone */
	for (i = 0; i < nlearned; i++)
		if (!strcmp(learned[i].name, z->name)) {
			learned[i] = *z;
			return &learned[i];
		}
	i = nlearned < NLEARN ? nlearned++ : next_learn;
	next_learn = (i + 1) % NLEARN;
	learned[i] = *z;
	return &learned[i];
}

/* "Etc/GMT-2" (which means UTC+2: IANA's signs are POSIX's),
   "GMT+02:00", "UTC+2" */
static const TZONE *fixed_zone(const char *name)
{
	TZONE z;
	const char *p = name;
	int sign, h = 0, m = 0, posix = 0;
	if (!strncasecmp(p, "Etc/", 4)) {
		p += 4;
		posix = 1;
	}
	if (!strncasecmp(p, "GMT", 3) || !strncasecmp(p, "UTC", 3))
		p += 3;
	else
		return 0;
	if (*p != '+' && *p != '-')
		return 0;
	sign = *p++ == '-' ? -1 : 1;
	if (posix)
		sign = -sign;
	while (*p >= '0' && *p <= '9')
		h = h * 10 + (*p++ - '0');
	if (*p == ':')
		p++;
	while (*p >= '0' && *p <= '9')
		m = m * 10 + (*p++ - '0');
	if (*p || h > 14)
		return 0;
	memset(&z, 0, sizeof(z));
	str_copy(z.name, name, sizeof(z.name));
	z.std = (short)(sign * (h * 60 + m));
	return tz_learn(&z);
}

const TZONE *tz_find_builtin(const char *name)
{
	int i;
	for (i = 0; i < NZONES; i++)
		if (!strcasecmp(zones[i].name, name))
			return &zones[i];
	for (i = 0; i < NALIASES; i++)
		if (!strcasecmp(aliases[i][0], name))
			return tz_find_builtin(aliases[i][1]);
	return 0;
}

/* the last names asked for: every event of a calendar names the same
   zone or two, and the search below compares against ~100 names */
#define NMEMO 4
static struct {
	char name[48];
	const TZONE *z;
} memo[NMEMO];
static int memo_next;

static void memo_clear(void)
{
	memset(memo, 0, sizeof(memo));
}

static const TZONE *tz_search(const char *name);

const TZONE *tz_find(const char *name)
{
	const TZONE *z;
	int i;
	if (!name || !*name)
		return 0;
	for (i = 0; i < NMEMO; i++)
		if (memo[i].z && !strcmp(memo[i].name, name))
			return memo[i].z;
	z = tz_search(name);
	if (z && strlen(name) < sizeof(memo[0].name)) {
		str_copy(memo[memo_next].name, name, sizeof(memo[0].name));
		memo[memo_next].z = z;
		memo_next = (memo_next + 1) % NMEMO;
	}
	return z;
}

static const TZONE *tz_search(const char *name)
{
	int i;
	/* some clients quote the name or prefix it with a vendor path */
	if (*name == '/') {
		const char *p = strstr(name, "/zoneinfo/");
		if (p)
			name = p + 10;
	}
	for (i = 0; i < nlearned; i++)
		if (!strcmp(learned[i].name, name))
			return &learned[i];
	for (i = 0; i < NZONES; i++)
		if (!strcasecmp(zones[i].name, name))
			return &zones[i];
	for (i = 0; i < NALIASES; i++)
		if (!strcasecmp(aliases[i][0], name))
			return tz_find(aliases[i][1]);
	return fixed_zone(name);
}

/* the moment (UTC) of a change in year y; before = the offset in force
   until then */
static CTIME change_utc(const TZCHANGE *c, int y, int before)
{
	int from = c->day ? c->day : days_in_month(y, c->mon) - 6;
	long d;
	CTIME t;
	if (from > days_in_month(y, c->mon))
		from = days_in_month(y, c->mon);
	d = days_from_civil(y, c->mon, from);
	d += (c->wday - wday_of(d) + 7) % 7;
	t = d * MIN_PER_DAY + c->min;
	if (!c->utc)
		t -= before;
	return t;
}

int tz_offset(const TZONE *z, CTIME utc)
{
	DT d;
	CTIME on, off;
	int in;
	if (!z)
		return 0;
	if (!z->dst || !z->on.mon || !z->off.mon)
		return z->std;
	ct_split(utc + z->std, &d);
	on = change_utc(&z->on, d.year, z->std);
	off = change_utc(&z->off, d.year, z->std + z->dst);
	if (on < off)
		in = utc >= on && utc < off;
	else			/* southern hemisphere: summer spans the new year */
		in = utc >= on || utc < off;
	return in ? z->std + z->dst : z->std;
}

CTIME tz_to_local(const TZONE *z, CTIME utc)
{
	return utc + tz_offset(z, utc);
}

CTIME tz_to_utc(const TZONE *z, CTIME local)
{
	CTIME t;
	if (!z)
		return local;
	if (z->dst) {
		t = local - (z->std + z->dst);
		if (tz_offset(z, t) == z->std + z->dst)
			return t;
	}
	return local - z->std;
}

void tz_label(const TZONE *z, CTIME utc, char *out, int size)
{
	int off = tz_offset(z, utc), a = off < 0 ? -off : off;
	if (!off)
		str_copy(out, "GMT", size);
	else if (a % 60)
		snprintf(out, size, "GMT%c%d:%02d", off < 0 ? '-' : '+', a / 60, a % 60);
	else
		snprintf(out, size, "GMT%c%d", off < 0 ? '-' : '+', a / 60);
}

const char *tz_city(const TZONE *z)
{
	static char buf[40];
	const char *p;
	char *q;
	if (!z)
		return "Floating";
	p = strrchr(z->name, '/');
	str_copy(buf, p ? p + 1 : z->name, sizeof(buf));
	for (q = buf; *q; q++)
		if (*q == '_')
			*q = ' ';
	return buf;
}

static void fmt_off(int off, char *out)
{
	int a = off < 0 ? -off : off;
	sprintf(out, "%c%02d%02d", off < 0 ? '-' : '+', a / 60, a % 60);
}

static void add_change(SBUF *b, const char *kind, const TZCHANGE *c, int from, int to)
{
	static const char *const wd[7] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
	char f[8], t[8];
	int min = c->utc ? c->min + from : c->min;
	if (min >= 1440)
		min = 1439;
	fmt_off(from, f);
	fmt_off(to, t);
	sb_printf(b, "BEGIN:%s\r\nDTSTART:1970%02d01T%02d%02d00\r\nTZOFFSETFROM:%s\r\nTZOFFSETTO:%s\r\n",
		  kind, c->mon, min / 60, min % 60, f, t);
	if (!c->day)
		sb_printf(b, "RRULE:FREQ=YEARLY;BYMONTH=%d;BYDAY=-1%s\r\n", c->mon, wd[c->wday]);
	else if (c->day % 7 == 1)
		sb_printf(b, "RRULE:FREQ=YEARLY;BYMONTH=%d;BYDAY=%d%s\r\n", c->mon, c->day / 7 + 1, wd[c->wday]);
	else
		sb_printf(b, "RRULE:FREQ=YEARLY;BYMONTH=%d;BYDAY=%s;BYMONTHDAY=%d,%d,%d,%d,%d,%d,%d\r\n",
			  c->mon, wd[c->wday], c->day, c->day + 1, c->day + 2, c->day + 3,
			  c->day + 4, c->day + 5, c->day + 6);
	sb_printf(b, "END:%s\r\n", kind);
}

void tz_vtimezone(const TZONE *z, char *out, int size)
{
	SBUF b;
	char o[8];
	sb_init(&b);
	sb_printf(&b, "BEGIN:VTIMEZONE\r\nTZID:%s\r\n", z->name);
	if (z->dst && z->on.mon) {
		add_change(&b, "DAYLIGHT", &z->on, z->std, z->std + z->dst);
		add_change(&b, "STANDARD", &z->off, z->std + z->dst, z->std);
	} else {
		fmt_off(z->std, o);
		sb_printf(&b, "BEGIN:STANDARD\r\nDTSTART:19700101T000000\r\nTZOFFSETFROM:%s\r\nTZOFFSETTO:%s\r\nEND:STANDARD\r\n", o, o);
	}
	sb_adds(&b, "END:VTIMEZONE\r\n");
	str_copy(out, b.s ? b.s : "", size);
	sb_free(&b);
}
