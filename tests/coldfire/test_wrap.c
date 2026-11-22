/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_wrap.c - ClaudeST's conversation layout (st/textwrap.h, the word
 * wrap, with common's bidi.c for the Hebrew paragraphs' direction and
 * order) as ColdFire code under qemu-m68k -cpu cfv4e, against the same
 * source built for the host (-DHOST): the FireBee edition's core (the
 * guidelines, 7.4).
 *
 * 3,000 conversations from a fixed seed, each a run of messages as the
 * bridge sends them (M a message, P text in pieces of 1 to 40 bytes, H a
 * bold line, g a grey tool step, c a line of code, B a line break, Z its
 * end), with English words, Hebrew letters (the Atari's 0xC2-0xDC),
 * inline code marks, long words and empty paragraphs, at widths of 8 to
 * 90 columns. After each piece the lines come from wrap_tail, as the
 * program makes them while a reply streams in; at the end of each
 * conversation every line must be what rewrap_all makes from the whole
 * text (checked here too), and every line's kind, role, place and its
 * visual order (bidi_visual) are folded into one sum, which the two
 * builds must print alike. */
#ifdef HOST
#include <stdio.h>
#include <string.h>
#else
#include "rt.h"
#endif
#include "bidi.h"

typedef unsigned char u8;
typedef unsigned int u32;	/* 32 bits on the host and the ColdFire */

/* claudest.c's marks and line kinds, as they are there */
#define MK_HEADER 0x10
#define MK_BOLD   0x11
#define MK_GREY   0x12
#define MK_CODE   0x13
#define MK_INL    0x14

typedef struct {
	long off;
	short len;
	u8 kind;
	u8 role;
} LINE;

#define L_TEXT   0
#define L_HEADER 1
#define L_BOLD   2
#define L_GREY   3
#define L_CODE   4
#define L_TYPE   0x0f
#define L_INL    0x20
#define L_RTL    0x40

#define TCAP 60000L
#define MAXL 4000
static char tb[TCAP];
static long tlen;
static LINE lines[MAXL], whole[MAXL];
static short nlines, maxlines = MAXL;
static short cols;

#include "textwrap.h"

static u32 seed = 12345;
static u32 rnd(void)
{
	seed = seed * 1103515245U + 12345U;
	return (seed >> 8) & 0xffffffU;
}

static u32 sum = 2166136261U;
static long fails;
static void fold(u32 v)
{
	int i;
	for (i = 0; i < 4; i++, v >>= 8)
		sum = (sum ^ (v & 255)) * 16777619U;
}

/* claudest.c's append, its layout part */
static void append(const char *s, short n)
{
	if (tlen + n + 4 >= TCAP)
		return;
	memcpy(tb + tlen, s, n);
	tlen += n;
	wrap_tail();
}

static void end_para(void)
{
	if (tlen > 0 && tb[tlen - 1] != '\n')
		append("\n", 1);
}

/* a paragraph's text: words of English or Hebrew, inline code, a long word */
static short gen_text(char *t, short cap)
{
	short n = 0, words = (short)(1 + rnd() % 30), heb = (rnd() % 4) == 0, w;
	while (words-- > 0 && n < cap - 50) {
		short len = (short)(1 + rnd() % 9);
		if (rnd() % 25 == 0)
			len = (short)(20 + rnd() % 80);	/* longer than a line */
		if (rnd() % 12 == 0)
			t[n++] = MK_INL;
		for (w = 0; w < len && n < cap - 2; w++)
			t[n++] = (char)(heb || rnd() % 10 == 0 ? 0xC2 + rnd() % 27 : 'a' + rnd() % 26);
		if (rnd() % 7 == 0)
			t[n++] = (char)(rnd() % 2 ? '.' : ',');
		t[n++] = ' ';
	}
	if (n && rnd() % 3)
		n--;	/* most end on a letter */
	return n;
}

/* the text in pieces, as P lines come while a reply streams */
static void pieces(const char *t, short n)
{
	while (n > 0) {
		short k = (short)(1 + rnd() % 40);
		if (k > n)
			k = n;
		append(t, k);
		t += k;
		n -= k;
	}
}

static void conversation(void)
{
	char t[600];
	short msgs = (short)(1 + rnd() % 5), i, n;
	tlen = 0;
	nlines = 0;
	cols = (short)(8 + rnd() % 83);
	while (msgs-- > 0) {
		char role = (char)("UA"[rnd() % 2]);
		char hdr[3];
		short paras = (short)(1 + rnd() % 6);
		if (tlen > 0) {	/* begin_message */
			end_para();
			append("\n", 1);
		}
		hdr[0] = MK_HEADER;
		hdr[1] = role;
		hdr[2] = '\n';
		append(hdr, 3);
		while (paras-- > 0) {
			u32 k = rnd() % 10;
			n = gen_text(t, sizeof(t));
			if (k < 6) {			/* P ... B */
				pieces(t, n);
				append("\n", 1);
			} else {			/* H, g, c: a marked line */
				char m = k == 6 ? MK_BOLD : k == 7 ? MK_GREY : MK_CODE;
				end_para();
				append(&m, 1);
				pieces(t, n);
				append("\n", 1);
			}
			if (rnd() % 8 == 0)
				append("\n", 1);	/* an empty paragraph */
		}
		end_para();		/* Z */
	}
	/* as streamed against all at once */
	n = nlines;
	memcpy(whole, lines, sizeof(LINE) * n);
	rewrap_all();
	if (nlines != n || memcmp(whole, lines, sizeof(LINE) * n))
		fails++;
	for (i = 0; i < nlines; i++) {
		LINE *l = &lines[i];
		char vis[BIDI_MAX];
		short j, len = l->len > BIDI_MAX ? BIDI_MAX : l->len;
		fold((u32)l->off);
		fold((u32)l->len << 16 | (u32)l->kind << 8 | l->role);
		bidi_visual(tb + l->off, len, (l->kind & L_RTL) != 0, vis);
		for (j = 0; j < len; j++)
			sum = (sum ^ (u8)vis[j]) * 16777619U;
	}
}

int main(void)
{
	long c;
	unsigned long lines_all = 0;
	for (c = 0; c < 3000; c++) {
		conversation();
		lines_all += (unsigned long)nlines;
	}
#ifdef HOST
	printf("wrap: 3000 conversations, %lu lines, sum %08x, %ld streamed unlike whole\n", lines_all, sum, fails);
#else
	out("wrap: 3000 conversations, ");
	out_num(lines_all);
	out(" lines, sum ");
	out_hex((unsigned long)sum);
	out(", ");
	out_num((unsigned long)fails);
	out(" streamed unlike whole\n");
#endif
	return fails ? 1 : 0;
}
