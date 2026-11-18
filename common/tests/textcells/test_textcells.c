/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_textcells.c - ui/textcells.c on the host: each mode's VDI calls
 * for a row of Latin and Hebrew (TOS's codes 0xC2-0xDC), against what
 * EMail's draw.c makes (the hub, 17 Nov 2026).
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "text.h"
#include "../../ui/textcells.h"

static char log_[4096];
static short ext, widths_differ;

static void add(const char *t)
{
	strncat(log_, t, sizeof log_ - strlen(log_) - 1);
}

short vqt_extent_n(short h, const char *s, short n) { (void)h; (void)s; (void)n; return ext; }
short vqt_width(short h, short c) { (void)h; return (short)(widths_differ && c == 'i' ? 5 : 9); }
void v_justified_n(short h, short x, short y, const char *s, short n, short len, short ws, short cs)
{
	char t[96];
	(void)h; (void)ws;
	snprintf(t, sizeof t, "J%d,%d:%.*s/%d/%d ", x, y, n, s, len, cs);
	add(t);
}
void v_gtext_n(short h, short x, short y, const char *s, short n)
{
	char t[96];
	(void)h;
	snprintf(t, sizeof t, "G%d,%d:", x, y);
	add(t);
	for (; n > 0; n--, s++) {
		snprintf(t, sizeof t, (unsigned char)*s >= 0x80 ? "<%02X>" : "%c", (unsigned char)*s);
		add(t);
	}
	add(" ");
}
void v_gtext_w(short h, short x, short y, const unsigned short *s, short n)
{
	char t[64];
	(void)h;
	snprintf(t, sizeof t, "W%d,%d:%u/%d ", x, y, s[0], n);
	add(t);
}
short vst_map_mode(short h, short mode) { char t[16]; (void)h; snprintf(t, sizeof t, "M%d ", mode); add(t); return mode; }
short vst_font(short h, short id) { char t[16]; (void)h; snprintf(t, sizeof t, "F%d ", id); add(t); return id; }
void vst_height(short h, short height, short *cw, short *ch) { char t[16]; (void)h; snprintf(t, sizeof t, "H%d ", height); add(t); *cw = 8; *ch = 16; }
short vst_point(short h, short point, short *cw, short *ch) { char t[16]; (void)h; snprintf(t, sizeof t, "P%d ", point); add(t); *cw = 9; *ch = 17; return point; }
void text_n(short h, short x, short y, const char *s, long n)
{
	char t[96];
	(void)h;
	snprintf(t, sizeof t, "T%d,%d:%.*s ", x, y, (int)n, s);
	add(t);
}

static int bad;

static void expect(const char *what, const char *want)
{
	if (strcmp(log_, want)) {
		printf("FAIL %s\n  got  %s\n  want %s\n", what, log_, want);
		bad = 1;
	} else
		printf("ok   %s\n", what);
	log_[0] = 0;
}

int main(void)
{
	/* "ab " then alef bet (0xC2 0xC3) then " c": a row in display order */
	static const char row[] = "ab \xC2\xC3 c";
	unsigned short idx[27];
	short k;

	for (k = 0; k < 27; k++)
		idx[k] = (unsigned short)(1000 + k);

	/* the system font: text_n, the codes as they are */
	text_hebrew(0, NULL, 1, 16);
	text_cells(text_cells_probe(1, 1, 8), 8, 16);
	text_cells_n(1, 10, 20, row, 7);
	expect("system font: one run each, the codes as they are", "T10,20:ab  T34,20:\xC2\xC3 T50,20: c ");

	/* a vector font whose runs keep their cells, its Hebrew by index */
	ext = (short)(32 * 9);
	text_hebrew(1, idx, 5, 14);
	text_cells(text_cells_probe(1, 5, 9), 9, 17);
	text_cells_n(1, 10, 20, row, 7);
	expect("FS_HEB_UNICODE, runs: the letters by the font's index in their cells",
	       "T10,20:ab  M0 W37,20:1000/1 W46,20:1001/1 M1 T55,20: c ");

	/* monospaced within a pixel a cell: runs made to fit */
	ext = (short)(32 * 9 - 10);
	text_cells(text_cells_probe(1, 5, 9), 9, 17);
	text_cells_n(1, 10, 20, "abc", 3);
	expect("TEXT_JUSTIFY: a run made to fill its cells", "J10,20:abc/27/1 ");

	/* proportional: a character a cell, spaces left out */
	widths_differ = 1;
	text_cells(text_cells_probe(1, 5, 9), 9, 17);
	text_cells_n(1, 10, 20, "a b", 3);
	expect("TEXT_CELLS: a character a cell, no spaces", "G10,20:a G28,20:b ");

	/* a vector font without Hebrew: the system font's letters, centred,
	   the font set again after */
	widths_differ = 0;
	ext = (short)(32 * 9);
	text_hebrew(2, NULL, 5, 14);
	text_cells(text_cells_probe(1, 5, 9), 9, 17);
	text_cells_n(1, 10, 20, "\xC2" "a", 2);
	expect("FS_HEB_SYSTEM: the system font's letter centred, the font again",
	       "F1 H13 G10,20:<C2> F5 P14 T19,20:a ");

	/* the last letter (final tsadi, 0xDC): its index */
	text_hebrew(1, idx, 5, 14);
	text_cells_n(1, 0, 0, "\xDC", 1);
	expect("FS_HEB_UNICODE: 0xDC is the 27th letter", "M0 W0,0:1026/1 M1 ");
	return bad;
}
