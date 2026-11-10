/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * badge.c - a small boxed label in the 6 x 6 font, one raster copy
 * (badge.h). From EPlay's bar (0.27).
 */
#include <string.h>
#include "badge.h"
#ifndef BADGE_HOST
#include "gem.h"
#endif

int badge_vdi;

static void set(BADGE *b, short x, short y)
{
	if (x >= 0 && y >= 0 && x < BADGE_WPL * 16 && y < BADGE_ROWS)
		b->bits[y * BADGE_WPL + (x >> 4)] |= (unsigned short)(0x8000u >> (x & 15));
}

int badge_form(BADGE *b, const char *s, short cw, short ch, short first, short last, const short *off,
	       const unsigned char *dat, short fw, short fh)
{
	short n = (short)strlen(s), i, x, y;
	b->w = 0;
	b->glyphs = 0;
	b->s[0] = 0;
	if (n <= 0 || n > BADGE_MAX || cw <= 0 || ch <= 0)
		return 0;
	memset(b->bits, 0, sizeof(b->bits));
	b->w = (short)(n * cw + 5);
	b->h = (short)(ch + 4);
	strcpy(b->s, s);
	if (b->w > BADGE_WPL * 16 || b->h > BADGE_ROWS) {
		b->w = 0;		/* too big for the form: badge_draw boxes it */
		return 0;
	}
	for (x = 0; x < b->w; x++) {
		set(b, x, 0);
		set(b, x, (short)(b->h - 1));
	}
	for (y = 0; y < b->h; y++) {
		set(b, 0, y);
		set(b, (short)(b->w - 1), y);
	}
	if (!off || !dat || fh != ch || fw <= 0)
		return 0;
	for (i = 0; i < n; i++) {
		short c = (unsigned char)s[i], g0, g1;
		if (c < first || c > last)
			continue;
		g0 = off[c - first];
		g1 = off[c - first + 1];
		for (x = 0; x < g1 - g0 && x < cw; x++) {
			short sx = (short)(g0 + x);
			for (y = 0; y < fh; y++)
				if (dat[(long)y * fw + (sx >> 3)] & (0x80 >> (sx & 7)))
					set(b, (short)(3 + i * cw + x), (short)(2 + y));
		}
	}
	b->glyphs = 1;
	return 1;
}

#ifndef BADGE_HOST

static short vh, back_h, scw, sch, gen = 1;

static const unsigned char *const *line_a_fonts(void)
{
	register const unsigned char *const *f __asm__("a1");
	__asm__ volatile(".dc.w 0xa000" : "=r"(f) : : "d0", "d1", "d2", "a0", "a2", "memory", "cc");
	return f;
}

void badge_init(short vdi_h, short text_h)
{
	short a, b;
	vh = vdi_h;
	back_h = text_h;
	gen++;
	vst_height(vh, 4, &scw, &sch);
	vst_height(vh, text_h, &a, &b);
	if (scw <= 0 || sch <= 0) {
		scw = a;
		sch = b;
	}
}

short badge_width(const char *s)
{
	return (short)(strlen(s) * scw + 5);
}

short badge_height(void)
{
	return (short)(sch + 4);
}

static void make(BADGE *b, const char *s)
{
	const unsigned char *hd = line_a_fonts()[0];	/* the 6 x 6 */
	if (hd)
		badge_form(b, s, scw, sch, *(const short *)(hd + 36), *(const short *)(hd + 38),
			   *(const short *const *)(hd + 72), *(const unsigned char *const *)(hd + 76),
			   *(const short *)(hd + 80), *(const short *)(hd + 82));
	else
		badge_form(b, s, scw, sch, 0, 0, 0, 0, 0, 0);
	b->gen = gen;
}

static void copy(const BADGE *b, short x, short y, short pen, const short *clip)
{
	MFDB src, dst;
	short pxy[8], colors[2], x1 = x, y1 = y, x2 = (short)(x + b->w - 1), y2 = (short)(y + b->h - 1);
	if (clip) {
		if (x1 < clip[0])
			x1 = clip[0];
		if (y1 < clip[1])
			y1 = clip[1];
		if (x2 > clip[2])
			x2 = clip[2];
		if (y2 > clip[3])
			y2 = clip[3];
		if (x1 > x2 || y1 > y2)
			return;
	}
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = (void *)b->bits;
	src.fd_w = BADGE_WPL * 16;
	src.fd_h = BADGE_ROWS;
	src.fd_wdwidth = BADGE_WPL;
	src.fd_nplanes = 1;
	pxy[0] = (short)(x1 - x);
	pxy[1] = (short)(y1 - y);
	pxy[2] = (short)(x2 - x);
	pxy[3] = (short)(y2 - y);
	pxy[4] = x1;
	pxy[5] = y1;
	pxy[6] = x2;
	pxy[7] = y2;
	colors[0] = pen;
	colors[1] = 0;
	vrt_cpyfm(vh, 2, pxy, &src, &dst, colors);	/* transparent */
}

static void bar(short x, short y, short w, short h)
{
	short p[4];
	p[0] = x;
	p[1] = y;
	p[2] = (short)(x + w - 1);
	p[3] = (short)(y + h - 1);
	vr_recfl(vh, p);
}

short badge_draw(BADGE *b, short x, short y, short rh, const char *s, short pen, const short *clip)
{
	short n = (short)strlen(s), w = (short)(n * scw + 5), h = (short)(sch + 4), by = (short)(y + (rh - h) / 2);
	badge_vdi = 0;
	if (n <= 0)
		return 0;
	if (n > BADGE_MAX) {
		b->w = b->glyphs = 0;	/* longer than a form: the VDI's way */
		b->s[0] = 0;
	} else if (b->gen != gen || strcmp(b->s, s))
		make(b, s);
	if (b->w)
		copy(b, x, by, pen, clip);
	else {
		/* too big for the form: the box the VDI's too */
		badge_vdi = 1;
		vsf_interior(vh, 1);
		vsf_perimeter(vh, 0);
		vsf_color(vh, pen);
		bar(x, by, w, 1);
		bar(x, (short)(by + h - 1), w, 1);
		bar(x, by, 1, h);
		bar((short)(x + w - 1), by, 1, h);
	}
	if (!b->glyphs) {
		short a, c;
		badge_vdi = 1;
		vswr_mode(vh, 2);
		vst_height(vh, 4, &a, &c);
		vst_color(vh, pen);
		vst_effects(vh, 0);
		v_gtext_n(vh, (short)(x + 3), (short)(by + 2), s, n);
		vst_height(vh, back_h, &a, &c);
	}
	return w;
}

#endif
