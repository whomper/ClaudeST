/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The family's progress bar (progbar.h; the guidelines 5.9).
 */
#include "gemui.h"
#include "progbar.h"

static short npen = 1, pen[PB_SHADES];

/* the blue's ends, in the VDI's 0 to 1000 */
static const short dark[3] = { 0, 40, 330 };
static const short light[3] = { 620, 800, 1000 };

static long dist(const short *a, const short *b)
{
	long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return 3 * dr * dr + 4 * dg * dg + 2 * db * db;	/* (pen_nearest's weights) */
}

/* black: the darkest of the first n pens */
static short black_pen(short h, short n)
{
	/* pen 1, GEM's black, unless another is darker: EmuTOS in ST high
	   answers both pens 1000,1000,1000, and the first of a tie was pen 0,
	   white on white (EPDF's bar not shown, 3 Nov 2026) */
	short p, best = 1, rgb[3];
	long bl;
	vq_color(h, 1, rgb);
	bl = 299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2];
	for (p = 0; p < n; p++) {
		long l;
		if (p == 1)
			continue;
		vq_color(h, p, rgb);
		l = 299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2];
		if (l < bl) {
			bl = l;
			best = p;
		}
	}
	return best;
}

void progbar_pens(short h, short ground)
{
	short g[3], rgb[3], p, i;
	vq_color(h, ground, g);
	if (planes == 8) {
		/* 256 colours: each of PB_SHADES steps is the pen nearest to its blue
		   on a smooth ramp from deep navy to sky blue, among the pens that are
		   blue at all (pens 16 and up hold whatever palette is loaded: TOS 4's
		   has no light blue, NVDI's and EWeb's do; found again when it
		   changes, the app's call), never the ground's own. Blue is a hue
		   band, not "more blue than green": in a coarse palette the teals and
		   purples next to it made the steps jump (EPDF's shots, 3 Nov 2026).
		   The picks are sorted dark to light, each pen once. */
		short want[PB_SHADES][3], cand[256], nc = 0, j;
		long bd[PB_SHADES], lm[PB_SHADES];
		for (p = 0; p < 256; p++) {
			short mx, mn;
			vq_color(h, p, rgb);
			if (p == ground || dist(rgb, g) < 3000L)
				continue;	/* (it wouldn't show) */
			mx = rgb[2];
			mn = rgb[0] < rgb[1] ? rgb[0] : rgb[1];
			/* blue is the strongest and the colour is not a grey: 250 of 1000 */
			if (rgb[0] > mx || rgb[1] > mx || mx - mn < 250)
				continue;
			/* hue 195 to 250 degrees: 240 + 60 * (r - g) / (b - min) */
			if (60L * (rgb[0] - rgb[1]) < -45L * (mx - mn) || 60L * (rgb[0] - rgb[1]) > 10L * (mx - mn))
				continue;
			cand[nc++] = p;
		}
		for (i = 0; i < PB_SHADES; i++) {
			int c;
			for (c = 0; c < 3; c++)
				want[i][c] = (short) (dark[c] + (long) (light[c] - dark[c]) * i / (PB_SHADES - 1));
			bd[i] = 0x7fffffffL;
			pen[i] = -1;
		}
		if (nc == 0) {
			/* no blue at all: the nearest pen of any to each step */
			for (p = 0; p < 256; p++)
				if (p != ground)
					cand[nc++] = p;
		}
		for (j = 0; j < nc; j++) {
			vq_color(h, cand[j], rgb);
			for (i = 0; i < PB_SHADES; i++) {
				long d = dist(rgb, want[i]);
				if (d < bd[i]) {
					bd[i] = d;
					pen[i] = cand[j];
				}
			}
		}
		/* each pen once, dark to light */
		npen = 0;
		for (i = 0; i < PB_SHADES; i++) {
			short k, at, cur = pen[i];
			long l;
			if (cur < 0)
				continue;
			for (k = 0; k < npen && pen[k] != cur; k++)
				;
			if (k < npen)
				continue;
			vq_color(h, cur, rgb);
			l = 299L * rgb[0] + 587L * rgb[1] + 114L * rgb[2];
			for (at = npen; at > 0 && lm[at - 1] > l; at--) {
				lm[at] = lm[at - 1];
				pen[at] = pen[at - 1];
			}
			lm[at] = l;
			pen[at] = cur;
			npen++;
		}
		if (npen == 0)
			pen[npen++] = 1;
	} else if (planes == 4) {
		/* 16 colours: pens 1 to 15 in order, less any the ground's
		   colour (white, the bar's light grey) */
		npen = 0;
		for (p = 1; p < 16; p++) {
			vq_color(h, p, rgb);
			if (p == ground || dist(rgb, g) < 3000L)
				continue;
			pen[npen++] = p;
		}
		if (npen == 0)
			pen[npen++] = black_pen(h, 16);
	} else {
		/* mono, 4 colours, true colour: black */
		npen = 1;
		pen[0] = black_pen(h, planes < 4 ? (short) (1 << planes) : 16);
	}
}

void progbar_fill(short h, short x, short y, short w, short hh, short from, short to)
{
	short i, a, b, pxy[4];
	if (from < 0)
		from = 0;
	if (to > PB_FULL)
		to = PB_FULL;
	if (from >= to || w <= 0 || hh <= 0)
		return;
	a = (short) ((long) w * from / PB_FULL);
	b = (short) ((long) w * to / PB_FULL);
	if (a >= b)
		return;
	vsf_interior(h, 1);	/* FIS_SOLID */
	vsf_perimeter(h, 0);
	vswr_mode(h, 1);	/* MD_REPLACE */
	for (i = 0; i < npen; i++) {
		/* each pen's place along the bar: from s to e */
		short s = (short) ((long) w * i / npen), e = (short) ((long) w * (i + 1) / npen);
		if (s < a)
			s = a;
		if (e > b)
			e = b;
		if (s >= e)
			continue;
		vsf_color(h, pen[i]);
		pxy[0] = x + s;
		pxy[1] = y;
		pxy[2] = x + e - 1;
		pxy[3] = y + hh - 1;
		v_bar(h, pxy);
	}
}

short progbar_npens(void)
{
	return npen;
}

short progbar_pen(short i)
{
	return i >= 0 && i < npen ? pen[i] : -1;
}
