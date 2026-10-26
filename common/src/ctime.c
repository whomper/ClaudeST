/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ctime.c - civil date arithmetic (proleptic Gregorian, days since
 * 1970) and the Hebrew calendar.
 */
#include <stdio.h>
#include <string.h>
#include "ctime.h"

const char *const month_names[12] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December"
};
const char *const month_short[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
const char *const wday_names[7] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
const char *const wday_short[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

/* floor division, for dates before 1970 */
static long fdiv(long a, long b)
{
	long q = a / b;
	if ((a % b) && ((a < 0) != (b < 0)))
		q--;
	return q;
}

/* n / d with the remainder, for a quotient under 65536: one divu.w on a
   68000, where a long division is a libgcc call of some 150 instructions
   (ECal: over half of a year view's collect on an ST, 26 Oct 2026) */
static unsigned short udiv16(unsigned long n, unsigned short d, unsigned short *rem)
{
#ifdef __mc68000__
	__asm__("divu.w %1,%0" : "+d"(n) : "dmi"(d));
	*rem = (unsigned short)(n >> 16);
	return (unsigned short)n;
#else
	*rem = (unsigned short)(n % d);
	return (unsigned short)(n / d);
#endif
}

#define UDIV16(n, d) ({ unsigned short r_; udiv16((n), (d), &r_); })

/* Howard Hinnant's algorithms, in 32-bit arithmetic */
long days_from_civil(int y, int m, int d)
{
	long era, yoe, doy, doe;
	y -= m <= 2;
	era = fdiv(y, 400);
	yoe = (long)y - era * 400;
	doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097L + doe - 719468L;
}

void civil_from_days(long z, int *y, int *m, int *d)
{
	long era, doe, yoe, doy, mp, yy;
	z += 719468L;
	if (z >= 0 && z < 146097L * 32) {	/* 0000 to 12800: subtracted, not divided */
		for (era = 0, doe = z; doe >= 146097L; era++)
			doe -= 146097L;
	} else {
		era = fdiv(z, 146097L);
		doe = z - era * 146097L;
	}
	/* doe is under 146097: every quotient below fits in 16 bits */
	yoe = UDIV16(doe - UDIV16(doe, 1460) + UDIV16(doe, 36524) - (doe == 146096), 365);
	yy = yoe + era * 400;
	doy = doe - (365 * yoe + (yoe >> 2) - UDIV16(yoe, 100));
	mp = UDIV16(5 * doy + 2, 153);
	*d = (int)(doy - UDIV16(153 * mp + 2, 5) + 1);
	*m = (int)(mp < 10 ? mp + 3 : mp - 9);
	*y = (int)(yy + (*m <= 2));
}

int wday_of(long days)
{
	unsigned short w;
	if (days >= -4 && days < 7L * 65536 - 4) {	/* 1970 to 3225 */
		udiv16(days + 4, 7, &w);
		return w;
	}
	{
		long l = (days + 4) % 7;	/* 1970-01-01 was a Thursday */
		return (int)(l < 0 ? l + 7 : l);
	}
}

int is_leap(int y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int days_in_month(int y, int m)
{
	static const char dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return m == 2 && is_leap(y) ? 29 : dm[m - 1];
}

CTIME ct_make(int y, int mo, int d, int h, int mi)
{
	return days_from_civil(y, mo, d) * MIN_PER_DAY + h * 60L + mi;
}

long ct_days(CTIME t)
{
	if (t >= 0 && t < MIN_PER_DAY * 65536)	/* 1970 to 2149 */
		return UDIV16(t, MIN_PER_DAY);
	return fdiv(t, MIN_PER_DAY);
}

CTIME ct_day_start(CTIME t)
{
	return ct_days(t) * MIN_PER_DAY;
}

void ct_split(CTIME t, DT *o)
{
	long days, m;
	int y, mo, d;
	unsigned short mi;
	if (t >= 0 && t < MIN_PER_DAY * 65536) {	/* as ct_days */
		days = udiv16(t, MIN_PER_DAY, &mi);
		m = mi;
	} else {
		days = fdiv(t, MIN_PER_DAY);
		m = t - days * MIN_PER_DAY;
	}
	civil_from_days(days, &y, &mo, &d);
	o->year = (short)y;
	o->mon = (short)mo;
	o->day = (short)d;
	o->hour = (short)udiv16(m, 60, &mi);
	o->min = (short)mi;
	o->wday = (short)wday_of(days);
}

long days_add_months(long days, int months)
{
	int y, m, d, dim;
	long mm;
	civil_from_days(days, &y, &m, &d);
	mm = (long)y * 12 + (m - 1) + months;
	y = (int)fdiv(mm, 12);
	m = (int)(mm - (long)y * 12) + 1;
	dim = days_in_month(y, m);
	if (d > dim)
		d = dim;
	return days_from_civil(y, m, d);
}

static int digits(const char *s, int n, int *v)
{
	int i, r = 0;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return 0;
		r = r * 10 + (s[i] - '0');
	}
	*v = r;
	return 1;
}

int ct_parse(const char *s, CTIME *out)
{
	int y, mo, d, h = 0, mi = 0, sec = 0;
	while (*s == ' ')
		s++;
	if (!digits(s, 4, &y) || !digits(s + 4, 2, &mo) || !digits(s + 6, 2, &d))
		return 0;
	if (mo < 1 || mo > 12 || d < 1 || d > 31)
		return 0;
	if (s[8] != 'T') {
		*out = ct_make(y, mo, d, 0, 0);
		return 1;
	}
	if (!digits(s + 9, 2, &h) || !digits(s + 11, 2, &mi))
		return 0;
	*out = ct_make(y, mo, d, h, mi);
	if (s[13] == 'Z')
		return 3;
	if (digits(s + 13, 2, &sec) && s[15] == 'Z')
		return 3;
	return 2;
}

void ct_format_ics(CTIME t, int kind, char *out, int size)
{
	DT d;
	ct_split(t, &d);
	if (kind == 1)
		snprintf(out, size, "%04d%02d%02d", d.year, d.mon, d.day);
	else
		snprintf(out, size, "%04d%02d%02dT%02d%02d00%s", d.year, d.mon, d.day, d.hour, d.min,
			 kind == 3 ? "Z" : "");
}

void fmt_time(int hour, int min, int h24, char *out, int size)
{
	if (h24) {
		snprintf(out, size, "%d:%02d", hour, min);
	} else {
		int h = hour % 12;
		if (!h)
			h = 12;
		if (min)
			snprintf(out, size, "%d:%02d%s", h, min, hour < 12 ? "am" : "pm");
		else
			snprintf(out, size, "%d%s", h, hour < 12 ? "am" : "pm");
	}
}

/* ---------------- the Hebrew calendar ----------------
 * After Reingold and Dershowitz, "Calendrical Calculations": the molad
 * count and the four postponement rules (dehiyyot), in 32-bit
 * arithmetic (good for the next 6000 years). Dates are "fixed" day
 * numbers there; ours count from 1970, which is fixed day 719163. */

#define FIXED_1970 719163L
#define HEB_EPOCH  (-1373427L)	/* fixed day of Tishri 1, year 1 */

static long heb_elapsed(long y)
{
	long months = (235 * y - 234) / 19;
	long parts = 12084 + 13753 * months;
	long day = 29 * months + parts / 25920;
	if ((3 * (day + 1)) % 7 < 3)
		day++;
	return day;
}

static long heb_new_year(long y)
{
	/* a small cache: a month view asks about the same years again and again */
	static long cy[4], cv[4];
	static int next;
	long ny0, ny1, ny2, r;
	int i, delay = 0;
	for (i = 0; i < 4; i++)
		if (cy[i] == y && cv[i])
			return cv[i];
	ny0 = heb_elapsed(y - 1);
	ny1 = heb_elapsed(y);
	ny2 = heb_elapsed(y + 1);
	if (ny2 - ny1 == 356)
		delay = 2;
	else if (ny1 - ny0 == 382)
		delay = 1;
	r = HEB_EPOCH + ny1 + delay;
	cy[next] = y;
	cv[next] = r;
	next = (next + 1) & 3;
	return r;
}

int hebrew_leap(long y)
{
	return (7 * y + 1) % 19 < 7;
}

static long heb_year_days(long y)
{
	return heb_new_year(y + 1) - heb_new_year(y);
}

int hebrew_month_days(long y, int m)
{
	long yd;
	if (m == 2 || m == 4 || m == 6 || m == 10 || m == 13)
		return 29;
	if (m == 12 && !hebrew_leap(y))
		return 29;
	if (m == 8 || m == 9) {
		yd = heb_year_days(y) % 10;
		if (m == 8 && yd != 5)
			return 29;	/* Heshvan is long only in a "complete" year */
		if (m == 9 && yd == 3)
			return 29;	/* Kislev is short in a "deficient" year */
	}
	return 30;
}

static long fixed_from_hebrew(long y, int m, int d)
{
	long r = heb_new_year(y) + d - 1;
	int last = hebrew_leap(y) ? 13 : 12, mm;
	if (m < 7) {
		for (mm = 7; mm <= last; mm++)
			r += hebrew_month_days(y, mm);
		for (mm = 1; mm < m; mm++)
			r += hebrew_month_days(y, mm);
	} else {
		for (mm = 7; mm < m; mm++)
			r += hebrew_month_days(y, mm);
	}
	return r;
}

long days_from_hebrew(long y, int m, int d)
{
	return fixed_from_hebrew(y, m, d) - FIXED_1970;
}

void hebrew_from_days(long days, HDATE *h)
{
	long rd = days + FIXED_1970, y;
	int m;
	y = (rd - HEB_EPOCH) * 4 / 1461 + 1;
	while (y > 1 && heb_new_year(y) > rd)
		y--;
	while (heb_new_year(y + 1) <= rd)
		y++;
	m = rd < fixed_from_hebrew(y, 1, 1) ? 7 : 1;
	while (rd > fixed_from_hebrew(y, m, hebrew_month_days(y, m)))
		m++;
	h->year = y;
	h->mon = (short)m;
	h->day = (short)(rd - fixed_from_hebrew(y, m, 1) + 1);
}

/* Hebrew letters in the Atari character set, by a one-letter spelling:
   a b g d h v z x(het) T(tet) y k l m n s e(ayin) p c(tsadi) q r S(shin)
   t, and the final forms N K M P C */
static void heb(const char *spell, char *out, int size)
{
	static const char from[] = "abgdhvzxTyklmnsepcqrStNKMPC";
	static const unsigned char to[] = {
		0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE,
		0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC
	};
	int o = 0;
	for (; *spell && o < size - 1; spell++) {
		const char *p = strchr(from, *spell);
		out[o++] = p ? (char)to[p - from] : *spell;
	}
	out[o] = 0;
}

void hebrew_numeral(long n, char *out, int size)
{
	static const char ones[] = "abgdhvzxT", tens[] = "yklmnsepc", hund[] = "qrSt";
	char sp[16];
	int k = 0;
	n %= 1000;
	while (n >= 400 && k < 8) {
		sp[k++] = 't';
		n -= 400;
	}
	if (n >= 100) {
		sp[k++] = hund[n / 100 - 1];
		n %= 100;
	}
	if (n == 15 || n == 16) {	/* not yod-he / yod-vav: spelled tet-vav, tet-zayin */
		sp[k++] = 'T';
		sp[k++] = n == 15 ? 'v' : 'z';
		n = 0;
	}
	if (n >= 10) {
		sp[k++] = tens[n / 10 - 1];
		n %= 10;
	}
	if (n)
		sp[k++] = ones[n - 1];
	if (k == 1) {
		sp[k++] = '\'';		/* geresh after a single letter */
	} else if (k > 1) {
		sp[k] = sp[k - 1];	/* gershayim before the last letter */
		sp[k - 1] = '"';
		k++;
	}
	sp[k] = 0;
	heb(sp, out, size);
}

void hebrew_format(const HDATE *h, int with_year, char *out, int size)
{
	static const char *const mon[14] = {
		"", "nysN", "ayyr", "syvN", "tmvz", "ab", "alvl", "tSry", "xSvN", "kslv",
		"Tbt", "Sbt", "adr", "adr b'"
	};
	char d[16], m[24], y[16];
	hebrew_numeral(h->day, d, sizeof(d));
	if (h->mon == 12 && hebrew_leap(h->year))
		heb("adr a'", m, sizeof(m));
	else
		heb(mon[h->mon], m, sizeof(m));
	if (with_year) {
		hebrew_numeral(h->year, y, sizeof(y));
		snprintf(out, size, "%s %s %s", d, m, y);
	} else {
		snprintf(out, size, "%s %s", d, m);
	}
}
