/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aboutbox.c - the About box (see aboutbox.h), ECal's, drawn as EMail's and
 * Claude ST's were; in the same frame, a box of plain text (CRASH.LOG).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "gemui.h"
#include "aboutbox.h"
#include "alert.h"
#include "dialog.h"
#include "crash.h"
#include "tos.h"

#define ICON_W 32
#define ICON_H 32
#define GPL_LINE "Free software under the GNU GPL 2 or later."

static short dx, dy, dw, dh;

static void line(short x1, short y1, short x2, short y2)
{
	line_col(x1, y1, x2, y2, 1);
}

static void rect_fill(short x1, short y1, short x2, short y2, short color)
{
	GRECT r;
	r.x = x1;
	r.y = y1;
	r.w = x2 - x1 + 1;
	r.h = y2 - y1 + 1;
	fill(&r, color);
}

/* clip to the screen, never off: a box clamped to the desk has its
   shadow one row past the screen's foot, and with clipping off the VDI
   writes it past the screen's end (in the Falcon's 640x200 true colour
   the screen ends where ST RAM does: a bus error, two bombs; EWeb found
   it) */
static void clip_screen(void)
{
	GRECT s;
	s.x = 0;
	s.y = 0;
	s.w = desk_x + desk_w;
	s.h = desk_y + desk_h;
	clip_on(&s);
}

static void box(short x, short y, short w, short h)
{
	/* the shadow: its two strips, not a box-sized fill painted over (on
	   a stock Falcon 15 ms of the About box's 155) */
	rect_fill(x + w, y + 3, x + w + 2, y + h + 2, 1);
	rect_fill(x + 3, y + h, x + w - 1, y + h + 2, 1);
	rect_fill(x, y, x + w - 1, y + h - 1, 0);
	line(x, y, x + w - 1, y);
	line(x + w - 1, y, x + w - 1, y + h - 1);
	line(x + w - 1, y + h - 1, x, y + h - 1);
	line(x, y + h - 1, x, y);
	line(x + 2, y + 2, x + w - 3, y + 2);
	line(x + w - 3, y + 2, x + w - 3, y + h - 3);
	line(x + w - 3, y + h - 3, x + 2, y + h - 3);
	line(x + 2, y + h - 3, x + 2, y + 2);
}

/* the 32x32 icon scaled by sx, sy (2x on square pixels; 2x1 in ST
 * medium, whose pixels are twice as tall as wide), blitted from ST RAM */
static unsigned short icon_big[64 * 4];
static const unsigned short *icon_was;	/* the icon and scale icon_big holds */
static short icon_sx, icon_sy;

static void draw_icon(const unsigned short *bits, short x, short y, short sx, short sy)
{
	MFDB src, dst;
	short pxy[8], colors[2], r, c, k, wpl = 2 * sx;
	short w = ICON_W * sx, h = ICON_H * sy;
	if (icon_was == bits && icon_sx == sx && icon_sy == sy)
		goto made;		/* scaled once (an ST took 30 ms each time) */
	icon_was = bits;
	icon_sx = sx;
	icon_sy = sy;
	memset(icon_big, 0, sizeof(icon_big));
	for (r = 0; r < ICON_H; r++)
		for (c = 0; c < ICON_W; c++) {
			if (!(bits[r * 2 + c / 16] & (0x8000 >> (c & 15))))
				continue;
			for (k = 0; k < sx; k++) {
				short bx = c * sx + k, yy;
				for (yy = 0; yy < sy; yy++)
					icon_big[(r * sy + yy) * wpl + bx / 16] |= 0x8000 >> (bx & 15);
			}
		}
made:
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = st_ram(sizeof(icon_big));
	if (!src.fd_addr)
		return;
	memcpy(src.fd_addr, icon_big, sizeof(icon_big));
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	colors[0] = 1;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

/* an icon of pens (32x32, a byte a pixel) scaled by sx, sy: one 1-plane
 * mask a pen it has, each blitted once, transparent, in that pen (map[]
 * or pens[] turning its pens into the screen's). As runs of VDI fills it
 * took a stock Falcon some 300 fills; now one vrt_cpyfm a colour (the
 * guidelines' Speed, Erez, 26 Oct 2026). */
static void icon_pens(const unsigned char *px, const unsigned char *map, const short *pens,
		      short x, short y, short sx, short sy)
{
	unsigned char slot[256];
	short pen_of[64], n = 0, r, c, k, yy, i, wpl = 2 * sx, words = wpl * ICON_H * sy;
	short w = ICON_W * sx, h = ICON_H * sy, pxy[8], colors[2];
	unsigned short *masks;
	MFDB src, dst;
	memset(slot, 0xff, sizeof(slot));
	for (i = 0; i < ICON_W * ICON_H; i++)
		if (px[i] && slot[px[i]] == 0xff && n < 64) {
			slot[px[i]] = (unsigned char)n;
			pen_of[n++] = px[i];
		}
	if (!n || !(masks = calloc((size_t)n * words, 2)))
		return;
	for (r = 0; r < ICON_H; r++)
		for (c = 0; c < ICON_W; c++) {
			unsigned char s = px[r * ICON_W + c];
			unsigned short *m;
			if (!s || slot[s] == 0xff)
				continue;
			m = masks + slot[s] * words;
			for (k = 0; k < sx; k++) {
				short bx = c * sx + k;
				for (yy = 0; yy < sy; yy++)
					m[(r * sy + yy) * wpl + (bx >> 4)] |= 0x8000 >> (bx & 15);
			}
		}
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = st_ram((long)words * 2);
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	for (i = 0; src.fd_addr && i < n; i++) {
		memcpy(src.fd_addr, masks + i * words, (size_t)words * 2);
		colors[0] = map ? map[pen_of[i]] : pens ? pens[pen_of[i]] : pen_of[i];
		colors[1] = 0;
		vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
	}
	free(masks);
}

/* the 16-colour icon (two pixels a byte); pens 8..15 are lent the
 * icon's colours while the box is open */
void about_icon16(const unsigned char *px, short x, short y, short sx, short sy, const short *pens)
{
	unsigned char p[ICON_W * ICON_H];
	short i;
	for (i = 0; i < ICON_W * ICON_H; i++)
		p[i] = (i & 1) ? (px[i >> 1] & 15) : (px[i >> 1] >> 4);
	icon_pens(p, 0, pens, x, y, sx, sy);
}

#ifndef NO_ICON256
/* the 256-colour icon, a byte a pixel; map[] turns its pens into the
   screen's */
static void icon256_draw(const unsigned char *px, const unsigned char *map, short x, short y, short sx, short sy)
{
	icon_pens(px, map, 0, x, y, sx, sy);
}

/* for each colour of the icon, the pen that has it now: its own when the
   palette is TOS's, else the nearest (an app's theme may have taken pens
   16 and up: EMail's). Nothing is lent, so nothing else on the screen
   changes colour while the box is open. */
static void icon256_pens(const short (*rgb)[4], unsigned char *map)
{
	short i, p, have[3], (*pal)[3] = 0;
	for (i = 0; rgb[i][0]; i++) {
		long best = -1;
		short pen = rgb[i][0];
		vq_color(vdi_h, pen, have);
		if (abs(have[0] - rgb[i][1]) + abs(have[1] - rgb[i][2]) + abs(have[2] - rgb[i][3]) > 24) {
			/* the palette asked once, not 256 times a colour (the
			   guidelines' Speed) */
			if (!pal && (pal = malloc(256 * sizeof(*pal))) != 0)
				for (p = 0; p < 256; p++)
					vq_color(vdi_h, p, pal[p]);
			for (p = 0; pal && p < 256; p++) {
				long d;
				short *h = pal[p];
				d = (long)(h[0] - rgb[i][1]) * (h[0] - rgb[i][1]) +
				    (long)(h[1] - rgb[i][2]) * (h[1] - rgb[i][2]) +
				    (long)(h[2] - rgb[i][3]) * (h[2] - rgb[i][3]);
				if (best < 0 || d < best) {
					best = d;
					pen = p;
				}
			}
		}
		map[rgb[i][0]] = (unsigned char)pen;
	}
	free(pal);
}
#endif

const char *about_ram(void)
{
	/* TT/Fast RAM starts at 16 MB: where TOS loaded the program and
	   where its memory came from */
	void *m = malloc(16);
	int code = (unsigned long)about_ram >= 0x01000000UL, heap = (unsigned long)m >= 0x01000000UL;
	free(m);
	return code && heap ? "TT RAM" : code || heap ? "TT RAM in part" : "";
}

/* x on a byte (the nearest): TOS 4.04's v_gtext took a stock Falcon 9.1
   ms for 39 letters a pixel off a byte, 3.4 ms on one (tests/speed/vdt.py) */
static short on_byte(short x)
{
	return (x + 4) & ~7;
}

/* bold as TOS draws it, but plain text and its box ORed onto itself a
   pixel to the right (common/ui/text.c's text_fx: the same pixels, on an
   ST a third of vst_effects' time); NVDI's own bold elsewhere */
static void text_b(short x, short y, const char *s, short n, short flags)
{
	if ((flags & TX_BOLD) && !get_cookie(0x4E564449L)) {	/* 'NVDI' */
		MFDB scr;
		short p[8];
		text_at(x, y, s, n, n, (flags & ~TX_BOLD) | TX_LTR);
		memset(&scr, 0, sizeof(scr));
		p[0] = x;
		p[1] = y;
		p[2] = x + n * cw - 1;
		p[3] = y + ch - 1;
		p[4] = x + 1;
		p[5] = y;
		p[6] = x + n * cw;
		p[7] = y + ch - 1;
		vro_cpyfm(vdi_h, 7, p, &scr, &scr);	/* S_OR_D */
		return;
	}
	text_at(x, y, s, n, n, flags | TX_LTR);
}

static void center(short y, const char *s, short flags)
{
	short n = (short)strlen(s);
	text_b(on_byte(dx + (dw - n * cw) / 2), y, s, n, flags);
}

static void button(short x, short y, short w, short h, const char *label)
{
	short n = (short)strlen(label);
	rect_fill(x, y, x + w - 1, y + h - 1, 0);
	line(x, y, x + w - 1, y);
	line(x + w - 1, y, x + w - 1, y + h - 1);
	line(x + w - 1, y + h - 1, x, y + h - 1);
	line(x, y + h - 1, x, y);
	/* the default button's thick GEM border */
	line(x - 1, y - 1, x + w, y - 1);
	line(x + w, y - 1, x + w, y + h);
	line(x + w, y + h, x - 1, y + h);
	line(x - 1, y + h, x - 1, y - 1);
	text_b(on_byte(x + (w - n * cw) / 2), y + (h - ch) / 2, label, n, TX_BOLD);
}

static void wait_up(void)
{
	short mx, my, mb, ks;
	do {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (mb & 3)
			evnt_timer_(10);
	} while (mb & 3);
}

/* Return, Esc, Undo, Help, Space or a click closes it */
static void wait_close(void)
{
	short m[8], done = 0;
	AEVENT e;
	wait_up();
	while (!done) {
		dlg_evnt(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);	/* in a window: its closer as Esc */
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);
			if (as == 0x0d || as == 0x1b || as == ' ' || sc == 0x61 || sc == 0x62)
				done = 1;
		}
		if (e.which & MU_BUTTON) {
			wait_up();
			done = 1;
		}
	}
}

/* the box's layout, for its drawing (about_draw) */
static const ABOUT *ab;
static short ab_sx, ab_sy, ab_ih, ab_lh, ab_g, ab_nst, ab_nfine, ab_fstep, ab_fcw, ab_bw, ab_bh, ab_colour;
#ifndef NO_ICON256
static unsigned char map256[256];	/* the icon's pens to the screen's */
#endif

/* the whole box at dx, dy: framed with its shadow over the screen (TOS),
   on white in a window of the AES's, which is the frame (MiNT, MagiC) */
static void about_draw(int framed)
{
	const ABOUT *a = ab;
	char v[40];
	short sx = ab_sx, sy = ab_sy, ih = ab_ih, lh = ab_lh, g = ab_g, nst = ab_nst;
	short nfine = ab_nfine, fstep = ab_fstep, fcw, fch, bw = ab_bw, bh = ab_bh;
	short by = dy + dh - bh - lh, y, i;

	vst_height(vdi_h, ch >= 16 ? 13 : 6, &fcw, &fch);	/* the normal size (a piece drawn again) */
	if (framed)
		box(dx, dy, dw, dh);
	else
		rect_fill(dx, dy, dx + dw - 1, dy + dh - 1, 0);

	y = dy + lh;
#ifndef NO_ICON256
	if (ab_colour == 2) {
		icon256_draw(a->icon256, map256, dx + (dw - ICON_W * sx) / 2, y, sx, sy);
	} else
#endif
	if (ab_colour) {
		about_icon16(a->icon16, dx + (dw - ICON_W * sx) / 2, y, sx, sy, 0);
	} else if (a->icon) {
		draw_icon(a->icon, dx + (dw - ICON_W * sx) / 2, y, sx, sy);
	}
	y += ih + g;
	center(y, a->name, TX_BOLD);
	{
		/* underlined, like EMail's and Claude ST's names */
		short n = (short)strlen(a->name), x = on_byte(dx + (dw - n * cw) / 2);
		line(x, y + ch - 1, x + n * cw - 1, y + ch - 1);
	}
	y += lh;
	snprintf(v, sizeof(v), "Version %s", a->version);
	center(y, v, 0);
	y += lh + g;
	center(y, a->tagline, 0);
	y += lh + g;
	line(dx + 4 * cw, y - lh / 4, dx + dw - 4 * cw, y - lh / 4);
	y += lh / 4;
	center(y, "Created by AERA | Erez Yaary", TX_BOLD);
	y += lh;
	center(y, "\xBD 2026 AERA | Erez Yaary", 0);
	y += lh + g;
	for (i = 0; i < nst; i++, y += lh)
		center(y, a->status[i], 0);
	if (nst)
		y += g;
	button(dx + (dw - bw) / 2, by, bw, bh, "OK");
	/* the fine print, last: the small size, then the normal one again */
	y += g;
	vst_height(vdi_h, ch >= 16 ? 6 : 4, &fcw, &fch);
	vswr_mode(vdi_h, MD_TRANS);
	vst_color(vdi_h, 1);
	vst_effects(vdi_h, 0);
	for (i = 0; i < nfine; i++, y += fstep) {
		const char *s = i ? a->fine[i - 1] : GPL_LINE;
		short n = (short)strlen(s);
		v_gtext_n(vdi_h, on_byte(dx + (dw - n * fcw) / 2), y, s, n);
	}
	(void)ab_fcw;
}

/* a piece of a box in a dialog window drawn again (dialog.c asks) */
static void (*box_draw)(int framed);
static void box_piece(short x, short y, short w, short h)
{
	GRECT r;
	short d;
	dlg_area(&dx, &dy, &d, &d);	/* where the window has it now */
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	box_draw(0);
	clip_off();
	graf_mouse(M_ON, 0);
}

/* ---- the box drawn in memory and shown at once ----
 * It is many fills, lines and texts; drawn on the screen, a stock machine
 * showed them appearing one by one (Erez, 26 Oct 2026). TOS's VDI draws at
 * the logical screen's base, so while the box is drawn that base is put in
 * a buffer holding the box's rows (the screen under it first), and the
 * buffer is copied to the screen in one raster copy. Only under TOS's own
 * single-tasking AES (the box is a window under MiNT and MagiC), the
 * logical and shown screens the same, and once a test pixel has landed in
 * the buffer; otherwise, or without the memory, it is drawn on the screen
 * as before. */
static long xb_setscreen(void *log)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w #-1,-(%%sp)\n\tmove.l #-1,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
			 "move.w #5,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(log) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

static int off_ok;		/* 1 the logical screen moves the VDI's drawing, -1 it doesn't */

/* the screen's bytes a line, as the VDI has them (Line-A's v_lin_wr) */
static long screen_bpl(void)
{
	register const short *la __asm__("a0");
	__asm__ volatile(".dc.w 0xa000" : "=r"(la) : : "d0", "d1", "d2", "a1", "a2", "memory", "cc");
	return la[1];		/* v_lin_wr */
}

static void rows_copy(unsigned char *d, long ds, const unsigned char *s, long ss, short rows, long n)
{
	for (; rows > 0; rows--, d += ds, s += ss)
		memcpy(d, s, n);
}

/* the box's rows: the screen under it kept (to put back) and the box
   drawn in a buffer, then copied to the screen; plain memory copies, the
   screen being TOS's own (on an ST they are some six times the speed of
   the VDI's raster copies, and no workstation is opened for them) */
static struct {
	unsigned char *screen, *under;
	long bpl, off, n;
	short rows;
} ob;

extern short _is_acc;		/* crt0.S: started as a desk accessory */

static void buf_free(void *p)
{
	if (_is_acc)
		free(p);
	else
		Mfree(p);
}

static int draw_off(void (*draw)(int framed))
{
	short x0 = dx & ~15, x1 = (dx + dw + 3 + 15) & ~15;
	unsigned char *buf, *base, *scr = (unsigned char *)trap14_w(3);	/* Logbase */
	long size;
	GRECT c;
	if (off_ok < 0 || planes < 1 || scr != (unsigned char *)trap14_w(2) || dy + dh + 3 > scr_h ||
	    x1 > scr_w)
		return 0;
	ob.bpl = screen_bpl();
	if (ob.bpl < (long)((scr_w + 15) / 16) * 2 * planes)
		return 0;
	ob.rows = dh + 3;
	ob.off = (long)(x0 >> 4) * planes * 2;
	ob.n = (long)((x1 - x0) >> 4) * planes * 2;
	ob.screen = scr + (long)dy * ob.bpl + ob.off;
	size = ob.bpl * ob.rows;
	if (_is_acc) {
		/* an accessory takes no GEMDOS memory once it runs (the
		   guidelines, 2.2): its heap's, in ST RAM (the blitter's), or
		   the box drawn on the screen */
		buf = malloc(size);
		if (buf && (unsigned long)buf + size > 0x01000000UL) {
			free(buf);
			buf = 0;
		}
	} else {
		buf = Mxalloc(size, 0);		/* ST RAM, where the blitter reaches */
		if ((long)buf == -32 || !buf)	/* no Mxalloc: Malloc's is ST RAM */
			buf = Malloc(size);
		if ((long)buf <= 0)
			buf = 0;
	}
	if (!buf)
		return 0;
	ob.under = malloc(ob.n * ob.rows);
	if (!ob.under) {
		buf_free(buf);
		return 0;
	}
	rows_copy(ob.under, ob.n, ob.screen, ob.bpl, ob.rows, ob.n);
	base = buf - (long)dy * ob.bpl;	/* the screen's row dy at the buffer's start */
	if (!off_ok) {
		/* does the VDI draw where the logical screen is, at our idea of
		   a line's length? the box's top left pixel (the frame's, drawn
		   over anyway) and its bottom left, in one pen and then the
		   other, tell */
		short n = planes * 2 > 32 ? 32 : planes * 2, pen, k;
		off_ok = 1;
		for (k = 0; k < 2 && off_ok > 0; k++) {
			short y = k ? dy + dh - 1 : dy;
			unsigned char was[32], *at = buf + (long)(y - dy) * ob.bpl + (long)(dx >> 4) * planes * 2;
			GRECT px;
			px.x = dx;
			px.y = y;
			px.w = 1;
			px.h = 1;
			off_ok = -1;
			for (pen = 1; pen >= 0 && off_ok < 0; pen--) {
				memcpy(was, at, n);
				xb_setscreen(base);
				clip_on(&px);
				fill(&px, pen);
				clip_off();
				xb_setscreen(scr);
				if (memcmp(was, at, n))
					off_ok = 1;
			}
		}
		if (off_ok < 0) {
			rows_copy(ob.screen, ob.bpl, ob.under, ob.n, ob.rows, ob.n);	/* a test pixel on the screen */
			free(ob.under);
			buf_free(buf);
			return 0;
		}
	}
	{
		/* where the box leaves the screen showing: the shadow's rows
		   in whole, and the words at its edges, a word at a time (a
		   memcpy a row took an ST 60 ms) */
		short r, i, g = planes, lw = (dx & 15) ? g : 0, rw = (short)(ob.n >> 1) - 2 * g;
		unsigned short *d = (unsigned short *)(buf + ob.off);
		const unsigned short *u = (const unsigned short *)ob.under;
		long ds = ob.bpl >> 1, us = ob.n >> 1;
		for (r = 0; r < ob.rows; r++, d += ds, u += us) {
			if (r < 3 || r >= ob.rows - 3) {
				memcpy(d, u, ob.n);
				continue;
			}
			for (i = 0; i < lw; i++)
				d[i] = u[i];
			if (rw > 0)
				for (i = rw; i < rw + 2 * g; i++)
					d[i] = u[i];
		}
	}
	c.x = dx;
	c.y = dy;
	c.w = dw + 3;
	c.h = ob.rows;
	xb_setscreen(base);
	clip_on(&c);				/* nothing past the buffer's rows */
	draw(1);
	clip_off();
	xb_setscreen(scr);
	rows_copy(ob.screen, ob.bpl, buf + ob.off, ob.bpl, ob.rows, ob.n);
	buf_free(buf);
	return 1;
}

/* the screen under the box put back */
static void off_back(void)
{
	graf_mouse(M_OFF, 0);
	rows_copy(ob.screen, ob.bpl, ob.under, ob.n, ob.rows, ob.n);
	graf_mouse(M_ON, 0);
	free(ob.under);
	ob.under = 0;
}

/* the box shown, run and put away: over the screen on TOS, in a window of
   the AES's on MiNT and MagiC (the guidelines, 3.5) */
static void box_run(void (*draw)(int framed), const char *name)
{
	void *under;
	box_draw = draw;
	if (dlg_windowed()) {
		short x, y, w = dw, h = dh;
		dlg_name(name);
		dlg_open_area(box_piece, &x, &y, &w, &h);
		if (dlg_in_window()) {
			wait_close();
			dlg_close();
			return;
		}
		dlg_close();		/* no window to be had: as on TOS */
	}
	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(M_OFF, 0);
	if (draw_off(draw)) {
		graf_mouse(M_ON, 0);
		wait_close();
		off_back();
		wind_update(2);
		wind_update(END_UPDATE);
		return;
	}
	graf_mouse(M_ON, 0);
	under = dlg_under(dx, dy, dw + 3, dh + 3);	/* put back, not redrawn */
	form_dial(FMD_START, dx, dy, dw + 3, dh + 3);
	graf_mouse(M_OFF, 0);
	clip_screen();
	draw(1);
	graf_mouse(M_ON, 0);
	wait_close();
	if (under)
		dlg_under_back(under);
	wind_update(2);
	wind_update(END_UPDATE);
	if (!under)
		form_dial(FMD_FINISH, dx, dy, dw + 3, dh + 3);
}

void about_show(const ABOUT *a)
{
	static char title[40];
	short sx = 2, sy = gl_hchar >= 16 ? 2 : 1;
	short ih, lh, i, nst = 0, widest = 0, nfine, fcw, fch, fstep, g;
	short saved_rgb[8][3], colour;
	short bw, bh;

	font_sys_push();
	ih = ICON_H * sy;
	lh = ch + (ch >= 16 ? 2 : 1);
	colour = planes >= 4 && a->icon16 && a->icon16_rgb;
#ifndef NO_ICON256
	if (planes >= 8 && a->icon256 && a->icon256_rgb)
		colour = 2;
#endif
	bw = 10 * cw;
	bh = ch + 6;
	for (i = 0; i < 3 && a->status[i]; i++) {
		if ((short)strlen(a->status[i]) > widest)
			widest = (short)strlen(a->status[i]);
		nst++;
	}
	dw = 50 * cw;
	if (widest + 4 > 50)			/* the status lines fit whole */
		dw = (widest + 4) * cw;
	/* the fine print: the system font's small size, 8x8 on a high
	   screen, 6x6 on a low one; measured, then back to the normal size
	   (font_sys_push's: 13 or 6) */
	vst_height(vdi_h, ch >= 16 ? 6 : 4, &fcw, &fch);
	vst_height(vdi_h, ch >= 16 ? 13 : 6, &fch, &fch);
	fstep = ch >= 16 ? 8 + 3 : 6 + 2;
	nfine = 1;
	widest = (short)strlen(GPL_LINE);
	for (i = 0; i < 3 && a->fine[i]; i++) {
		if ((short)strlen(a->fine[i]) > widest)
			widest = (short)strlen(a->fine[i]);
		nfine++;
	}
	if ((widest + 2) * fcw > dw)		/* and so does the fine print */
		dw = (widest + 2) * fcw;
	if (dw > desk_w - 2 * cw)
		dw = desk_w - 2 * cw;
	/* the gaps between the parts: half a line, less on a short screen
	   (640x200: the fine print ran into OK), so that the box and its
	   3-pixel shadow fit the desk */
	for (g = lh / 2;; g = g > 1 ? g / 2 : 0) {
		dh = lh + ih + g + lh + (lh + g) + (lh + g) + lh / 4 + lh + (lh + g) +
		     (nst ? nst * lh + g : 0) + g + nfine * fstep + g + bh + lh;
		if (dh <= desk_h - 6 || !g)
			break;
	}
	if (dh > desk_h - 6)
		dh = desk_h - 6;
	dx = (desk_x + (desk_w - dw) / 2 + 8) & ~15;	/* on a word: its text on bytes */
	dy = desk_y + (desk_h - dh) / 2;

	ab = a;
	ab_sx = sx, ab_sy = sy, ab_ih = ih, ab_lh = lh, ab_g = g, ab_nst = nst;
	ab_nfine = nfine, ab_fstep = fstep, ab_fcw = fcw, ab_bw = bw, ab_bh = bh, ab_colour = colour;
#ifndef NO_ICON256
	if (colour == 2)
		icon256_pens(a->icon256_rgb, map256);
	else
#endif
	if (colour) {
		for (i = 0; i < 8; i++) {
			vq_color(vdi_h, 8 + i, saved_rgb[i]);
			vs_color(vdi_h, 8 + i, a->icon16_rgb[i]);
		}
	}
	snprintf(title, sizeof(title), "About %s", a->name);
	box_run(about_draw, title);
	if (colour == 1)
		for (i = 0; i < 8; i++)
			vs_color(vdi_h, 8 + i, saved_rgb[i]);
	font_sys_pop();
}

/* ---------------- a box of plain text ---------------- */

#define TB_LINES 40

static const char *tb_title;
static char (*tb_buf)[82];
static short tb_n, tb_cols, tb_lh, tb_bw, tb_bh;

static void tb_draw(int framed)
{
	short y = dy + tb_lh, i;
	if (framed)
		box(dx, dy, dw, dh);
	else
		rect_fill(dx, dy, dx + dw - 1, dy + dh - 1, 0);
	center(y, tb_title, TX_BOLD);
	y += tb_lh + tb_lh / 2;
	for (i = 0; i < tb_n; i++, y += tb_lh)
		text_at(dx + 2 * cw, y, tb_buf[i], (long)strlen(tb_buf[i]), tb_cols, TX_LTR);
	button(dx + (dw - tb_bw) / 2, dy + dh - tb_bh - tb_lh, tb_bw, tb_bh, "OK");
}

void text_box(const char *title, const char *text)
{
	char (*buf)[82] = malloc(TB_LINES * sizeof(*buf));	/* only while it shows */
	short n = 0, cols, widest, lh, i, bw, bh;
	const char *p = text;

	if (!buf) {
		alert(1, "[1][Not enough memory to show|%s.][ OK ]", title);
		return;
	}
	font_sys_push();
	cols = desk_w / cw - 6;
	if (cols > 80)
		cols = 80;
	/* the lines, long ones broken at a space */
	while (*p && n < TB_LINES) {
		const char *e = p;
		short k = 0, cut = -1;
		while (*e && *e != '\r' && *e != '\n' && k < cols) {
			if (*e == ' ')
				cut = k;
			e++;
			k++;
		}
		if (*e && *e != '\r' && *e != '\n' && cut > cols / 2) {
			k = cut;
			e = p + cut + 1;	/* the space goes */
		}
		memcpy(buf[n], p, k);
		buf[n++][k] = 0;
		p = e;
		if (*p == '\r')
			p++;
		if (*p == '\n')
			p++;
	}
	widest = (short)strlen(title);
	for (i = 0; i < n; i++)
		if ((short)strlen(buf[i]) > widest)
			widest = (short)strlen(buf[i]);
	lh = ch + (ch >= 16 ? 1 : 0);
	bw = 10 * cw;
	bh = ch + 6;
	if (n > (desk_h - 6 - 4 * lh - lh / 2 - bh) / lh)	/* the box and its shadow fit */
		n = (desk_h - 6 - 4 * lh - lh / 2 - bh) / lh;
	dw = (widest + 4) * cw;
	if (dw < 30 * cw)
		dw = 30 * cw;
	dh = lh + lh + lh / 2 + n * lh + lh + bh + lh;
	dx = desk_x + (desk_w - dw) / 2;
	dy = desk_y + (desk_h - dh) / 2;

	tb_title = title;
	tb_buf = buf;
	tb_n = n;
	tb_cols = cols;
	tb_lh = lh;
	tb_bw = bw;
	tb_bh = bh;
	box_run(tb_draw, title);
	font_sys_pop();
	free(buf);
}

/* ---------------- after a crash ---------------- */

#define CRASH_TAIL 4096		/* the end of CRASH.LOG that is read */

void crash_offer(const char *app, const char *where)
{
	char *text, *last, *q;
	long h, n, size;
	if (!crash_previous())
		return;
	if (alert(1, "[1][%s crashed last time|(%s).|What happened is in|CRASH.LOG, %s.][Show|OK]",
		  app, crash_previous(), where) != 1)
		return;
	/* the end of CRASH.LOG, and in it the last report; the memory only
	   while it shows (it was 4 KB of every app's BSS, and the box's 3 KB) */
	h = Fopen(crash_log(), 0);
	if (h < 0) {
		alert(1, "[1][Can't read CRASH.LOG.][ OK ]");
		return;
	}
	if (!(text = malloc(CRASH_TAIL))) {
		Fclose((short)h);
		alert(1, "[1][Not enough memory to show|CRASH.LOG.][ OK ]");
		return;
	}
	size = Fseek(0, (short)h, 2);
	Fseek(size > CRASH_TAIL - 1 ? size - (CRASH_TAIL - 1) : 0, (short)h, 0);
	n = Fread((short)h, CRASH_TAIL - 1, text);
	Fclose((short)h);
	if (n < 0)
		n = 0;
	text[n] = 0;
	last = text;
	for (q = text; (q = strstr(q, "---- ")) != 0; q++)
		if (q == text || q[-1] == '\n')
			last = q;
	text_box("CRASH.LOG", last);
	free(text);
}
