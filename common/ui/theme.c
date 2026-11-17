/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * theme.c - the family's UI palette (theme.h): the six base roles, and the app's own after them.
 */
#include "gemui.h"
#include "dialog.h"
#include "theme.h"

short pen[THEME_MAX];
int colour;

#define FIRST_PEN 16		/* past the desktop's sixteen */

/* the base roles: soft shades for 256 colours, VDI 0-1000 RGB (EMail's, EFTP's and EUpdate's, which were the
   same); and for 16 colours TOS's standard shades: black, dark grey, light grey, navy for the selection, picked
   among the screen's own pens (5.2) */
static const THEME_ROLE base[P_BASE] = {
	{ { 133, 145, 165 }, {    0,   0,   0 }, 1, 0 },	/* P_TEXT */
	{ { 420, 447, 502 }, {  533, 533, 533 }, 1, 0 },	/* P_DIM */
	{ { 780, 800, 830 }, {  533, 533, 533 }, 0, 0 },	/* P_LINE */
	{ { 922, 929, 941 }, {  733, 733, 733 }, 0, 0 },	/* P_PANEL */
	{ { 200, 255, 333 }, {    0,   0,   0 }, 1, 0 },	/* P_HEAD */
	{ {   0, 471, 851 }, {    0,   0, 667 }, 1, 0 },	/* P_SEL: the one selection colour (D42) */
};

static short saved[THEME_MAX][3];
static short nset;		/* pens 16 up set by theme_init, to be put back */
static short set4, saved4[3];	/* 4 colours: pen 2 turned to the accent, and what it was */
static int on4;			/* theme_init was asked for colour on 4 colours */

static const THEME_ROLE *role(short i, const THEME_ROLE *extra)
{
	return i < P_BASE ? &base[i] : &extra[i - P_BASE];
}

void theme_exit(void)
{
	short i;
	for (i = 0; i < nset; i++)
		vs_color(vdi_h, (short)(FIRST_PEN + i), saved[i]);
	nset = 0;
	if (set4)
		vs_color(vdi_h, 2, saved4);
	set4 = on4 = 0;
}

void theme_accent4(short role, const short rgb[3])
{
	if (!on4 || set4 || role < 0 || role >= THEME_MAX)
		return;
	vq_color(vdi_h, 2, saved4);
	vs_color(vdi_h, 2, (short *)rgb);
	set4 = 1;
	pen[role] = 2;
}

void theme_init(int colour_on, const THEME_ROLE *extra, short n)
{
	short i, all;
	theme_exit();
	if (n < 0 || !extra)
		n = 0;
	if (n > THEME_MAX - P_BASE)
		n = THEME_MAX - P_BASE;
	all = (short)(P_BASE + n);
	colour = colour_on && planes >= 4;
	for (i = 0; i < THEME_MAX; i++)
		pen[i] = i == P_PANEL ? 0 : 1;		/* black and white */
	on4 = colour_on && planes == 2;
	if (!colour)
		return;
	if (planes >= 8) {
		for (i = 0; i < all; i++) {
			short rgb[3];
			const THEME_ROLE *r = role(i, extra);
			vq_color(vdi_h, (short)(FIRST_PEN + i), saved[i]);
			rgb[0] = r->rgb[0];
			rgb[1] = r->rgb[1];
			rgb[2] = r->rgb[2];
			vs_color(vdi_h, (short)(FIRST_PEN + i), rgb);
			pen[i] = (short)(FIRST_PEN + i);
		}
		nset = all;
	} else {
		for (i = 0; i < all; i++) {
			const THEME_ROLE *r = role(i, extra);
			pen[i] = pen_nearest(vdi_h, 16, r->want16[0], r->want16[1], r->want16[2], r->text16);
		}
		for (i = P_BASE; i < all; i++)
			if (role(i, extra)->not_dim && pen[i] == pen[P_DIM])
				pen[i] = pen[P_TEXT];
	}
}
