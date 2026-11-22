/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* textwrap.h - the conversation's word wrap, included by claudest.c where
 * the text buffer's code is (the same code, in the same place), and by
 * tests/coldfire's test_wrap.c, which runs it as ColdFire code under QEMU
 * against the host's (the guidelines, 7.4: the FireBee edition's core).
 * Its world is claudest.c's: tb, tlen, lines, nlines, maxlines, cols, the
 * MK_ and L_ marks, bidi_is_rtl. */

static void add_line(long off, short len, u8 kind, u8 role)
{
	if (nlines >= maxlines)
		return;
	lines[nlines].off = off;
	lines[nlines].len = len;
	lines[nlines].kind = kind;
	lines[nlines].role = role;
	nlines++;
}

static short wrap_width(u8 role)
{
	short w = role == 'U' ? cols - 3 : cols;
	return w < 8 ? 8 : w;
}

/* wrap one paragraph's text from s to e (no newline in it), its lines of
   the given kind: L_INL says the first line starts inside inline code */
static void wrap_para(long s, long e, u8 kind, u8 role)
{
	const char *t = tb, *q = t + s, *end = t + e;
	short w = wrap_width(role);

	while (q < end) {
		const char *m = q, *b;
		short k = 0;
		/* w characters on, not counting inline code marks */
		while (m < end && (k < w || *m == MK_INL)) {
			if (*m != MK_INL)
				k++;
			m++;
		}
		if (m >= end) {
			add_line(q - t, (short)(end - q), kind, role);
			break;
		}
		for (b = m; b > q && *b != ' '; b--)
			;
		if (b == q)
			b = m;
		add_line(q - t, (short)(b - q), kind, role);
		for (m = q; m < b; m++)	/* inline code going on */
			if (*m == MK_INL)
				kind ^= L_INL;
		q = *b == ' ' ? b + 1 : b;	/* a space breaks the line */
	}
}

static long para_end(long p)
{
	const char *t = tb + p, *end = tb + tlen;
	while (t < end && *t != '\n')
		t++;
	return t - tb;
}

static void wrap_from(long p, u8 role)
{
	long e, s;
	u8 kind;

	while (p < tlen) {
		e = para_end(p);
		if (tb[p] == MK_HEADER && e - p >= 2) {
			role = (u8)tb[p + 1];
			add_line(p, 0, L_HEADER, role);
		} else if (e == p) {
			add_line(p, 0, L_TEXT, role);
		} else {
			kind = L_TEXT;
			s = p;
			if (tb[s] == MK_BOLD) {
				kind = L_BOLD;
				s++;
			} else if (tb[s] == MK_GREY) {
				kind = L_GREY;
				s++;
			} else if (tb[s] == MK_CODE) {
				kind = L_CODE;
				s++;
			}
			/* the paragraph's direction comes from its first strong letter */
			if (bidi_is_rtl(tb + s, e - s > 2000 ? 2000 : (short)(e - s)))
				kind |= L_RTL;
			wrap_para(s, e, kind, role);
		}
		if (e >= tlen)
			break;
		p = e + 1;
	}
}

static short is_para_start(long off)
{
	return off == 0 || tb[off - 1] == '\n' || tb[off - 1] == MK_BOLD || tb[off - 1] == MK_GREY ||
	       tb[off - 1] == MK_CODE;
}

/* rewrap after text was appended; returns the first changed line. Only
   the paragraph's last line can change: the breaks before it were made
   with all their text there, and text added at the end can't move them.
   Unless the paragraph's direction changed (its first strong letter just
   came): then all of it. */
static short wrap_tail(void)
{
	short li = nlines, last = nlines - 1;
	long p = 0;
	u8 role = 0;

	while (li > 0) {
		li--;
		if (is_para_start(lines[li].off))
			break;
	}
	if (nlines) {
		LINE *l = &lines[last];
		p = lines[li].off;
		if (last > li && (l->kind & L_TYPE) != L_HEADER) {
			long e = para_end(l->off), n = e - p;
			if (!bidi_is_rtl(tb + p, n > 2000 ? 2000 : (short)n) == !(l->kind & L_RTL)) {
				nlines = last;
				wrap_para(l->off, e, l->kind, l->role);
				if (e < tlen)
					wrap_from(e + 1, l->role);
				return last;
			}
		}
		if (li < nlines && p > 0 && (((lines[li].kind & L_TYPE) == L_BOLD && tb[p - 1] == MK_BOLD) ||
					     ((lines[li].kind & L_TYPE) == L_GREY && tb[p - 1] == MK_GREY) ||
					     ((lines[li].kind & L_TYPE) == L_CODE && tb[p - 1] == MK_CODE)))
			p--;
		role = li > 0 ? lines[li - 1].role : 0;
	}
	nlines = li;
	wrap_from(p, role);
	return li;
}

static void rewrap_all(void)
{
	nlines = 0;
	wrap_from(0, 0);
}
