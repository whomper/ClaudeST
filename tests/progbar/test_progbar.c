/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The family's progress bar (ui/progbar.c, the guidelines 5.9) against a
 * stand-in VDI: the pens it picks in each kind of screen, and what it
 * draws, whole and in pieces.
 *   - 256 colours, EWeb's palette (a 6x6x6 cube and greys over pens 16
 *     and up): 16 steps of blue, dark to light, none the ground's;
 *   - 256 colours, a palette of other colours: still 16 pens, none the
 *     ground's;
 *   - 16 colours, TOS's and a TT's: pens 1 to 15 in order, less the
 *     ground's (white, light grey);
 *   - mono, 4 colours, true colour: black alone;
 *   - a bar drawn in any pieces is the bar drawn whole, pixel for pixel,
 *     each pixel drawn once.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gemui.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h;
short gl_wchar, gl_hchar;
#include "progbar.h"

short planes;
static short pal[256][3];
static short cur_color, row[2048], drawn[2048];
static int fails, checks;

void vq_color(short h, short index, short *rgb)
{
	(void) h;
	memcpy(rgb, pal[index & 255], sizeof pal[0]);
}
void vsf_interior(short h, short s) { (void) h; (void) s; }
void vsf_perimeter(short h, short s) { (void) h; (void) s; }
void vswr_mode(short h, short s) { (void) h; (void) s; }
void vsf_color(short h, short c) { (void) h; cur_color = c; }
void v_bar(short h, const short *pxy)
{
	short x;
	(void) h;
	for (x = pxy[0]; x <= pxy[2]; x++) {
		row[x] = cur_color;
		drawn[x]++;
	}
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static void set(int p, int r, int g, int b)
{
	pal[p][0] = (short) r;
	pal[p][1] = (short) g;
	pal[p][2] = (short) b;
}

/* TOS's 16 (a Falcon's); tt: a TT's, light colours at 9 to 14 */
static void tos16(int tt)
{
	static const short f[16][3] = {
		{1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
		{1000,1000,0},{1000,0,1000},{733,733,733},{533,533,533},{533,0,0},
		{0,533,0},{0,0,533},{0,533,533},{533,533,0},{533,0,533}};
	static const short t[16][3] = {
		{1000,1000,1000},{0,0,0},{1000,0,0},{0,1000,0},{0,0,1000},{0,1000,1000},
		{1000,1000,0},{1000,0,1000},{733,733,733},{1000,600,600},{600,1000,600},
		{600,600,1000},{600,1000,1000},{1000,1000,600},{1000,600,1000},{533,533,533}};
	memcpy(pal, tt ? t : f, sizeof f);
}

/* hue 195 to 250 degrees, blue the strongest, not a grey */
static int blue_hue(short p)
{
	long r = pal[p][0], g = pal[p][1], b = pal[p][2], mn = r < g ? r : g;
	return b >= r && b >= g && b - mn >= 250 && 60 * (r - g) >= -45 * (b - mn) && 60 * (r - g) <= 10 * (b - mn);
}

static long lum(short p)
{
	return 299L * pal[p][0] + 587L * pal[p][1] + 114L * pal[p][2];
}

/* a bar of w pixels: drawn whole, then in pieces; the same pixels, each once */
static void fills(short w, const char *what)
{
	static short whole[2048];
	static const short cuts[] = { 0, 1, 100, 101, 333, 512, 700, 1023, 1024 };
	char msg[120];
	int i, k, ok;
	memset(row, -1, sizeof row);
	memset(drawn, 0, sizeof drawn);
	progbar_fill(0, 0, 0, w, 3, 0, PB_FULL);
	memcpy(whole, row, sizeof row);
	ok = 1;
	for (i = 0; i < w; i++)
		ok &= drawn[i] == 1 && row[i] >= 0;
	snprintf(msg, sizeof msg, "%s: the whole bar, %d pixels, each drawn once", what, w);
	check(ok, msg);
	/* the colours lie left to right in the pens' order */
	ok = 1;
	for (i = 1, k = 0; i < w; i++)
		if (row[i] != row[i - 1]) {
			while (k < progbar_npens() && progbar_pen(k) != row[i - 1])
				k++;
			ok &= k < progbar_npens();
		}
	snprintf(msg, sizeof msg, "%s: the colours in the pens' order along the bar", what);
	check(ok, msg);
	/* in pieces as a load goes on */
	memset(row, -1, sizeof row);
	memset(drawn, 0, sizeof drawn);
	for (k = 1; k < (int) (sizeof cuts / sizeof cuts[0]); k++)
		progbar_fill(0, 0, 0, w, 3, cuts[k - 1], cuts[k]);
	ok = 1;
	for (i = 0; i < w; i++)
		ok &= drawn[i] == 1 && row[i] == whole[i];
	snprintf(msg, sizeof msg, "%s: in pieces, the same pixels, each once", what);
	check(ok, msg);
	/* nothing outside its part */
	memset(drawn, 0, sizeof drawn);
	progbar_fill(0, 0, 0, w, 3, 600, 600);
	progbar_fill(0, 0, 0, w, 3, 700, 650);
	ok = 1;
	for (i = 0; i < w; i++)
		ok &= drawn[i] == 0;
	snprintf(msg, sizeof msg, "%s: an empty part draws nothing", what);
	check(ok, msg);
	progbar_fill(0, 0, 0, w, 3, 0, 512);
	ok = 1;
	for (i = 0; i < w; i++)
		ok &= drawn[i] == (i < w / 2);
	snprintf(msg, sizeof msg, "%s: half the bar is its left half", what);
	check(ok, msg);
}

int main(void)
{
	int i, r, g, b, ok;
	short ground;

	/* 256 colours, EWeb's palette: TOS's 16, a cube, greys */
	planes = 8;
	tos16(0);
	i = 16;
	for (r = 0; r < 6; r++)
		for (g = 0; g < 6; g++)
			for (b = 0; b < 6; b++)
				set(i++, r * 200, g * 200, b * 200);
	for (; i < 256; i++)
		set(i, (i - 232) * 40, (i - 232) * 40, (i - 232) * 40);
	for (ground = 0; ground <= 8; ground += 8) {
		char what[40];
		snprintf(what, sizeof what, "256, cube, ground %d", ground);
		progbar_pens(0, ground);
		check(progbar_npens() >= 8 && progbar_npens() <= PB_SHADES, "256: 8 to 16 steps");
		ok = 1;
		for (i = 0; i < progbar_npens(); i++) {
			short p = progbar_pen(i);
			ok &= p != ground;
			ok &= blue_hue(p);	/* blue, no teal, no purple */
			if (i)
				ok &= lum(p) >= lum(progbar_pen(i - 1));		/* dark to light */
		}
		check(ok, "256: blue, dark to light, never the ground's");
		check(lum(progbar_pen(0)) < 150000L, "256: its left end dark");
		check(lum(progbar_pen(progbar_npens() - 1)) > 600000L, "256: its right end light");
		fills(608, what);
	}
	/* 256 colours, few blues and none light (as TOS 4's): only blues,
	   dark to light, never a grey for the light end */
	for (i = 16; i < 256; i++)
		set(i, (i * 7) % 1000, (i * 7) % 1000, (i * 7) % 1000);	/* greys */
	set(40, 0, 0, 200);
	set(41, 0, 0, 467);
	set(42, 200, 200, 733);
	set(43, 0, 267, 533);
	set(44, 0, 0, 467);		/* the same blue again */
	progbar_pens(0, 8);
	ok = progbar_npens() >= 3 && progbar_npens() <= 7;
	for (i = 0; i < progbar_npens(); i++) {
		short p = progbar_pen(i);
		ok &= blue_hue(p);
		if (i)
			ok &= lum(p) > lum(progbar_pen(i - 1));
	}
	check(ok, "256, few blues: only blues, each once, dark to light");
	fills(608, "256, few blues");

	/* 256 colours, a palette with few blues (whatever is loaded) */
	for (i = 16; i < 256; i++)
		set(i, (i * 37) % 1000, (i * 91) % 1000, (i * 13) % 400);
	progbar_pens(0, 8);
	ok = progbar_npens() >= 1 && progbar_npens() <= PB_SHADES;
	for (i = 0; i < progbar_npens(); i++)
		ok &= progbar_pen(i) >= 0 && progbar_pen(i) < 256 && progbar_pen(i) != 8;
	check(ok, "256, other palette: up to 16 pens, never the ground's");
	fills(400, "256, other palette");

	/* 16 colours */
	planes = 4;
	tos16(0);
	progbar_pens(0, 0);
	ok = progbar_npens() == 15;
	for (i = 0; i < 15; i++)
		ok &= progbar_pen(i) == i + 1;
	check(ok, "16, white ground: pens 1 to 15 in order");
	progbar_pens(0, 8);
	ok = progbar_npens() == 14;
	for (i = 0, r = 1; i < 14; i++, r++) {
		if (r == 8)
			r++;
		ok &= progbar_pen(i) == r;
	}
	check(ok, "16, grey ground: pens 1 to 15 less 8");
	fills(608, "16");
	tos16(1);
	progbar_pens(0, 8);
	ok = progbar_npens() == 14;
	for (i = 0; i < progbar_npens(); i++)
		ok &= progbar_pen(i) != 8 && progbar_pen(i) != 0;
	check(ok, "16, a TT's: 14 pens, not the ground's");
	/* a grey ground the colour of another pen too: that one left out */
	set(15, 733, 733, 733);
	progbar_pens(0, 8);
	ok = progbar_npens() == 13;
	for (i = 0; i < progbar_npens(); i++)
		ok &= progbar_pen(i) != 15;
	check(ok, "16: a pen the ground's colour is left out");

	/* mono, 4 colours, true colour: black */
	tos16(0);
	for (r = 0; r < 4; r++) {
		static const short pl[4] = { 1, 2, 16, 32 };
		char what[40];
		planes = pl[r];
		progbar_pens(0, 0);
		snprintf(what, sizeof what, "%d planes: black alone", planes);
		check(progbar_npens() == 1 && progbar_pen(0) == 1, what);
		snprintf(what, sizeof what, "%d planes", planes);
		fills(640, what);
	}
	/* 4 colours where black isn't pen 1 */
	planes = 2;
	set(1, 1000, 0, 0);
	set(3, 0, 0, 0);
	progbar_pens(0, 0);
	check(progbar_npens() == 1 && progbar_pen(0) == 3, "4 colours: black wherever it is");
	/* mono where both pens read alike: EmuTOS in ST high answers
	   1000,1000,1000 for pens 0 and 1; GEM's black, pen 1, not white */
	planes = 1;
	set(0, 1000, 1000, 1000);
	set(1, 1000, 1000, 1000);
	progbar_pens(0, 0);
	check(progbar_npens() == 1 && progbar_pen(0) == 1, "mono, both pens alike: pen 1");

	printf("progbar: %d checks, %s\n", checks, fails ? "FAILED" : "all passed");
	return fails != 0;
}
