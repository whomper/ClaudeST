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
static int sf_rows_cpu;			/* scr_rows by the CPU: a 68000 (on a 68030 TOS's copy is as fast) */
static long sf_bpl;
static unsigned char *base_held;	/* Logbase while scr_clip holds a clip (an XBIOS call 100 us of an ST) */

static unsigned char *logbase(void)
{
	return base_held ? base_held : (unsigned char *)trap14_w(3);
}

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
/* the system font of this cell height's form (8x8 or 8x16), or below 0:
   -2 another height, -3 none in Line-A, -4 its header not as TOS's, -5
   not 8 pixels a glyph */
static int sys_form(short height, const unsigned char **form)
{
	const unsigned char *const *fonts, *hd, *dat;
	const short *off;
	short first, last, fw, i;

	if (height != 8 && height != 16)
		return -2;
	line_a(&fonts);
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
	*form = dat;
	return 0;
}

static int sf_setup(short height)
{
	const unsigned char *const *fonts;
	const short *la = line_a(&fonts);
	int e = sys_form(height, &glyph);

	if (e < 0)
		return e;
	sf_rows_cpu = get_cookie(0x5F435055L) < 20;	/* '_CPU' */
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

int scr_cpu_wins(void)
{
	return sf == 1 && sf_rows_cpu;
}

int text_plain_state(void)
{
	return sf;
}

void text_reclip(short h)
{
	vs_clip(h, clipping, clip_now);
}

unsigned char *scr_at(long *bpl, short *planes, short *w, short *h)
{
	if (sf != 1)
		return 0;
	*bpl = sf_bpl;
	*planes = sf_planes;
	*w = sf_w;
	*h = sf_h;
	return (unsigned char *)trap14_w(3);	/* Logbase: where the VDI draws now */
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

/* a row's middle from s to d: blocks of 40 bytes by movem, then longs
   (0 to 9, by a jump into a run of moves) and a last word when tail.
   The counts are worked out once a scroll, not a row: a short's / 40 is
   a library call on the 68000 */
static inline __attribute__((always_inline)) void move_row(unsigned char *d, const unsigned char *s, short blocks, short longs, short tail)
{
	__asm__ volatile("move.w %2,%%d0\n\t"
			 "bra.s 2f\n"
			 "1:\tmovem.l (%0)+,%%d1-%%d7/%%a2-%%a4\n\t"
			 "movem.l %%d1-%%d7/%%a2-%%a4,(%1)\n\t"
			 "lea 40(%1),%1\n"
			 "2:\tdbra %%d0,1b\n\t"
			 "move.w %3,%%d0\n\t"
			 "add.w %%d0,%%d0\n\t"
			 "neg.w %%d0\n\t"
			 "jmp 3f(%%pc,%%d0.w)\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n\t"
			 "move.l (%0)+,(%1)+\n"
			 "3:\ttst.w %4\n\t"
			 "beq.s 4f\n\t"
			 "move.w (%0)+,(%1)+\n"
			 "4:"
			 : "+a"(s), "+a"(d)
			 : "m"(blocks), "m"(longs), "m"(tail)
			 : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a2", "a3", "a4", "memory", "cc");
}

/* the planes' words of one 16-pixel group, the pixels in mask; np
   planes (1, 2, 4 or 8) unrolled, each word d ^ ((d ^ s) & mask): with
   a loop over sf_planes it was some 300 cycles a call, the most of a
   narrow pane's scroll (EContact's list, 31 Oct 2026) */
static inline void move_group(unsigned short *d, const unsigned short *s, unsigned short mask, short np)
{
	switch (np) {
	case 8:
		d[7] ^= (d[7] ^ s[7]) & mask;
		d[6] ^= (d[6] ^ s[6]) & mask;
		d[5] ^= (d[5] ^ s[5]) & mask;
		d[4] ^= (d[4] ^ s[4]) & mask;
		/* fall through */
	case 4:
		d[3] ^= (d[3] ^ s[3]) & mask;
		d[2] ^= (d[2] ^ s[2]) & mask;
		/* fall through */
	case 2:
		d[1] ^= (d[1] ^ s[1]) & mask;
		/* fall through */
	default:
		d[0] ^= (d[0] ^ s[0]) & mask;
	}
}

void scr_rows(short h, const short *p)
{
	short x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3], ty = p[5], g0, g1, m0, m1, rows, r, step;
	unsigned short lmask, rmask;
	unsigned char *base;
	long bpl;

	/* by the CPU only on a 68000 and a box at least 384 pixels wide: a
	   row has its overhead and two cut groups, so a narrow box is
	   faster through TOS's copy (TOS 1.04 ST:mono, 192 wide: vro_cpyfm
	   27 ms, the CPU 51), and on a 68030 TOS's copy is as fast (Falcon-4:
	   592 wide, 35 against 36) */
	if (sf != 1 || !sf_rows_cpu || x2 - x1 < 383 || p[4] != x1 || p[6] != x2 || p[7] - ty != y2 - y1 || x1 < 0 || x2 < x1 || x2 >= sf_w || y1 < 0 ||
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
		short blocks = 0, longs = 0, tail = 0, np = sf_planes;

		if (mid > 0) {
			/* once a scroll: 40-byte blocks, longs, a word */
			short rest = mid;

			while (rest >= 40) {
				rest -= 40;
				blocks++;
			}
			longs = (short)(rest >> 2);
			tail = (short)(rest & 2);
		}
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
				move_group((unsigned short *)(d + l), (const unsigned short *)(s + l), lmask, np);
			if (mid > 0)
				move_row(d + m, s + m, blocks, longs, tail);
			if (right)
				move_group((unsigned short *)(d + rr), (const unsigned short *)(s + rr), rmask, np);
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

/* n bytes of a row, every plane's, set to v */
static void set_row(unsigned char *d, short n, unsigned long v)
{
	unsigned long *q = (unsigned long *)d;
	short k;

	for (k = n >> 5; k > 0; k--) {
		q[0] = v;
		q[1] = v;
		q[2] = v;
		q[3] = v;
		q[4] = v;
		q[5] = v;
		q[6] = v;
		q[7] = v;
		q += 8;
	}
	for (k = (n & 31) >> 2; k > 0; k--, q++)
		*q = v;
	if (n & 2)
		*(unsigned short *)q = (unsigned short)v;
}

/* the box p (x1, y1, x2, y2) cut to the clip and the screen, every plane
   of each pixel turned over (op 0), cleared (1, pen 0) or set (2, pen 1),
   by the CPU on TOS's screen (sf 1) */
static void box_op(const short *p, int op, const unsigned short *pat)
{
	short x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3], g0, g1, m0, m1, r, step, k;
	unsigned short lmask, rmask;
	unsigned char *row;

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
	row = logbase() + (long)y1 * sf_bpl;	/* Logbase */
	{
		long l = (long)g0 * step, m = (long)m0 * step, rr = (long)g1 * step, bpl = sf_bpl;
		short mid = (short)((m1 - m0 + 1) * step), left = m0 > g0, right = g1 != g0 && m1 < g1;

		short y = y1;
		if (op <= 2) {
			/* inverted (0), cleared (1) or set (2) with every choice made
			   once for the box, not once a row: a call and the op's tests
			   each row made a 16 x 16 box slower than TOS's own fill (EMail
			   1.72; tests/text, 320 boxes: ST 510 ms against vr_recfl's
			   425, Falcon-4 220 against 130). An edge word is (w & a) ^ x */
			unsigned short la = op == 0 ? 0xFFFFu : (unsigned short)~lmask, lx = op == 1 ? 0 : lmask;
			unsigned short ra = op == 0 ? 0xFFFFu : (unsigned short)~rmask, rx = op == 1 ? 0 : rmask;
			unsigned long v = op == 2 ? 0xFFFFFFFFUL : 0;
			short nl = (short)(mid >> 2), tail = (short)(mid & 2), planes = sf_planes, n;

			if (mid <= 0)
				nl = tail = 0;
			for (r = (short)(y2 - y1 + 1); r > 0; r--, row += bpl) {
				unsigned short *w;
				unsigned long *q = (unsigned long *)(row + m);

				if (left)
					for (w = (unsigned short *)(row + l), k = planes; k > 0; k--, w++)
						*w = (unsigned short)((*w & la) ^ lx);
				if (nl >= 8) {
					/* a wide box: the rows' unrolled loops (the plain loop
					   here made a window's rows 58 -> 66 ms on ST:mono) */
					if (op == 0)
						xor_row((unsigned char *)q, mid);
					else
						set_row((unsigned char *)q, mid, v);
				} else if (op == 0) {
					for (n = nl; n > 0; n--, q++)
						*q = ~*q;
					if (tail)
						*(unsigned short *)q = (unsigned short)~*(unsigned short *)q;
				} else {
					for (n = nl; n > 0; n--)
						*q++ = v;
					if (tail)
						*(unsigned short *)q = (unsigned short)v;
				}
				if (right)
					for (w = (unsigned short *)(row + rr), k = planes; k > 0; k--, w++)
						*w = (unsigned short)((*w & ra) ^ rx);
			}
			return;
		}
		for (r = (short)(y2 - y1 + 1); r > 0; r--, row += bpl, y++) {
			/* op 3: the pattern's row, its set pixels in every plane
			   (pen 1), the others in none (pen 0) */
			unsigned short pw = op == 3 ? pat[y & 15] : 0;
			if (left) {
				unsigned short *w = (unsigned short *)(row + l);

				for (k = 0; k < sf_planes; k++)
					w[k] = op == 0 ? w[k] ^ lmask : op == 1 ? w[k] & ~lmask : op == 2 ? w[k] | lmask :
					       (unsigned short)((w[k] & ~lmask) | (pw & lmask));
			}
			if (mid > 0) {
				if (op == 0)
					xor_row(row + m, mid);
				else
					set_row(row + m, mid, op == 1 ? 0 : op == 2 ? 0xFFFFFFFFUL :
						((unsigned long)pw << 16) | pw);
			}
			if (right) {
				unsigned short *w = (unsigned short *)(row + rr);

				for (k = 0; k < sf_planes; k++)
					w[k] = op == 0 ? w[k] ^ rmask : op == 1 ? w[k] & ~rmask : op == 2 ? w[k] | rmask :
					       (unsigned short)((w[k] & ~rmask) | (pw & rmask));
			}
		}
	}
}

/* the same, op 4 only: the pattern's set pixels cleared (MD_TRANS in pen
   0, the DISABLED grey). Apart from box_op, whose row loop stays as it
   is: a fifth op in it made TXT's rows cleared on ST:mono 53 ms against
   45 */
static void box_pat(const short *p, int op, const unsigned short *pat)
{
	short x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3], g0, g1, m0, m1, r, step, k;
	unsigned short lmask, rmask;
	unsigned char *row;

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
	row = logbase() + (long)y1 * sf_bpl;	/* Logbase */
	{
		long l = (long)g0 * step, m = (long)m0 * step, rr = (long)g1 * step, bpl = sf_bpl;
		short mid = (short)((m1 - m0 + 1) * step), left = m0 > g0, right = g1 != g0 && m1 < g1;

		short y = y1;
		for (r = (short)(y2 - y1 + 1); r > 0; r--, row += bpl, y++) {
			/* op 3: the pattern's row, its set pixels in every plane
			   (pen 1), the others in none (pen 0) */
			unsigned short pw = op >= 3 ? pat[y & 15] : 0;
			if (left) {
				unsigned short *w = (unsigned short *)(row + l);

				for (k = 0; k < sf_planes; k++)
					w[k] = op == 0 ? w[k] ^ lmask : op == 1 ? w[k] & ~lmask : op == 2 ? w[k] | lmask :
					       op == 4 ? (unsigned short)(w[k] & ~(pw & lmask)) :
					       (unsigned short)((w[k] & ~lmask) | (pw & lmask));
			}
			if (mid > 0 && op == 4) {
				unsigned short *w = (unsigned short *)(row + m);
				short n;

				for (n = (short)(mid >> 1); n > 0; n--, w++)
					*w = (unsigned short)(*w & ~pw);
			} else if (mid > 0) {
				if (op == 0)
					xor_row(row + m, mid);
				else
					set_row(row + m, mid, op == 1 ? 0 : op == 2 ? 0xFFFFFFFFUL :
						((unsigned long)pw << 16) | pw);
			}
			if (right) {
				unsigned short *w = (unsigned short *)(row + rr);

				for (k = 0; k < sf_planes; k++)
					w[k] = op == 0 ? w[k] ^ rmask : op == 1 ? w[k] & ~rmask : op == 2 ? w[k] | rmask :
					       op == 4 ? (unsigned short)(w[k] & ~(pw & rmask)) :
					       (unsigned short)((w[k] & ~rmask) | (pw & rmask));
			}
		}
	}
}

void scr_xor(short h, const short *p)
{
	if (sf != 1) {
		vswr_mode(h, 3);	/* MD_XOR */
		vsf_interior(h, 1);	/* solid */
		vsf_color(h, 1);
		vr_recfl(h, (short *)p);
		vswr_mode(h, 1);
		return;
	}
	box_op(p, 0, 0);
}

void scr_fill(short h, const short *p, short pen)
{
	if (sf != 1 || (pen != 0 && pen != 1)) {
		/* the VDI's settings put back after, as the CPU's way leaves
		   them: ECal, EContact and EMail keep what they set last */
		short was[5];
		vqf_attributes(h, was);
		vswr_mode(h, 1);	/* MD_REPLACE */
		vsf_interior(h, 1);	/* solid */
		vsf_color(h, pen);
		vr_recfl(h, (short *)p);
		vsf_interior(h, was[0]);
		vsf_color(h, was[1]);
		vswr_mode(h, was[3]);
		return;
	}
	box_op(p, pen == 0 ? 1 : 2, 0);
}

int scr_pattern(short h, const short *p, const unsigned short *pat)
{
	(void)h;
	if (sf != 1)
		return 0;
	box_op(p, 3, pat);
	return 1;
}

int scr_grey_off(const short *p)
{
	/* the VDI's pattern 4 of interior 2 (50%), its set pixels where x + y
	   is even: cleared */
	static const unsigned short half[16] = { 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555,
						 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555 };
	if (sf != 1)
		return 0;
	box_pat(p, 4, half);
	return 1;
}

int scr_spans(short y1, short y2, const short *sp, short n)
{
	/* the band's words: those the spans cover in part, each as (w & keep)
	   | set, and runs of whole words in one pen */
	struct {
		short g;
		unsigned short keep, set;
	} op[24];
	struct {
		short g0, g1;
		unsigned long v;
	} run[12];
	short nop = 0, nrun = 0, cx1, cx2, k, r, step = (short)(sf_planes * 2);
	long bpl = sf_bpl;
	unsigned char *row;

	if (sf != 1)
		return 0;
	cx1 = clipping ? clip_now[0] : 0;
	cx2 = clipping ? clip_now[2] : (short)(sf_w - 1);
	if (cx1 < 0)
		cx1 = 0;
	if (cx2 > sf_w - 1)
		cx2 = (short)(sf_w - 1);
	if (clipping && y1 < clip_now[1])
		y1 = clip_now[1];
	if (clipping && y2 > clip_now[3])
		y2 = clip_now[3];
	if (y1 < 0)
		y1 = 0;
	if (y2 > sf_h - 1)
		y2 = (short)(sf_h - 1);
	if (y1 > y2)
		return 1;
	for (k = 0; k < n; k++) {
		short x0 = sp[3 * k], x1 = sp[3 * k + 1], pen = sp[3 * k + 2], g0, g1, g;
		unsigned short lm, rm;

		if (x0 < cx1)
			x0 = cx1;
		if (x1 > cx2)
			x1 = cx2;
		if (x0 > x1)
			continue;
		g0 = (short)(x0 >> 4);
		g1 = (short)(x1 >> 4);
		lm = (unsigned short)(0xFFFFu >> (x0 & 15));
		rm = (unsigned short)(0xFFFFu << (15 - (x1 & 15)));
		if (g0 == g1) {
			lm &= rm;
			rm = 0;
		}
		for (g = g0; g <= g1; g++) {
			unsigned short m = g == g0 ? lm : g == g1 ? rm : 0xFFFF;
			short i;

			if (m == 0xFFFF) {
				/* a whole word: onto the run before it, or a new one */
				if (nrun && run[nrun - 1].g1 == g - 1 && run[nrun - 1].v == (pen ? 0xFFFFFFFFUL : 0))
					run[nrun - 1].g1 = g;
				else if (nrun < 12) {
					run[nrun].g0 = run[nrun].g1 = g;
					run[nrun].v = pen ? 0xFFFFFFFFUL : 0;
					nrun++;
				} else
					return 0;
				continue;
			}
			for (i = 0; i < nop && op[i].g != g; i++)
				;
			if (i == nop) {
				if (nop == 24)
					return 0;
				op[nop].g = g;
				op[nop].keep = 0xFFFF;
				op[nop].set = 0;
				nop++;
			}
			op[i].keep &= (unsigned short)~m;
			op[i].set = (unsigned short)((op[i].set & ~m) | (pen ? m : 0));
		}
	}
	row = logbase() + (long)y1 * bpl;	/* Logbase */
	{
		/* the places in a row worked out once: a multiply a word a row
		   cost the root of EFTP's Settings 86 ms */
		short roff[12], rlen[12], ooff[24], p, np = (short)sf_planes;

		for (k = 0; k < nrun; k++) {
			roff[k] = (short)(run[k].g0 * step);
			rlen[k] = (short)((run[k].g1 - run[k].g0 + 1) * step);
		}
		for (k = 0; k < nop; k++)
			ooff[k] = (short)(op[k].g * step);
		for (r = (short)(y2 - y1 + 1); r > 0; r--, row += bpl) {
			for (k = 0; k < nrun; k++)
				set_row(row + roff[k], rlen[k], run[k].v);
			if (np == 1)
				for (k = 0; k < nop; k++) {
					unsigned short *w = (unsigned short *)(row + ooff[k]);

					*w = (unsigned short)((*w & op[k].keep) | op[k].set);
				}
			else
				for (k = 0; k < nop; k++) {
					unsigned short *w = (unsigned short *)(row + ooff[k]), keep = op[k].keep, set = op[k].set;

					for (p = np; p > 0; p--, w++)
						*w = (unsigned short)((*w & keep) | set);
				}
		}
	}
	return 1;
}

int scr_bits(short x, short y, const unsigned char *b, short wb, short rows, short pen)
{
	/* a bitmap's rows, wb bytes each, its set bits in pen (MD_TRANS) */
	short cx1, cy1, cx2, cy2, r, k, shift = (short)(x & 7), ls = (short)(8 - (x & 7)), step = (short)(sf_planes * 2);
	long bpl = sf_bpl;
	unsigned char *base;

	if (sf != 1 || (pen != 0 && pen != 1))
		return 0;
	cx1 = clipping ? clip_now[0] : 0;
	cy1 = clipping ? clip_now[1] : 0;
	cx2 = clipping ? clip_now[2] : (short)(sf_w - 1);
	cy2 = clipping ? clip_now[3] : (short)(sf_h - 1);
	if (cx1 < 0)
		cx1 = 0;
	if (cy1 < 0)
		cy1 = 0;
	if (cx2 > sf_w - 1)
		cx2 = (short)(sf_w - 1);
	if (cy2 > sf_h - 1)
		cy2 = (short)(sf_h - 1);
	base = logbase();	/* Logbase */
	for (r = 0; r < rows; r++) {
		short yy = (short)(y + r);

		if (yy < cy1 || yy > cy2)
			continue;
		for (k = 0; k < wb; k++) {
			unsigned char v = b[(long)r * wb + k], ml = 0xFF, mr = 0xFF, a, c;
			short px = (short)(x + k * 8), bl = (short)(px >> 3), br = (short)(bl + 1), p;
			unsigned char *row = base + (long)yy * bpl, *dl, *dr;

			if (!v)
				continue;
			if (bl * 8 < cx1)
				ml = (unsigned char)(bl * 8 + 7 < cx1 ? 0 : 0xFF >> (cx1 - bl * 8));
			if (bl * 8 + 7 > cx2)
				ml &= (unsigned char)(bl * 8 > cx2 ? 0 : 0xFF << (bl * 8 + 7 - cx2));
			if (br * 8 < cx1)
				mr = (unsigned char)(br * 8 + 7 < cx1 ? 0 : 0xFF >> (cx1 - br * 8));
			if (br * 8 + 7 > cx2)
				mr &= (unsigned char)(br * 8 > cx2 ? 0 : 0xFF << (br * 8 + 7 - cx2));
			a = (unsigned char)((v >> shift) & ml);
			c = shift ? (unsigned char)((v << ls) & mr) : 0;
			dl = row + (bl >> 1) * step + (bl & 1);
			dr = row + (br >> 1) * step + (br & 1);
			for (p = 0; p < step; p += 2) {
				if (pen) {
					dl[p] |= a;
					if (c)
						dr[p] |= c;
				} else {
					dl[p] &= (unsigned char)~a;
					if (c)
						dr[p] &= (unsigned char)~c;
				}
			}
		}
	}
	return 1;
}

void scr_clip(const short *r)
{
	static short keep[4];
	static int keep_on, held;

	if (r) {
		if (!held) {
			memcpy(keep, clip_now, sizeof(keep));
			keep_on = clipping;
			held = 1;
			base_held = (unsigned char *)trap14_w(3);	/* Logbase, once a dialog */
		}
		memcpy(clip_now, r, sizeof(clip_now));
		clipping = 1;
	} else if (held) {
		memcpy(clip_now, keep, sizeof(keep));
		clipping = keep_on;
		held = 0;
		base_held = 0;
	}
}

/* one glyph byte a row into the screen's bytes dl and dr (dr: 0 on a
   byte), nr rows from gp on: v >> sh into dl, v << (8 - sh) into dr,
   each cut by its mask; pen 1 sets every plane's bits, pen 0 clears them */
static void glyph_put(unsigned char *dl, unsigned char *dr, const unsigned char *gp, short nr, short sh,
		      unsigned char ml, unsigned char mr, short pen, short step)
{
	long bpl = sf_bpl;
	short r, p, ls = (short)(8 - sh);

	if (step == 2 && bpl == 80 && nr == 16 && ml == 0xFF && pen) {
		/* ST high, a whole cell: each row at a fixed place */
		if (!dr) {
#define ROW1(k) dl[(k) * 80] |= gp[(k) * 256]
			ROW1(0); ROW1(1); ROW1(2); ROW1(3); ROW1(4); ROW1(5); ROW1(6); ROW1(7);
			ROW1(8); ROW1(9); ROW1(10); ROW1(11); ROW1(12); ROW1(13); ROW1(14); ROW1(15);
#undef ROW1
			return;
		}
		if (mr == 0xFF) {
#define ROW2(k) do { unsigned char v = gp[(k) * 256]; dl[(k) * 80] |= (unsigned char)(v >> sh); \
		     dr[(k) * 80] |= (unsigned char)(v << ls); } while (0)
			ROW2(0); ROW2(1); ROW2(2); ROW2(3); ROW2(4); ROW2(5); ROW2(6); ROW2(7);
			ROW2(8); ROW2(9); ROW2(10); ROW2(11); ROW2(12); ROW2(13); ROW2(14); ROW2(15);
#undef ROW2
			return;
		}
	}
	for (r = nr; r > 0; r--, gp += 256, dl += bpl, dr = dr ? dr + bpl : 0) {
		unsigned char v = *gp, a, b;

		if (!v)
			continue;
		a = (unsigned char)((v >> sh) & ml);
		b = dr ? (unsigned char)((v << ls) & mr) : 0;
		for (p = 0; p < step; p += 2) {
			if (pen) {
				dl[p] |= a;
				if (dr)
					dr[p] |= b;
			} else {
				dl[p] &= (unsigned char)~a;
				if (dr)
					dr[p] &= (unsigned char)~b;
			}
		}
	}
}

int scr_text(short x, short y, const char *s, long n, short pen, short height, int ground)
{
	static const unsigned char *form[2];
	static signed char got[2];
	const unsigned char *g;
	short cx1, cy1, cx2, cy2, r0, r1, shift, step, b0, b1, nb, k0, j, p, len;
	unsigned char lmask, rmask;
	unsigned char *row;
	long bpl = sf_bpl;

	if (sf != 1 || (pen != 0 && pen != 1) || (height != 8 && height != 16))
		return 0;
	{
		int f = height == 16;

		if (!got[f])
			got[f] = (signed char)(sys_form(height, &form[f]) < 0 ? -1 : 1);
		if (got[f] < 0)
			return 0;
		g = form[f];
	}
	if (n <= 0)
		return 1;
	len = (short)(n > 200 ? 200 : n);
	cx1 = clipping ? clip_now[0] : 0;
	cy1 = clipping ? clip_now[1] : 0;
	cx2 = clipping ? clip_now[2] : (short)(sf_w - 1);
	cy2 = clipping ? clip_now[3] : (short)(sf_h - 1);
	if (cx1 < x)
		cx1 = x;
	if (cx1 < 0)
		cx1 = 0;
	if (cx2 > x + len * 8 - 1)
		cx2 = (short)(x + len * 8 - 1);
	if (cx2 > sf_w - 1)
		cx2 = (short)(sf_w - 1);
	if (cy1 < 0)
		cy1 = 0;
	if (cy2 > sf_h - 1)
		cy2 = (short)(sf_h - 1);
	r0 = (short)(cy1 > y ? cy1 - y : 0);
	r1 = (short)(cy2 < y + height - 1 ? cy2 - y : height - 1);
	if (r0 > r1 || cx1 > cx2)
		return 1;		/* nothing of it shows */
	step = (short)(sf_planes * 2);
	shift = (short)(x & 7);
	row = logbase() + (long)(y + r0) * bpl;	/* Logbase */
	if (!ground) {
		/* character by character: its glyph's byte a row into the one
		   screen byte under it, or the two (off a byte), cut to the
		   clip at the ends; a space draws nothing */
		short kf = (short)((cx1 - x) >> 3), kl = (short)((cx2 - x) >> 3), k, nr = (short)(r1 - r0 + 1);
		const unsigned char *gr = g + ((unsigned)r0 << 8);

		int st_high = step == 2 && bpl == 80 && nr == 16 && pen;
		short ls = (short)(8 - shift);

		for (k = kf; k <= kl; k++) {
			short px = (short)(x + k * 8), bl = (short)(px >> 3);
			unsigned char ml = 0xFF, mr = 0xFF, c = (unsigned char)s[k];
			unsigned char *dl, *dr = 0;

			if (c == ' ')
				continue;
			if (st_high && (bl << 3) >= cx1 && (bl << 3) + (shift ? 15 : 7) <= cx2) {
				/* ST high, the whole cell inside the clip: each row at a
				   fixed place, no call (a letter some 3.5 times
				   text_plain's time the other way) */
				const unsigned char *gp = gr + c;
				unsigned char *d = row + bl;

				if (!shift) {
#define C1(r) d[(r) * 80] |= gp[(r) * 256]
					C1(0); C1(1); C1(2); C1(3); C1(4); C1(5); C1(6); C1(7);
					C1(8); C1(9); C1(10); C1(11); C1(12); C1(13); C1(14); C1(15);
#undef C1
				} else if (!(bl & 1)) {
					/* on a word: the glyph shifted in one word, both bytes
					   at once */
#define C2(r) *(unsigned short *)(d + (r) * 80) |= (unsigned short)((unsigned short)gp[(r) * 256] << ls)
					C2(0); C2(1); C2(2); C2(3); C2(4); C2(5); C2(6); C2(7);
					C2(8); C2(9); C2(10); C2(11); C2(12); C2(13); C2(14); C2(15);
#undef C2
				} else {
#define C3(r) do { unsigned char v = gp[(r) * 256]; d[(r) * 80] |= (unsigned char)(v >> shift); \
		   d[(r) * 80 + 1] |= (unsigned char)(v << ls); } while (0)
					C3(0); C3(1); C3(2); C3(3); C3(4); C3(5); C3(6); C3(7);
					C3(8); C3(9); C3(10); C3(11); C3(12); C3(13); C3(14); C3(15);
#undef C3
				}
				continue;
			}
			if (step == 4 && pen && (bl << 3) >= cx1 && (bl << 3) + (shift ? 15 : 7) <= cx2) {
				/* ST medium, the whole cell inside the clip: both planes'
				   bytes of each row, no call (28 letters 12 ms of an 8
				   MHz ST the other way) */
				const unsigned char *gp = gr + c;
				unsigned char *d = row + (bl >> 1) * 4 + (bl & 1), *e = 0;
				short r;

				if (shift)
					e = (bl & 1) ? d + 3 : d + 1;
				for (r = nr; r > 0; r--, gp += 256, d += bpl) {
					unsigned char v = *gp;

					if (v) {
						unsigned char a = (unsigned char)(v >> shift);

						d[0] |= a;
						d[2] |= a;
						if (e) {
							unsigned char b = (unsigned char)(v << ls);

							e[0] |= b;
							e[2] |= b;
						}
					}
					if (e)
						e += bpl;
				}
				continue;
			}
			/* the clip on the bytes: pixels cx1..cx2 */
			if (bl * 8 < cx1)
				ml = (unsigned char)(0xFF >> (cx1 - bl * 8));
			if (bl * 8 + 7 > cx2)
				ml &= (unsigned char)(0xFF << (bl * 8 + 7 - cx2));
			dl = row + (bl >> 1) * step + (bl & 1);
			if (shift) {
				short br = (short)(bl + 1);

				if (br * 8 > cx2)
					mr = 0;
				else if (br * 8 + 7 > cx2)
					mr = (unsigned char)(0xFF << (br * 8 + 7 - cx2));
				if (br * 8 < cx1)
					mr &= (unsigned char)(0xFF >> (cx1 - br * 8));
				if (mr)
					dr = row + (br >> 1) * step + (br & 1);
			}
			if (shift && !dr)	/* the right byte cut off: the left alone */
				glyph_put(dl, 0, gr + c, nr, shift, ml, 0, pen, step);
			else if (!shift)
				glyph_put(dl, 0, gr + c, nr, 0, ml, 0, pen, step);
			else
				glyph_put(dl, dr, gr + c, nr, shift, ml, mr, pen, step);
		}
		return 1;
	}
	/* MD_REPLACE (the cells' ground pen 0): byte by byte along each row,
	   the end of one character and the start of the next */
	b0 = (short)(cx1 >> 3);
	b1 = (short)(cx2 >> 3);
	nb = (short)(b1 - b0 + 1);
	lmask = (unsigned char)(0xFF >> (cx1 & 7));
	rmask = (unsigned char)(0xFF << (7 - (cx2 & 7)));
	if (nb == 1)
		lmask &= rmask;
	k0 = (short)(b0 - (x >> 3) - (shift ? 1 : 0));
	row += (b0 >> 1) * step + (b0 & 1);
	for (; r0 <= r1; r0++, row += bpl) {
		const unsigned char *gr = g + ((unsigned)r0 << 8), *sp = (const unsigned char *)s + k0;
		unsigned short acc = shift && k0 >= 0 ? gr[*sp] : 0;
		unsigned char *d = row;
		short k = shift ? k0 + 1 : k0, bi = b0;

		if (shift)
			sp++;
		for (j = 0; j < nb; j++, bi++, k++, sp++) {
			unsigned char v, m = j == 0 ? lmask : j == nb - 1 ? rmask : 0xFF;

			if (shift) {
				acc = (unsigned short)((acc << 8) | (k < len ? gr[*sp] : 0));
				v = (unsigned char)(acc >> shift);
			} else
				v = gr[*sp];
			v &= m;
			for (p = 0; p < step; p += 2) {	/* every plane: pens 0 and 1 */
				unsigned char o = (unsigned char)(d[p] & ~m);

				d[p] = (unsigned char)(pen ? o | v : o & ~v);
			}
			d += (bi & 1) ? step - 1 : 1;
		}
	}
	return 1;
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
