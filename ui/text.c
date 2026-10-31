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

/* text_plain's way into the screen: the glyphs read where TOS keeps them
   (Line-A's font: a form 256 bytes wide, glyph c's row r at r * 256 + c,
   no copy, so nothing in BSS for programs that never call text_plain), the
   screen's layout from Line-A; on (1) once the first draw's check found
   the VDI's pixels where it would put them, off (-1) for the run else */
static int sf;				/* 0 not checked yet, 1 on, -1 off */
static const unsigned char *glyph;	/* the font's form: row r of c at [r * 256 + c] */
static short sf_planes, sf_w, sf_h;
static long sf_bpl;

static void sup_os(void)
{
	const unsigned char *os;
	__asm__ volatile("move.l 0x4f2.w,%0" : "=a"(os));	/* _sysbase */
	os_version = *(const unsigned short *)(os + 2);
	os_magic = *(const unsigned long *)(os + 0x2c);
}

/* Line-A's variables (a0) and its three system fonts (a1) */
static const short *line_a(const unsigned char *const **fonts)
{
	register const short *la __asm__("a0");
	register const unsigned char *const *f __asm__("a1");
	__asm__ volatile(".dc.w 0xa000" : "=r"(la), "=r"(f) : : "d0", "d1", "d2", "a2", "memory", "cc");
	*fonts = f;
	return la;
}

/* the system font of this cell height from Line-A, its form 256 bytes
   wide; the screen as Line-A has it: 0 (checked at the first draw), or
   below 0 when it can't be the fast way */
static int sf_setup(short height)
{
	const unsigned char *const *fonts, *hd, *dat;
	const short *la = line_a(&fonts), *off;
	short first, last, fw, i;

	if (height != 8 && height != 16)
		return -2;
	hd = fonts[height == 16 ? 2 : 1];
	if (!hd)
		return -3;
	first = *(const short *)(hd + 36);
	last = *(const short *)(hd + 38);
	off = *(const short *const *)(hd + 72);
	dat = *(const unsigned char *const *)(hd + 76);
	fw = *(const short *)(hd + 80);
	/* the whole set, 8 pixels a glyph on a byte, in the 68000's byte
	   order (flag 4, as TOS's and EmuTOS's own have it), a form as high
	   as the cell */
	if (first != 0 || last != 255 || !off || !dat || !(*(const short *)(hd + 66) & 4) ||
	    *(const short *)(hd + 82) != height || fw != 256)
		return -4;
	for (i = 0; i <= 256; i++)
		if (off[i] != i * 8)
			return -5;
	glyph = dat;
	sf_planes = la[0];		/* v_planes */
	sf_bpl = la[1];			/* v_lin_wr */
	sf_w = la[-6];			/* V_REZ_HZ */
	sf_h = la[-2];			/* V_REZ_VT */
	if ((sf_planes != 1 && sf_planes != 2 && sf_planes != 4 && sf_planes != 8) || sf_w <= 0 || sf_h <= 0 ||
	    sf_bpl != (long)sf_w / 8 * sf_planes)
		return -6;
	return 0;			/* the first draw checks it */
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
	sf = fast && cw == 8 ? sf_setup(ch) : -1;
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
   edges make no difference). A row really cut by the clip (a window's
   last row) can't be helped here: text_plain draws it by its visible
   pixels, at its usual speed */
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

/* the screen's byte holding pixel x of row y, plane 0 (planes are the
   words of a 16-pixel group, one after another) */
static unsigned char *at(unsigned char *base, short x, short y)
{
	return base + (long)y * sf_bpl + (long)(x >> 4) * sf_planes * 2 + ((x >> 3) & 1);
}

/* n whole cells of 8x16 on ST high's screen (one plane, 80 bytes a
   line) from d on: each glyph row one move to a fixed place, 16 cycles a
   byte on a 68000 */
static void cells_st_high(unsigned char *d, const unsigned char *sp, short n)
{
	for (; n > 0; n--, d++) {
		const unsigned char *g = glyph + *sp++;

		d[0] = g[0];		d[80] = g[256];		d[160] = g[512];	d[240] = g[768];
		d[320] = g[1024];	d[400] = g[1280];	d[480] = g[1536];	d[560] = g[1792];
		d[640] = g[2048];	d[720] = g[2304];	d[800] = g[2560];	d[880] = g[2816];
		d[960] = g[3072];	d[1040] = g[3328];	d[1120] = g[3584];	d[1200] = g[3840];
	}
}

/* n whole cells from d on, rows r0 on (nr of them), the first at pixel
   px: the same byte in every plane (pen 1 sets all of a pixel's planes,
   pen 0 none), planes 2, 4 or 8 */
static void cells_planes(unsigned char *d, const unsigned char *sp, short n, short px, short r0, short nr, short step)
{
	long bpl = sf_bpl;

	for (; n > 0; n--, px += 8) {
		const unsigned char *g = glyph + *sp++ + ((unsigned)r0 << 8);
		unsigned char *e = d;
		short r;

		switch (step) {
		case 4:
			for (r = nr; r > 0; r--, e += bpl) {
				unsigned char v = *g;

				g += 256;

				e[0] = v;
				e[2] = v;
			}
			break;
		case 8:
			for (r = nr; r > 0; r--, e += bpl) {
				unsigned char v = *g;

				g += 256;

				e[0] = v;
				e[2] = v;
				e[4] = v;
				e[6] = v;
			}
			break;
		default:
			for (r = nr; r > 0; r--, e += bpl) {
				unsigned char v = *g;

				g += 256;

				e[0] = v;
				e[2] = v;
				e[4] = v;
				e[6] = v;
				e[8] = v;
				e[10] = v;
				e[12] = v;
				e[14] = v;
			}
			break;
		}
		d += (px & 8) ? step - 1 : 1;
	}
}

/* character c's cell, its rows r0 on (nr of them), at the screen's byte
   d for pixel px, the columns inside cx1..cx2 */
static void one_cell(unsigned char *d, unsigned char c, short px, short cx1, short cx2, short r0, short nr, short step)
{
	const unsigned char *g = glyph + c + ((unsigned)r0 << 8);
	unsigned char mask = 0xFF;
	short r, p;

	if (px < cx1)
		mask = (unsigned char)(0xFF >> (cx1 - px));
	if (px + 7 > cx2)
		mask &= (unsigned char)(0xFF << (px + 7 - cx2));
	if (mask == 0xFF && step == 2) {
		for (r = nr; r > 0; r--, d += sf_bpl)
			*d = *g, g += 256;
	} else if (mask == 0xFF) {
		for (r = nr; r > 0; r--, d += sf_bpl) {
			unsigned char v = *g;

			g += 256;

			for (p = 0; p < step; p += 2)
				d[p] = v;
		}
	} else {
		for (r = nr; r > 0; r--, d += sf_bpl) {
			unsigned char v = *g;

			g += 256;

			for (p = 0; p < step; p += 2)
				d[p] = (unsigned char)((d[p] & ~mask) | (v & mask));
		}
	}
}

/* does the screen at base show character c's cell at (x, y), drawn by the
   VDI in pen 1 on 0? */
static int cell_is(unsigned char *base, short x, short y, unsigned char c)
{
	const unsigned char *g = glyph + c;
	const unsigned char *d = at(base, x, y);
	short r, p;

	for (r = 0; r < ch; r++, d += sf_bpl, g += 256)
		for (p = 0; p < sf_planes; p++)
			if (d[p * 2] != *g)
				return 0;
	return 1;
}

int text_plain_state(void)
{
	return sf;
}

void text_plain(short h, short x, short y, const char *s, long n)
{
	short cx1, cy1, cx2, cy2, r0, r1;
	long i, i0, i1;
	unsigned char *base;

	if (sf < 0 || (x & 7) || n <= 0) {
		text_n(h, x, y, s, n);
		return;
	}
	if (clipping) {
		cx1 = clip_now[0];
		cy1 = clip_now[1];
		cx2 = clip_now[2];
		cy2 = clip_now[3];
	} else {
		cx1 = cy1 = 0;
		cx2 = (short)(sf_w - 1);
		cy2 = (short)(sf_h - 1);
	}
	if (cx1 < 0)
		cx1 = 0;
	if (cy1 < 0)
		cy1 = 0;
	if (cx2 > sf_w - 1)
		cx2 = (short)(sf_w - 1);
	if (cy2 > sf_h - 1)
		cy2 = (short)(sf_h - 1);
	r0 = (short)(cy1 > y ? cy1 - y : 0);
	r1 = (short)(cy2 < y + ch - 1 ? cy2 - y : ch - 1);
	i0 = cx1 > x ? (cx1 - x) / 8 : 0;
	i1 = cx2 < x + n * 8 - 1 ? (cx2 - x) / 8 : n - 1;
	if (r0 > r1 || i0 > i1 || x + i0 * 8 > cx2)
		return;			/* nothing of it shows: the VDI draws nothing either */
	base = (unsigned char *)trap14_w(3);	/* Logbase: where the VDI draws now */
	if (!sf) {
		/* the first time: drawn by the VDI, then a whole cell of it whose
		   glyph isn't blank compared with what would have been put there */
		long k;

		text_n(h, x, y, s, n);
		if (r0 != 0 || r1 != ch - 1)
			return;
		for (k = i0; k <= i1; k++) {
			short kx = (short)(x + k * 8), r;
			const unsigned char *g = glyph + (unsigned char)s[k];

			for (r = 0; r < ch && !g[(unsigned)r << 8]; r++)
				;
			if (r < ch && kx >= cx1 && kx + 7 <= cx2) {
				sf = cell_is(base, kx, y, (unsigned char)s[k]) ? 1 : -1;
				return;
			}
		}
		return;
	}
	{
		/* along the row: the screen's byte for pixel px of row y + r0,
		   then the next cell's, a byte on or the next 16-pixel group's
		   first */
		short nr = (short)(r1 - r0 + 1), step = (short)(sf_planes * 2), px;
		unsigned char *row = base + (long)(y + r0) * sf_bpl, *d;
		const unsigned char *sp = (const unsigned char *)s + i0;

		px = (short)(x + i0 * 8);
		d = row + (long)(px >> 4) * step + ((px >> 3) & 1);
		for (i = i0; i <= i1; i++, px += 8) {
			if (px >= cx1 && (step > 2 || (sf_bpl == 80 && ch == 16 && nr == 16))) {
				/* the whole cells from here in one run: ST high's
				   unrolled, or every plane's byte at once */
				long last = i1, k;

				if (x + last * 8 + 7 > cx2)
					last--;
				if (last >= i) {
					k = last - i + 1;
					if (step == 2)
						cells_st_high(d, sp, (short)k);
					else
						cells_planes(d, sp, (short)k, px, r0, nr, step);
					sp += k;
					px = (short)(px + k * 8);
					d = row + (long)(px >> 4) * step + ((px >> 3) & 1);
					i = last + 1;
					if (i > i1)
						break;
				}
			}
			one_cell(d, *sp++, px, cx1, cx2, r0, nr, step);
			d += (px & 8) ? step - 1 : 1;
		}
	}
}

/* n bytes (even) from s to d, 40 a turn by movem, then words */
static void move_row(unsigned char *d, const unsigned char *s, short n)
{
	short blocks = n / 40, words = (short)((n - blocks * 40) >> 1);

	__asm__ volatile("move.w %2,%%d0\n\t"
			 "bra.s 2f\n"
			 "1:\tmovem.l (%0)+,%%d1-%%d7/%%a2-%%a4\n\t"
			 "movem.l %%d1-%%d7/%%a2-%%a4,(%1)\n\t"
			 "lea 40(%1),%1\n"
			 "2:\tdbra %%d0,1b\n\t"
			 "move.w %3,%%d0\n\t"
			 "bra.s 4f\n"
			 "3:\tmove.w (%0)+,(%1)+\n"
			 "4:\tdbra %%d0,3b"
			 : "+a"(s), "+a"(d)
			 : "m"(blocks), "m"(words)
			 : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a2", "a3", "a4", "memory", "cc");
}

/* the planes' words of one 16-pixel group, the pixels in mask */
static void move_group(unsigned short *d, const unsigned short *s, unsigned short mask)
{
	short p;

	for (p = 0; p < sf_planes; p++)
		d[p] = (unsigned short)((d[p] & ~mask) | (s[p] & mask));
}

void scr_rows(short h, const short *p)
{
	short x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3], ty = p[5], g0, g1, m0, m1, rows, r, step;
	unsigned short lmask, rmask;
	unsigned char *base;
	long bpl;

	if (sf != 1 || p[4] != x1 || p[6] != x2 || p[7] - ty != y2 - y1 || x1 < 0 || x2 < x1 || x2 >= sf_w || y1 < 0 ||
	    y2 < y1 || y2 >= sf_h || ty < 0 || p[7] >= sf_h ||
	    (clipping && (x1 < clip_now[0] || x2 > clip_now[2] || ty < clip_now[1] || p[7] > clip_now[3]))) {
		MFDB scr;

		memset(&scr, 0, sizeof(scr));
		vro_cpyfm(h, 3, p, &scr, &scr);
		return;
	}
	base = (unsigned char *)trap14_w(3);	/* Logbase */
	bpl = sf_bpl;
	step = (short)(sf_planes * 2);
	g0 = x1 >> 4;
	g1 = x2 >> 4;
	lmask = (unsigned short)(0xFFFFu >> (x1 & 15));
	rmask = (unsigned short)(0xFFFFu << (15 - (x2 & 15)));
	if (g0 == g1) {
		lmask &= rmask;
		rmask = 0;
	}
	/* the whole groups between the cut ones */
	m0 = lmask == 0xFFFF ? g0 : g0 + 1;
	m1 = g0 == g1 ? g0 - 1 : rmask == 0xFFFF ? g1 : g1 - 1;
	if (lmask == 0xFFFF && g0 == g1)
		m1 = g0;
	rows = (short)(y2 - y1 + 1);
	{
		/* the first row's places worked out once, then a line on (or
		   back) each row: up the screen from the top row, down it from
		   the last, so no row is written before it is read */
		long l = (long)g0 * step, m = (long)m0 * step, rr = (long)g1 * step, line = bpl;
		short mid = (short)((m1 - m0 + 1) * step), left = m0 > g0 || (g0 == g1 && lmask != 0xFFFF),
		      right = g1 != g0 && m1 < g1;
		unsigned char *s, *d;

		if (ty <= y1) {
			s = base + (long)y1 * bpl;
			d = base + (long)ty * bpl;
		} else {
			s = base + (long)y2 * bpl;
			d = base + (long)p[7] * bpl;
			line = -bpl;
		}
		for (r = rows; r > 0; r--, s += line, d += line) {
			if (left)
				move_group((unsigned short *)(d + l), (const unsigned short *)(s + l), lmask);
			if (mid > 0)
				move_row(d + m, s + m, mid);
			if (right)
				move_group((unsigned short *)(d + rr), (const unsigned short *)(s + rr), rmask);
		}
	}
}

/* n bytes (even) from d on turned over, longs eight at a turn */
static void xor_row(unsigned char *d, short n)
{
	unsigned long *q = (unsigned long *)d;
	short k;

	for (k = n >> 5; k > 0; k--) {
		q[0] = ~q[0];
		q[1] = ~q[1];
		q[2] = ~q[2];
		q[3] = ~q[3];
		q[4] = ~q[4];
		q[5] = ~q[5];
		q[6] = ~q[6];
		q[7] = ~q[7];
		q += 8;
	}
	for (k = (n & 31) >> 2; k > 0; k--, q++)
		*q = ~*q;
	if (n & 2)
		*(unsigned short *)q = (unsigned short)~*(unsigned short *)q;
}

void scr_xor(short h, const short *p)
{
	short x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3], g0, g1, m0, m1, r, step, k;
	unsigned short lmask, rmask;
	unsigned char *row;

	if (sf != 1) {
		vswr_mode(h, 3);	/* MD_XOR */
		vsf_interior(h, 1);	/* solid */
		vsf_color(h, 1);
		vr_recfl(h, (short *)p);
		vswr_mode(h, 1);
		return;
	}
	if (x1 > x2) {
		short t = x1;
		x1 = x2;
		x2 = t;
	}
	if (y1 > y2) {
		short t = y1;
		y1 = y2;
		y2 = t;
	}
	if (clipping) {
		if (x1 < clip_now[0])
			x1 = clip_now[0];
		if (y1 < clip_now[1])
			y1 = clip_now[1];
		if (x2 > clip_now[2])
			x2 = clip_now[2];
		if (y2 > clip_now[3])
			y2 = clip_now[3];
	}
	if (x1 < 0)
		x1 = 0;
	if (y1 < 0)
		y1 = 0;
	if (x2 > sf_w - 1)
		x2 = (short)(sf_w - 1);
	if (y2 > sf_h - 1)
		y2 = (short)(sf_h - 1);
	if (x1 > x2 || y1 > y2)
		return;
	step = (short)(sf_planes * 2);
	g0 = x1 >> 4;
	g1 = x2 >> 4;
	lmask = (unsigned short)(0xFFFFu >> (x1 & 15));
	rmask = (unsigned short)(0xFFFFu << (15 - (x2 & 15)));
	if (g0 == g1) {
		lmask &= rmask;
		rmask = 0;
	}
	m0 = lmask == 0xFFFF ? g0 : g0 + 1;
	m1 = g0 == g1 ? (lmask == 0xFFFF ? g0 : g0 - 1) : rmask == 0xFFFF ? g1 : g1 - 1;
	row = (unsigned char *)trap14_w(3) + (long)y1 * sf_bpl;	/* Logbase */
	{
		long l = (long)g0 * step, m = (long)m0 * step, rr = (long)g1 * step, bpl = sf_bpl;
		short mid = (short)((m1 - m0 + 1) * step), left = m0 > g0, right = g1 != g0 && m1 < g1;

		for (r = (short)(y2 - y1 + 1); r > 0; r--, row += bpl) {
			if (left) {
				unsigned short *w = (unsigned short *)(row + l);

				for (k = 0; k < sf_planes; k++)
					w[k] ^= lmask;
			}
			if (mid > 0)
				xor_row(row + m, mid);
			if (right) {
				unsigned short *w = (unsigned short *)(row + rr);

				for (k = 0; k < sf_planes; k++)
					w[k] ^= rmask;
			}
		}
	}
}

void text_bold_strike(short h, short x, short y, const char *s, long n)
{
	vswr_mode(h, 2);	/* MD_TRANS */
	text_n(h, (short)(x + 1), y, s, n);
	vswr_mode(h, 1);
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
