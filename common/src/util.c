/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * util.c - growable strings and small text helpers.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "util.h"
#include "plat.h"

void sb_init(SBUF *b)
{
	b->s = 0;
	b->len = b->cap = 0;
}

void sb_free(SBUF *b)
{
	free(b->s);
	sb_init(b);
}

void sb_reset(SBUF *b)
{
	b->len = 0;
	if (b->s)
		b->s[0] = 0;
}

/* a size from the network can be anything: none that would overflow */
#define SB_MAX 0x3FFFFFFFL

static int grow(SBUF *b, long need)
{
	long cap;
	char *n;
	if (need < 0 || need > SB_MAX - b->len)
		return 0;
	if (b->len + need + 1 <= b->cap)
		return 1;
	cap = b->cap ? b->cap : 256;
	while (cap < b->len + need + 1)
		cap = cap > SB_MAX / 2 ? b->len + need + 1 : cap * 2;
	n = realloc(b->s, cap);
	if (!n)
		return 0;
	b->s = n;
	b->cap = cap;
	return 1;
}

int sb_add(SBUF *b, const char *s, long n)
{
	if (!grow(b, n))
		return 0;
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = 0;
	return 1;
}

int sb_room(SBUF *b, long n) { return grow(b, n); }

/* room for exactly n bytes more (an answer of known length): no doubling,
   so a 1 MB answer needs 1 MB, not up to 2 MB and the old copy while it
   moves */
int sb_reserve(SBUF *b, long n)
{
	char *p;
	if (n < 0 || n > SB_MAX - b->len)
		return 0;
	if (b->len + n + 1 <= b->cap)
		return 1;
	p = realloc(b->s, b->len + n + 1);
	if (!p)
		return 0;
	b->s = p;
	b->cap = b->len + n + 1;
	return 1;
}
int sb_adds(SBUF *b, const char *s) { return sb_add(b, s, strlen(s)); }
int sb_addc(SBUF *b, char c) { return sb_add(b, &c, 1); }

int sb_printf(SBUF *b, const char *fmt, ...)
{
	va_list ap;
	char tmp[512];
	int n;
	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n >= (int)sizeof(tmp))
		n = sizeof(tmp) - 1;
	return sb_add(b, tmp, n);
}

char *sb_steal(SBUF *b)
{
	char *s = b->s;
	if (!s)
		s = calloc(1, 1);
	sb_init(b);
	return s;
}

char *str_ndup(const char *s, long n)
{
	char *d = malloc(n + 1);
	if (d) {
		memcpy(d, s, n);
		d[n] = 0;
	}
	return d;
}

char *str_trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
		*--e = 0;
	return s;
}

int str_istarts(const char *s, const char *p)
{
	return strncasecmp(s, p, strlen(p)) == 0;
}

/* the needle's first letter, either case, is looked for before the rest is
   compared: strncasecmp at every place took EContact's search 14,700 calls
   a letter typed (the guidelines' Speed, 26 Oct 2026). tests/istr checks it
   finds what the plain way finds. */
const char *str_istr(const char *h, const char *n)
{
	long l = strlen(n);
	char lo, up;
	if (!l)
		return *h ? h : 0;
	lo = (char)tolower((unsigned char)*n);
	up = (char)toupper((unsigned char)*n);
	for (; *h; h++)
		if ((*h == lo || *h == up) && !strncasecmp(h, n, l))
			return h;
	return 0;
}

void str_copy(char *d, const char *s, long size)
{
	if (size <= 0)
		return;
	while (size > 1 && *s) {
		*d++ = *s++;
		size--;
	}
	*d = 0;
}

void path_join(char *out, long size, const char *dir, const char *name)
{
	long n;
	str_copy(out, dir, size);
	n = strlen(out);
	if (n && out[n - 1] != PF_SEP && n + 1 < size) {
		out[n++] = PF_SEP;
		out[n] = 0;
	}
	str_copy(out + n, name, size - n);
}

unsigned long str_hash(const char *s)
{
	unsigned long h = 5381;
	while (*s)
		h = h * 33 + (unsigned char)*s++;
	return h;
}

const char *num(long n)
{
	static char bufs[8][16];
	static int k;
	char *out = bufs[k++ & 7], digits[12];
	unsigned long v = n < 0 ? 0UL - (unsigned long)n : (unsigned long)n;
	int nd = 0, o = 0;
	do {
		digits[nd++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	if (n < 0)
		out[o++] = '-';
	while (nd) {
		out[o++] = digits[--nd];
		if (nd && nd % 3 == 0)
			out[o++] = ',';
	}
	out[o] = 0;
	return out;
}
