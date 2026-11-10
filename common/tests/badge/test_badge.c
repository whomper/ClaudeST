/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_badge.c - badge_form (ui/badge.c) on the build machine: the box a
 * pixel round the text, 3 left and 2 right, each glyph where it goes from
 * a font laid out as TOS's (glyphs side by side in rows of fw bytes), the
 * characters the font hasn't left blank, the limits, and the box alone
 * with no font or another height. With AddressSanitizer: make test
 */
#include <stdio.h>
#include <string.h>
#include "badge.h"

static int fails;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static int px(const BADGE *b, int x, int y)
{
	return (b->bits[y * BADGE_WPL + (x >> 4)] >> (15 - (x & 15))) & 1;
}

/* a 6 x 6 font of '0'..'9' and 'A'..'Z' (first 48, last 90): each glyph
   5 columns wide (its 6th, the right, blank), the glyph's own number in
   its first row as bits, a pixel at its column 4 on its last row */
#define FIRST 48
#define LAST 90
#define NG (LAST - FIRST + 1)
#define FW ((NG * 6 + 7) / 8)
static unsigned char dat[FW * 6];
static short off[NG + 1];

static void fpx(int x, int y)
{
	dat[y * FW + x / 8] |= (unsigned char)(0x80 >> (x & 7));
}

static int glyph_px(int g, int x, int y)
{
	if (x >= 5)
		return 0;
	if (y == 0)
		return (g >> x) & 1;
	return y == 5 && x == 4;
}

int main(void)
{
	static BADGE b;
	int g, x, y, bad;
	for (g = 0; g <= NG; g++)
		off[g] = (short)(g * 6);
	for (g = 0; g < NG; g++)
		for (y = 0; y < 6; y++)
			for (x = 0; x < 6; x++)
				if (glyph_px(g, x, y))
					fpx(g * 6 + x, y);

	check(badge_form(&b, "44.1 KHZ", 6, 6, FIRST, LAST, off, dat, FW, 6) == 1 && b.glyphs,
	      "made with its glyphs");
	check(b.w == 8 * 6 + 5 && b.h == 10 && !strcmp(b.s, "44.1 KHZ"), "the box 53 x 10, its text kept");
	bad = 0;
	for (x = 0; x < b.w; x++)
		bad += !px(&b, x, 0) || !px(&b, x, b.h - 1);
	for (y = 0; y < b.h; y++)
		bad += !px(&b, 0, y) || !px(&b, b.w - 1, y);
	for (x = b.w; x < BADGE_WPL * 16; x++)
		for (y = 0; y < BADGE_ROWS; y++)
			bad += px(&b, x, y);
	for (y = b.h; y < BADGE_ROWS; y++)
		for (x = 0; x < BADGE_WPL * 16; x++)
			bad += px(&b, x, y);
	check(!bad, "the box's lines, nothing outside it");
	bad = 0;
	{
		const char *s = "44.1 KHZ";
		int i;
		for (i = 0; s[i]; i++) {
			int c = (unsigned char)s[i];
			for (y = 0; y < 6; y++)
				for (x = 0; x < 6; x++) {
					int want = c >= FIRST && c <= LAST ? glyph_px(c - FIRST, x, y) : 0;
					bad += px(&b, 3 + i * 6 + x, 2 + y) != want;
				}
		}
		/* the clear pixel round the text: row 1, the last but one, columns 1-2, w-2 */
		for (x = 1; x < b.w - 1; x++)
			bad += px(&b, x, 1) || px(&b, x, b.h - 2);
		for (y = 1; y < b.h - 1; y++)
			bad += px(&b, 1, y) || px(&b, 2, y) || px(&b, b.w - 2, y);
	}
	check(!bad, "each glyph in place, '.' and ' ' (not in the font) blank, a pixel clear all round");

	check(badge_form(&b, "123456789012345678901", 6, 6, FIRST, LAST, off, dat, FW, 6) == 1 && b.w == 131,
	      "21 characters fit (131 wide)");
	check(!badge_form(&b, "1234567890123456789012", 6, 6, FIRST, LAST, off, dat, FW, 6) && !b.w,
	      "22 don't: not made");
	check(!badge_form(&b, "", 6, 6, FIRST, LAST, off, dat, FW, 6) && !b.w, "empty: not made");
	check(!badge_form(&b, "DSP", 6, 6, 0, 0, 0, 0, 0, 0) && b.w == 23 && !b.glyphs && px(&b, 0, 0) &&
	      !px(&b, 3, 2), "no font: the box alone, for the VDI's text");
	check(!badge_form(&b, "DSP", 6, 7, FIRST, LAST, off, dat, FW, 6) && b.w == 23 && b.h == 11 && !b.glyphs,
	      "a font of another height: the box alone");
	check(!badge_form(&b, "DSP", 6, 9, FIRST, LAST, off, dat, FW, 6) && !b.w, "a cell over 8 high: not made");
	check(!badge_form(&b, "123456789012345678901", 7, 6, FIRST, LAST, off, dat, FW, 6) && !b.w,
	      "wider than the form: not made");
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
