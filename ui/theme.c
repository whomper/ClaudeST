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
int theme_keep;
const short (*theme_palette)[3];
unsigned long theme_roles;

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

/* each role the nearest of n pens in pal, by pen_nearest's weights (3, 4, 2), a text role (text16) not one of the
   light pens while a dark one is left: the light pens marked once, the differences in shorts (16 x 16 bits, the
   68000's own multiply, not libgcc's 32-bit one), an exact match ending the search. 16 colours: want16, 256: rgb.
   EHypView's start (22 Nov 2026): 25 ms on a 16-colour Falcon, 70 under NVDI in 256, before this */
static void nearest_roles(const short (*pal)[3], short n, short all, const THEME_ROLE *extra, int by_rgb,
			  unsigned long want_roles)
{
	unsigned char light[256];
	short i, p;
	for (p = 0; p < n; p++)
		light[p] = 299L * pal[p][0] + 587L * pal[p][1] + 114L * pal[p][2] > 600000L;
	for (i = 0; i < all; i++) {
		const THEME_ROLE *r = role(i, extra);
		const short *want = by_rgb ? r->rgb : r->want16;
		if (want_roles && !(want_roles & (1UL << i)))
			continue;	/* left at black and white's pen */
		short best = -1;
		long bd = 0x7fffffffL;
		int pass;
		for (pass = 0; pass < 2 && best < 0; pass++)
			for (p = 0; p < n; p++) {
				short dr, dg, db;
				long d;
				if (r->text16 && !pass && light[p])
					continue;
				dr = (short)(pal[p][0] - want[0]);
				dg = (short)(pal[p][1] - want[1]);
				db = (short)(pal[p][2] - want[2]);
				d = 3 * ((long)dr * dr) + 4 * ((long)dg * dg) + 2 * ((long)db * db);
				if (d < bd) {
					bd = d;
					best = p;
					if (!d)
						break;
				}
			}
		pen[i] = best < 0 ? 1 : best;
	}
	if (!want_roles || (want_roles & (1UL << P_DIM)))
		for (i = P_BASE; i < all; i++)
			if (role(i, extra)->not_dim && pen[i] == pen[P_DIM])
				pen[i] = pen[P_TEXT];
}

/* the screen's first n pens: theme_palette when the app holds it, else read once (n vq_color calls) */
static const short (*palette_of(short n, short (*own)[3]))[3]
{
	short p;
	if (theme_palette)
		return theme_palette;
	for (p = 0; p < n; p++)
		vq_color(vdi_h, p, own[p]);
	return (const short (*)[3])own;
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
	if (planes >= 8 && theme_keep) {
		short own[256][3];
		nearest_roles(palette_of(256, own), 256, all, extra, 1, theme_roles);
	} else if (planes >= 8) {
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
		short own[16][3];
		nearest_roles(palette_of(16, own), 16, all, extra, 0, theme_keep ? theme_roles : 0);	/* the 16 read once, not once a role */
	}
}
