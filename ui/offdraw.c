/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * offdraw.c - a small piece of TOS's own screen drawn off it and shown at
 * once (offdraw.h). The About box's way (aboutbox.c's draw_off), for any
 * piece a few rows high.
 */
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "offdraw.h"

extern short _is_acc;		/* crt0.S: started as a desk accessory */

static int ok;			/* 1 the VDI follows the logical screen, -1 it doesn't, 0 not tried */
static unsigned char *buf;	/* OFF_ROWS lines, in ST RAM */
static long buf_bpl;		/* the line length it was taken for */
static struct {
	unsigned char *scr;	/* the logical screen as it was */
	unsigned char *at;	/* the piece's first row, its first word, on the screen */
	unsigned char *in;	/* the same in the buffer */
	long bpl, n;
	short rows;
} p;

/* Line-A's: bytes a line, planes, the screen's width and height */
static const short *line_a(void)
{
	register const short *la __asm__("a0");
	__asm__ volatile(".dc.w 0xa000" : "=r"(la) : : "d0", "d1", "d2", "a1", "a2", "memory", "cc");
	return la;
}

static long setscreen(void *log)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w #-1,-(%%sp)\n\tmove.l #-1,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
			 "move.w #5,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(log) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

/* n bytes (even) of each row: memcpy's longs, a last word */
static void rows_copy(unsigned char *d, const unsigned char *s, long bpl, short rows, long n)
{
	for (; rows > 0; rows--, d += bpl, s += bpl)
		memcpy(d, s, n);
}

int off_state(void)
{
	return ok;
}

int off_begin(short vh, short x, short y, short w, short h)
{
	const short *la = line_a();
	short planes = la[0], sw = la[-6], sh = la[-2], x0, x1;
	long bpl = la[1];
	unsigned char *scr = (unsigned char *)trap14_w(3);	/* Logbase */

	if (ok < 0 || p.scr || planes < 1 || planes > 8 || sw <= 0 || sh <= 0 ||
	    scr != (unsigned char *)trap14_w(2) || bpl < (long)((sw + 15) / 16) * 2 * planes)
		return 0;		/* another screen shown, or already off it */
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > sw)
		w = sw - x;
	if (y + h > sh)
		h = sh - y;
	if (w <= 0 || h <= 0 || h > OFF_ROWS)
		return 0;
	if (!buf || buf_bpl != bpl) {
		long size = bpl * OFF_ROWS;
		if (buf) {
			if (_is_acc)
				free(buf);
			else
				Mfree(buf);
			buf = 0;
		}
		if (_is_acc) {
			/* an accessory takes no GEMDOS memory once it runs: its
			   heap's, in ST RAM, or none */
			buf = malloc(size);
			if (buf && (unsigned long)buf + size > 0x01000000UL) {
				free(buf);
				buf = 0;
			}
		} else {
			buf = Mxalloc(size, 0);		/* ST RAM, where the VDI draws */
			if ((long)buf == -32 || !buf)	/* no Mxalloc: Malloc's is ST RAM */
				buf = Malloc(size);
			if ((long)buf <= 0)
				buf = 0;
		}
		if (!buf)
			return 0;
		buf_bpl = bpl;
	}
	x0 = (short)(x & ~15);
	x1 = (short)((x + w + 15) & ~15);
	p.bpl = bpl;
	p.rows = h;
	p.n = (long)((x1 - x0) >> 4) * planes * 2;
	p.at = scr + (long)y * bpl + (long)(x0 >> 4) * planes * 2;
	p.in = buf + (long)(x0 >> 4) * planes * 2;
	rows_copy(p.in, p.at, bpl, h, p.n);	/* the screen's own pixels first */
	if (!ok) {
		/* does the VDI draw where the logical screen is? the piece's top
		   left pixel, in pen 1 and then 0, in the buffer */
		short pen, attr[5], c[4], px[4], n = (short)(planes * 2);
		unsigned char was[16];
		px[0] = px[2] = x;
		px[1] = px[3] = y;
		c[0] = c[1] = c[2] = c[3] = 0;
		vqf_attributes(vh, attr);
		vswr_mode(vh, 1);
		vsf_interior(vh, 1);
		vsf_perimeter(vh, 0);
		vs_clip(vh, 0, c);
		ok = -1;
		for (pen = 1; pen >= 0 && ok < 0; pen--) {
			memcpy(was, p.in, n);
			setscreen(buf - (long)y * bpl);
			vsf_color(vh, pen);
			vr_recfl(vh, px);
			setscreen(scr);
			if (memcmp(was, p.in, n))
				ok = 1;
		}
		vsf_interior(vh, attr[0]);
		vsf_color(vh, attr[1]);
		vswr_mode(vh, attr[3]);
		vsf_perimeter(vh, attr[4]);
		if (ok < 0) {
			/* the test pixel went to the screen: the screen's own
			   pixels back from the buffer */
			rows_copy(p.at, p.in, bpl, h, p.n);
			return 0;
		}
		rows_copy(p.in, p.at, bpl, h, p.n);	/* the test pixel gone */
	}
	p.scr = scr;
	setscreen(buf - (long)y * bpl);	/* the screen's row y at the buffer's start */
	{
		/* the VDI clipped to the piece from here: a draw outside its rows
		   would go to the memory round the buffer, not to the screen
		   (the hub's review, 1 Nov 2026). The caller's own clip, set
		   after this, may be smaller; the AES's objc_draw takes its own */
		short c[4];
		c[0] = x;
		c[1] = y;
		c[2] = x + w - 1;
		c[3] = y + h - 1;
		vs_clip(vh, 1, c);
	}
	return 1;
}

void off_end(void)
{
	if (!p.scr)
		return;
	setscreen(p.scr);
	rows_copy(p.at, p.in, p.bpl, p.rows, p.n);	/* shown at once */
	p.scr = 0;
}
