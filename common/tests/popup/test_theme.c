/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/theme.c on the host (theme.h): the six base roles and an app's own, in each screen: monochrome and four
 * colours (pen 0 for the panel, pen 1 for the rest, colour off), sixteen (each role the pen nearest, never a
 * fixed number; a state's colour never the dim one), 256 (pens 16 up set and put back, the desktop's sixteen
 * never touched). The stand-ins record the palette calls.
 */
#include <stdio.h>
#include <string.h>
#include "stub.h"
#include "theme.h"

extern short vdi_h, planes;

static int failed, total;
static void check(int ok, const char *what)
{
	total++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

/* the palette: what vq_color gives and vs_color changed */
static short palette[256][3];
static int nset, nget;
static short last_set_pen[64];

void vq_color(short h, short index, short *rgb)
{
	(void)h;
	nget++;
	rgb[0] = palette[index][0];
	rgb[1] = palette[index][1];
	rgb[2] = palette[index][2];
}
void vs_color(short h, short index, const short *rgb)
{
	(void)h;
	if (nset < 64)
		last_set_pen[nset] = index;
	nset++;
	palette[index][0] = rgb[0];
	palette[index][1] = rgb[1];
	palette[index][2] = rgb[2];
}

/* sixteen pens: a stand-in that names the pen by its wanted colour's brightness band, so two close colours
   share a pen like the real one's, and text is never the lightest */
static int dim_pen = 9;
short pen_nearest(short h, short npens, short r, short g, short b, int text)
{
	(void)h, (void)npens, (void)text;
	if (r == 0 && g == 0 && b == 0)
		return 1;
	if (r == 0 && g == 0 && b == 667)
		return 12;
	if (r == 533 && g == 533 && b == 533)
		return dim_pen;
	if (r == 733 && g == 733 && b == 733)
		return 8;
	if (r == 667 && g == 667 && b == 0)
		return 14;
	if (r == 667 && g == 0 && b == 0)
		return 2;
	return 5;
}

static const THEME_ROLE mine[] = {
	{ { 745, 471, 216 }, { 667, 667,   0 }, 1, 1 },	/* P_BASE: a state's colour */
	{ {  55, 486, 525 }, {   0, 667, 667 }, 1, 0 },	/* P_BASE + 1 */
	{ { 863, 149, 149 }, { 667,   0,   0 }, 1, 1 },	/* P_BASE + 2: red */
};

int main(void)
{
	int i, ok;
	short before[3][3];

	/* monochrome and four colours: everything pen 1, the panel pen 0, colour off, nothing set */
	for (planes = 1; planes <= 2; planes++) {
		stub_reset();
		nset = nget = 0;
		theme_init(1, mine, 3);
		for (ok = 1, i = 0; i < P_BASE + 3; i++)
			ok &= pen[i] == (i == P_PANEL ? 0 : 1);
		check(ok && colour == 0 && nset == 0, "mono / 4 colours: the panel is pen 0, the rest pen 1, no palette touched");
	}

	/* 4 colours with an accent role: pen 2 turned to its rgb4 while the app runs, put back at exit */
	{
		static const short blue[3] = { 0, 0, 930 };
		THEME_ROLE acc[1] = { { { 0, 0, 0 }, { 0, 0, 0 }, 0, 0 } };
		planes = 2;
		palette[2][0] = 1000, palette[2][1] = 0, palette[2][2] = 0;
		nset = nget = 0;
		theme_init(1, acc, 1);
		theme_accent4(P_BASE, blue);
		check(colour == 0 && pen[P_BASE] == 2 && pen[P_SEL] == 1 && pen[P_PANEL] == 0, "4 colours: the accent role is pen 2, the rest black and white");
		check(palette[2][0] == 0 && palette[2][1] == 0 && palette[2][2] == 930, "4 colours: pen 2 is turned blue while the app runs");
		theme_exit();
		check(palette[2][0] == 1000 && palette[2][1] == 0 && palette[2][2] == 0, "4 colours: pen 2 put back at exit");
		theme_init(0, acc, 1);
		theme_accent4(P_BASE, blue);
		check(pen[P_BASE] == 1 && palette[2][2] == 0, "4 colours, colour off: no accent, pen 2 untouched");
	}

	/* colour off by the app's option, even on 256 colours */
	planes = 8;
	nset = nget = 0;
	theme_init(0, mine, 3);
	check(colour == 0 && pen[P_SEL] == 1 && pen[P_PANEL] == 0 && nset == 0, "COLOUR=0: black and white, nothing set");

	/* sixteen colours: nearest pens among TOS's own sixteen, read once (not once a role), the one selection
	   colour, the state colours never the dim pen */
	{
		static const short tos16[16][3] = {
			{ 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 }, { 0, 0, 1000 }, { 0, 1000, 1000 },
			{ 1000, 1000, 0 }, { 1000, 0, 1000 }, { 733, 733, 733 }, { 533, 533, 533 }, { 667, 0, 0 },
			{ 0, 667, 0 }, { 0, 0, 667 }, { 0, 667, 667 }, { 667, 667, 0 }, { 667, 0, 667 } };
		short keep14[3];
		memcpy(palette, tos16, sizeof(tos16));
		planes = 4;
		nset = nget = 0;
		theme_init(1, mine, 3);
		check(colour == 1 && nset == 0, "16 colours: colour is on and no palette is set");
		check(nget == 16, "16 colours: the sixteen pens read once");
		check(pen[P_TEXT] == 1 && pen[P_PANEL] == 8 && pen[P_SEL] == 12, "16 colours: text black, panel light grey, selection (0,0,667)");
		check(pen[P_DIM] == 9 && pen[P_BASE] == 14 && pen[P_BASE + 1] == 13 && pen[P_BASE + 2] == 10,
		      "16 colours: the app's roles take their own pens");
		/* a screen where the nearest to the dim grey is also the amber's: the amber must not read as dim */
		memcpy(keep14, palette[14], sizeof(keep14));
		palette[9][0] = 600, palette[9][1] = 600, palette[9][2] = 300;
		palette[14][0] = palette[14][1] = palette[14][2] = 0;
		theme_init(1, mine, 3);
		check(pen[P_BASE] == pen[P_TEXT] && pen[P_BASE + 1] != pen[P_DIM], "16 colours: a not_dim role that fell on P_DIM's pen takes P_TEXT");
		memcpy(palette[14], keep14, sizeof(keep14));
		palette[9][0] = palette[9][1] = palette[9][2] = 533;
	}

	/* 256 colours: pens 16 up, set to the RGB, the desktop's sixteen untouched, put back at exit */
	planes = 8;
	for (i = 0; i < 9; i++) {
		palette[16 + i][0] = (short)(100 + i);
		palette[16 + i][1] = (short)(200 + i);
		palette[16 + i][2] = (short)(300 + i);
	}
	for (i = 0; i < 3; i++)
		memcpy(before[i], palette[16 + i], sizeof(before[i]));
	nset = nget = 0;
	theme_init(1, mine, 3);
	check(colour == 1 && nset == P_BASE + 3 && nget == P_BASE + 3, "256 colours: one pen a role, each read before it is set");
	for (ok = 1, i = 0; i < nset; i++)
		ok &= last_set_pen[i] == 16 + i;
	check(ok, "256 colours: pens 16 up in order, never one of the desktop's sixteen");
	check(pen[P_TEXT] == 16 && pen[P_SEL] == 16 + P_SEL && pen[P_BASE + 2] == 16 + P_BASE + 2, "256 colours: pen[] holds those pens");
	check(palette[16 + P_SEL][0] == 0 && palette[16 + P_SEL][1] == 471 && palette[16 + P_SEL][2] == 851, "256 colours: the selection is (0, 471, 851), the same in every app");
	check(palette[16 + P_BASE][0] == 745 && palette[16 + P_BASE + 1][1] == 486, "256 colours: the app's own shades follow the base");
	nset = 0;
	theme_exit();
	check(nset == P_BASE + 3 && memcmp(before[0], palette[16], sizeof(before[0])) == 0 &&
	      memcmp(before[2], palette[18], sizeof(before[2])) == 0, "theme_exit puts the old palette back");
	nset = 0;
	theme_exit();
	check(nset == 0, "a second theme_exit does nothing");

	/* theme_init again puts the old palette back first, and does not stack saves (a changed option or mode) */
	theme_init(1, mine, 3);
	theme_init(1, mine, 3);
	nset = 0;
	theme_exit();
	check(memcmp(before[1], palette[17], sizeof(before[1])) == 0, "init twice, exit once: the original palette is back, not the first theme's");

	/* the base alone, and too many roles are cut */
	theme_init(1, 0, 0);
	check(pen[P_SEL] == 16 + P_SEL, "no roles of its own: the six alone");
	theme_exit();
	{
		THEME_ROLE many[20];
		for (i = 0; i < 20; i++)
			many[i] = mine[0];
		nset = 0;
		theme_init(1, many, 20);
		check(nset == THEME_MAX, "more roles than there are pens are cut at THEME_MAX");
		theme_exit();
	}

	/* theme_keep (EHypView, 22 Nov 2026): in 256 colours the roles take the nearest of the screen's own pens,
	   none set; text roles not the light ones while a dark one is left */
	planes = 8;
	for (i = 0; i < 256; i++) {
		palette[i][0] = (short)(i * 1000 / 255);
		palette[i][1] = (short)(i * 1000 / 255);
		palette[i][2] = (short)(i * 1000 / 255);
	}
	palette[200][0] = 0, palette[200][1] = 470, palette[200][2] = 850;	/* a blue among the greys */
	theme_keep = 1;
	nset = nget = 0;
	theme_init(1, mine, 3);
	check(nset == 0 && nget == 256, "theme_keep: the palette read once, no pen set");
	check(pen[P_SEL] == 200, "theme_keep: the selection takes the nearest blue");
	check(pen[P_PANEL] >= 230 && pen[P_TEXT] <= 40, "theme_keep: panel light, text dark, from the greys");
	theme_exit();
	check(nset == 0, "theme_keep: theme_exit puts nothing back");
	planes = 4;
	nset = 0;
	theme_init(1, mine, 3);
	check(nset == 0 && pen[P_SEL] >= 0 && pen[P_SEL] < 16, "theme_keep: 16 colours as without it");
	theme_exit();
	planes = 8;
	nget = 0;
	theme_palette = (const short (*)[3])palette;
	theme_init(1, mine, 3);
	check(nget == 0 && pen[P_SEL] == 200, "theme_palette: the app's own table, no vq_color, the same pens");
	theme_exit();
	theme_roles = 1UL << P_SEL;
	theme_init(1, mine, 3);
	check(pen[P_SEL] == 200 && pen[P_TEXT] == 1 && pen[P_PANEL] == 0, "theme_roles: P_SEL alone found, the others black and white");
	theme_exit();
	theme_roles = 0;
	theme_palette = 0;
	theme_keep = 0;

	printf("%d checks, %d failed\n", total, failed);
	return failed != 0;
}
