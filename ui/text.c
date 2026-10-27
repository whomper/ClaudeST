/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * text.c - text drawn the fast way TOS allows (text.h). From EHypView's
 * text_at and text_fx_after (e2f417a); EContact's bold is the same copy.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "text.h"

static short clip_now[4];
static int clipping;
static int fast;			/* the system font, TOS's own VDI */
static int bold_unclipped;		/* TOS bolds before it clips */
static short cw = 8, ch = 16;

static unsigned short os_version;
static unsigned long os_magic;

static void sup_os(void)
{
	const unsigned char *os;
	__asm__ volatile("move.l 0x4f2.w,%0" : "=a"(os));	/* _sysbase */
	os_version = *(const unsigned short *)(os + 2);
	os_magic = *(const unsigned long *)(os + 0x2c);
}

void text_font(short h, short id, short w, short hgt)
{
	(void)h;
	fast = id == 1 && !get_cookie(0x4E564449L);	/* 'NVDI' */
	/* TOS 4.04's VDI bolds a line and then clips it, so a column left
	   of the clip, not drawn, smears into the clip's first; TOS 1.04's
	   and EmuTOS's clip first (tests/text). TOS 3.06 is taken to be
	   TOS 4's: its VDI is the same code, not measured */
	Supexec(sup_os);
	bold_unclipped = os_version >= 0x0300 && os_magic != 0x45544F53L;	/* 'ETOS' */
	cw = w > 0 ? w : 8;
	ch = hgt > 0 ? hgt : 16;
}

void text_clip(short h, const short *r)
{
	memcpy(clip_now, r, sizeof(clip_now));
	clipping = 1;
	vs_clip(h, 1, clip_now);
}

void text_noclip(short h)
{
	clipping = 0;
	vs_clip(h, 0, clip_now);
}

/* v_gtext_n in pieces the VDI takes (its intin holds 128 characters) */
static void run(short h, short x, short y, const char *s, long n)
{
	while (n > 0) {
		short k = n > 120 ? 120 : (short)n;
		v_gtext_n(h, x, y, s, k);
		x = (short)(x + k * cw);
		s += k;
		n -= k;
	}
}

/* TOS takes the slow way only when the clip's right or bottom edge is
   less than 2 pixels past the text (EFTP, 26 Oct 2026: its left and top
   edges make no difference) */
static int room(short x2, short y2)
{
	return x2 + 2 <= clip_now[2] && y2 + 2 <= clip_now[3];
}

void text_n(short h, short x, short y, const char *s, long n)
{
	long i0 = 0, m;
	short xs;
	if (!clipping || !fast || y < clip_now[1] || y + ch - 1 > clip_now[3] || n <= 0 ||
	    (x >= clip_now[0] && room((short)(x + n * cw - 1), (short)(y + ch - 1)))) {
		run(h, x, y, s, n);	/* no clip, or nothing cut and fast with it */
		return;
	}
	/* the first character wholly inside, and how many are */
	if (x < clip_now[0])
		i0 = (clip_now[0] - x + cw - 1) / cw;
	if (i0 > n)
		i0 = n;
	xs = (short)(x + i0 * cw);
	m = xs <= clip_now[2] ? (clip_now[2] - xs + 1) / cw : 0;
	if (m > n - i0)
		m = n - i0;
	if (i0 > 0 && xs - cw <= clip_now[2])
		run(h, (short)(xs - cw), y, s + i0 - 1, 1);	/* cut on the left */
	if (m > 0 && room((short)(xs + m * cw - 1), (short)(y + ch - 1))) {
		run(h, xs, y, s + i0, m);
	} else if (m > 0) {
		vs_clip(h, 0, clip_now);
		run(h, xs, y, s + i0, m);
		vs_clip(h, 1, clip_now);
	}
	if (i0 + m < n && xs + m * cw <= clip_now[2])
		run(h, (short)(xs + m * cw), y, s + i0 + m, 1);	/* cut on the right */
}

int text_fx_fast(short fx, short mode)
{
	return fast && mode == 1 && fx && !(fx & ~9);	/* MD_REPLACE; bold, underline */
}

void text_fx(short h, short x, short y, const char *s, long n, short fx, short colour)
{
	short tw = (short)(n * cw);
	if (fx & 1) {
		/* bold: the text's box ORed onto itself a pixel to the right,
		   cut to the clip by hand (the VDI clips raster copies too, TOS's
		   and NVDI's: EFTP, 26 Oct 2026; the cut costs nothing) */
		short x1 = (short)(x + 1), x2 = (short)(x + tw), y1 = y, y2 = (short)(y + ch - 1);
		if (clipping) {
			if (x1 < clip_now[0])
				x1 = clip_now[0];
			if (x2 > clip_now[2])
				x2 = clip_now[2];
			if (y1 < clip_now[1])
				y1 = clip_now[1];
			if (y2 > clip_now[3])
				y2 = clip_now[3];
		}
		if (x1 <= x2 && y1 <= y2) {
			MFDB scr;
			short p[8];
			memset(&scr, 0, sizeof(scr));
			p[0] = (short)(x1 - 1);
			p[1] = y1;
			p[2] = (short)(x2 - 1);
			p[3] = y2;
			p[4] = x1;
			p[5] = y1;
			p[6] = x2;
			p[7] = y2;
			vro_cpyfm(h, 7, p, &scr, &scr);		/* S_OR_D */
		}
		/* where TOS bolds before it clips, the column just left of the
		   clip, not drawn, still smears into the clip's first: that
		   character again a pixel to the right, transparent, gives it */
		if (bold_unclipped && clipping && clip_now[0] > x && clip_now[0] <= x + tw) {
			long j = (clip_now[0] - 1 - x) / cw;
			if (j < n) {
				vswr_mode(h, 2);
				v_gtext_n(h, (short)(x + j * cw + 1), y, s + j, 1);
				vswr_mode(h, 1);
			}
		}
	}
	if (fx & 8) {
		/* underline: the cell's last row, a pixel longer than the text */
		short p[4];
		p[0] = x;
		p[2] = (short)(x + tw);
		p[1] = p[3] = (short)(y + ch - 1);
		vswr_mode(h, 1);
		vsl_color(h, colour);
		vsl_type(h, 1);
		v_pline(h, 2, p);
	}
}
