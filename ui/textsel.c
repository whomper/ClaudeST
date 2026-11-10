/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* textsel.c - see textsel.h */
#include <stdlib.h>
#include <string.h>
#include "textsel.h"

/* ---- positions: (line, col) compared in logical order ---- */

static int cmp(long l1, long c1, long l2, long c2)
{
	if (l1 != l2)
		return l1 < l2 ? -1 : 1;
	return c1 < c2 ? -1 : c1 > c2;
}

static const char *text(const TSVIEW *v, long n, long *len)
{
	const char *t;
	long l = -1;
	if (n < 0 || n >= v->nlines)
		return NULL;
	t = v->line(v->ud, n, &l);
	if (!t || l < 0)
		return NULL;
	*len = l;
	return t;
}

static int word_char(const TSVIEW *v, unsigned char c)
{
	if (v->is_word)
		return v->is_word(c);
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

/* 0 blank, 1 word, 2 anything else */
static int cls(const TSVIEW *v, unsigned char c)
{
	if (c == ' ' || c == '\t')
		return 0;
	return word_char(v, c) ? 1 : 2;
}

/* the extent of the unit at (n, col): a word is a run of one class */
static void unit_extent(const TSVIEW *v, int unit, long n, long col, long *c0, long *c1)
{
	long len = 0, a, b;
	const char *t = text(v, n, &len);
	if (!t) {
		*c0 = *c1 = 0;
		return;
	}
	if (unit == 3) {
		*c0 = 0;
		*c1 = len;
		return;
	}
	if (unit == 1 || len == 0) {
		*c0 = *c1 = col > len ? len : col;
		return;
	}
	if (col >= len)
		col = len - 1;
	a = col;
	b = col + 1;
	{
		int k = cls(v, (unsigned char)t[col]);
		while (a > 0 && cls(v, (unsigned char)t[a - 1]) == k)
			a--;
		while (b < len && cls(v, (unsigned char)t[b]) == k)
			b++;
	}
	*c0 = a;
	*c1 = b;
}

/* the chosen range, normalised: start <= end */
static int range(const TSSEL *s, const TSVIEW *v, long *sl, long *sc, long *el, long *ec)
{
	long c0, c1;
	if (!s->on)
		return 0;
	if (cmp(s->hl, s->hc, s->a0l, s->a0c) < 0) {
		unit_extent(v, s->unit, s->hl, s->hc, &c0, &c1);
		*sl = s->hl;
		*sc = c0;
		*el = s->a1l;
		*ec = s->a1c;
	} else if (cmp(s->hl, s->hc, s->a1l, s->a1c) > 0) {
		unit_extent(v, s->unit, s->hl, s->hc, &c0, &c1);
		*sl = s->a0l;
		*sc = s->a0c;
		*el = s->hl;
		*ec = c1;
	} else {
		*sl = s->a0l;
		*sc = s->a0c;
		*el = s->a1l;
		*ec = s->a1c;
	}
	return 1;
}

static void dirty_set(TSSEL *s, long a, long b)
{
	if (a > b) {
		long t = a;
		a = b;
		b = t;
	}
	if (s->d0 > s->d1) {
		s->d0 = a;
		s->d1 = b;
	} else {
		if (a < s->d0)
			s->d0 = a;
		if (b > s->d1)
			s->d1 = b;
	}
}

static void dirty_reset(TSSEL *s)
{
	s->d0 = 1;
	s->d1 = 0;
}

/* where the pointer is: the line clamped to the view, the column of x */
static void where(const TSVIEW *v, long line, long x, long *l, long *c)
{
	long len = 0;
	if (v->nlines <= 0) {
		*l = *c = 0;
		return;
	}
	if (line < 0) {
		*l = 0;
		*c = 0;
		return;
	}
	if (line >= v->nlines) {
		*l = v->nlines - 1;
		if (text(v, *l, &len))
			*c = len;
		else
			*c = 0;
		return;
	}
	*l = line;
	if (!text(v, line, &len)) {
		*c = 0;
		return;
	}
	*c = v->col_at(v->ud, line, x);
	if (*c < 0)
		*c = 0;
	if (*c > len)
		*c = len;
}

/* ---- the calls ---- */

void ts_clear(TSSEL *s)
{
	long f = 1, l = 0;
	if (s->on)
		ts_dirty(s, &f, &l);
	memset(s, 0, sizeof(*s));
	dirty_reset(s);
}

void ts_sync(TSSEL *s, long gen)
{
	if (s->gen != gen) {
		memset(s, 0, sizeof(*s));
		s->gen = gen;
		dirty_reset(s);
	}
}

int ts_press(TSSEL *s, const TSVIEW *v, long line, long x, int clicks)
{
	long l, c, c0, c1, len, ol0 = 0, ol1 = -1;
	if (s->on) {
		long a, b, d, e;
		if (range(s, v, &a, &b, &d, &e))
			ol0 = a, ol1 = d;
	}
	where(v, line, x, &l, &c);
	if (l < 0 || l >= v->nlines || !text(v, l, &len))
		return 0;
	s->unit = clicks >= 3 ? 3 : clicks == 2 ? 2 : 1;
	unit_extent(v, s->unit, l, c, &c0, &c1);
	s->a0l = s->a1l = l;
	s->a0c = c0;
	s->a1c = c1;
	s->hl = l;
	s->hc = c;
	s->on = 1;
	dirty_reset(s);
	dirty_set(s, l, l);
	if (ol1 >= ol0)
		dirty_set(s, ol0, ol1);
	return 1;
}

int ts_extend(TSSEL *s, const TSVIEW *v, long line, long x)
{
	long l, c, a, b, d, e, nl0, nl1;
	if (!s->on || !range(s, v, &a, &b, &d, &e))
		return ts_press(s, v, line, x, 1);
	where(v, line, x, &l, &c);
	/* the anchor becomes the end of the chosen text farther from the pointer */
	if (cmp(l, c, a, b) < 0) {
		s->a0l = s->a1l = d;
		s->a0c = s->a1c = e;
	} else {
		s->a0l = s->a1l = a;
		s->a0c = s->a1c = b;
	}
	s->unit = 1;
	s->hl = l;
	s->hc = c;
	s->on = 1;
	nl0 = l < a ? l : a;
	nl1 = l > d ? l : d;
	dirty_reset(s);
	dirty_set(s, nl0, nl1);
	return 1;
}

int ts_drag(TSSEL *s, const TSVIEW *v, long line, long x)
{
	long l, c, a, b, d, e, a2, b2, d2, e2;
	if (!s->on)
		return 0;
	where(v, line, x, &l, &c);
	if (l == s->hl && c == s->hc)
		return 0;
	range(s, v, &a, &b, &d, &e);
	s->hl = l;
	s->hc = c;
	range(s, v, &a2, &b2, &d2, &e2);
	dirty_reset(s);
	/* the lines whose chosen part changed: where each end of the range moved */
	if (a != a2 || b != b2)
		dirty_set(s, a, a2);
	if (d != d2 || e != e2)
		dirty_set(s, d, d2);
	return 1;
}

void ts_release(TSSEL *s)
{
	(void)s;
}

int ts_has(const TSSEL *s, const TSVIEW *v)
{
	long a, b, d, e;
	if (!range(s, v, &a, &b, &d, &e))
		return 0;
	return cmp(a, b, d, e) < 0;
}

int ts_dirty(const TSSEL *s, long *first, long *last)
{
	if (s->d0 > s->d1)
		return 0;
	*first = s->d0;
	*last = s->d1;
	return 1;
}

int ts_cols(const TSSEL *s, const TSVIEW *v, long n, long *c0, long *c1)
{
	long a, b, d, e, len = 0;
	if (!range(s, v, &a, &b, &d, &e) || cmp(a, b, d, e) >= 0 || n < a || n > d || !text(v, n, &len))
		return 0;
	*c0 = n == a ? b : 0;
	*c1 = n == d ? e : len;
	if (*c1 > len)
		*c1 = len;
	if (*c0 > *c1)
		*c0 = *c1;
	return *c0 < *c1;
}

int ts_rects(const TSSEL *s, const TSVIEW *v, long n, TSRUN *out, int max)
{
	long c0, c1;
	if (max < 1 || !ts_cols(s, v, n, &c0, &c1))
		return 0;
	if (v->runs)
		return v->runs(v->ud, n, c0, c1, out, max);
	out[0].x0 = v->x_of(v->ud, n, c0);
	out[0].x1 = v->x_of(v->ud, n, c1);
	if (out[0].x0 > out[0].x1) {
		long t = out[0].x0;
		out[0].x0 = out[0].x1;
		out[0].x1 = t;
	}
	return out[0].x0 < out[0].x1;
}

int ts_inside(const TSSEL *s, const TSVIEW *v, long line, long x)
{
	TSRUN r[8];
	int i, k;
	k = ts_rects(s, v, line, r, 8);
	for (i = 0; i < k; i++)
		if (x >= r[i].x0 && x < r[i].x1)
			return 1;
	return 0;
}

char *ts_text(const TSSEL *s, const TSVIEW *v, long *len)
{
	long a, b, d, e, n, total = 0, pos = 0;
	char *out;
	int pass;
	if (len)
		*len = 0;
	if (!range(s, v, &a, &b, &d, &e) || cmp(a, b, d, e) >= 0)
		return NULL;
	out = NULL;
	for (pass = 0; pass < 2; pass++) {
		int any = 0;
		pos = 0;
		for (n = a; n <= d; n++) {
			long c0, c1, k, ln = 0;
			const char *t = text(v, n, &ln);
			if (!t)
				continue;
			c0 = n == a ? b : 0;
			c1 = n == d ? e : ln;
			if (c1 > ln)
				c1 = ln;
			if (c0 > c1)
				c0 = c1;
			k = c1 - c0;
			if (any) {		/* the join before this line */
				if (pass)
					out[pos] = v->wrapped && v->wrapped(v->ud, n) ? ' ' : '\n';
				pos++;
			}
			if (pass)
				memcpy(out + pos, t + c0, k);
			pos += k;
			any = 1;
		}
		if (!pass) {
			total = pos;
			out = malloc(total + 1);
			if (!out)
				return NULL;
		}
	}
	out[total] = 0;
	if (len)
		*len = total;
	return out;
}

int ts_autoscroll(long y, long y0, long y1, long ch)
{
	long d;
	if (ch < 1)
		ch = 1;
	if (y < y0) {
		d = (y0 - y) / ch + 1;
		return (int)-(d > 4 ? 4 : d);
	}
	if (y >= y1) {
		d = (y - y1) / ch + 1;
		return (int)(d > 4 ? 4 : d);
	}
	return 0;
}
