/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * panebar.c - a pane's own scroll bar, down its right side or across its
 * foot, of AES objects (panebar.h; the guidelines, 5.1). The click
 * handling is EContact's list bar's (2d1e7f8d), made the family's. Every
 * position here is along the bar: y for a bar down, x for one across.
 */
#include "tos.h"
#include "gemui.h"
#include "dialog.h"		/* dlg_3d_ok, dlg_3d_on */
#include "panebar.h"

static short box_w, box_h, row_h;

/* a bar down as wide as the AES's own slider column, one across as high
   as its slider row: what a slider adds to a window's border (wind_calc),
   and the line it shares with the border (TOS 1.04 and 4, XaAES: 19, 23
   and 20 pixels in ST high and VGA; graf_handle's box is XaAES's
   character cell, 10). An arrow box is graf_handle's box high in a bar
   down, and as long as the bar is wide in one across */
static void sizes(void)
{
	short wc, hc, x0, y0, w0, h0, x1, y1, w1, h1;
	if (box_w)
		return;
	graf_handle(&wc, &hc, &box_w, &box_h);
	row_h = box_h;
	if (!wind_calc(0, 0, 100, 100, 200, 200, &x0, &y0, &w0, &h0))
		return;
	if (wind_calc(0, VSLIDE | UPARROW | DNARROW, 100, 100, 200, 200, &x1, &y1, &w1, &h1) &&
	    w1 - w0 + 1 > box_w)
		box_w = w1 - w0 + 1;
	if (wind_calc(0, HSLIDE | LFARROW | RTARROW, 100, 100, 200, 200, &x1, &y1, &w1, &h1) &&
	    h1 - h0 + 1 > row_h)
		row_h = h1 - h0 + 1;
}

short pb_width(void)
{
	sizes();
	return box_w;
}

short pb_height(void)
{
	sizes();
	return row_h;
}

short pb_arrow_h(void)
{
	sizes();
	return box_h;
}

/* along the bar: where it starts, how long it is, an arrow box's length */
static short start(const PANEBAR *b)
{
	return b->across ? b->r.x : b->r.y;
}

static short length(const PANEBAR *b)
{
	return b->across ? b->r.w : b->r.h;
}

static short arrow(const PANEBAR *b)
{
	return b->across ? pb_width() : pb_arrow_h();
}

/* the track between the arrows, and the slider in it */
void pb_slider(const PANEBAR *b, short *ty, short *th, short *sy, short *sh)
{
	short ah = arrow(b), least = ah / 2 > 4 ? ah / 2 : 4;
	*ty = start(b) + ah;
	*th = length(b) - 2 * ah;
	*sy = *ty;
	*sh = 0;
	if (b->total <= b->shown || *th <= 0)
		return;
	*sh = (short)((long)*th * b->shown / b->total);
	if (*sh < least)
		*sh = least;
	if (*sh > *th)
		*sh = *th;
	*sy = *ty + (short)((long)(*th - *sh) * b->top / (b->total - b->shown));
}

long pb_top_at(const PANEBAR *b, short p)
{
	short ty, th, sy, sh;
	long range = b->total - b->shown;
	pb_slider(b, &ty, &th, &sy, &sh);
	if (range <= 0 || th <= sh)
		return 0;
	if (p < ty)
		p = ty;
	if (p > ty + th - sh)
		p = ty + th - sh;
	return ((long)(p - ty) * range + (th - sh) / 2) / (th - sh);
}

int pb_part(const PANEBAR *b, short x, short y)
{
	short ah = arrow(b), p = b->across ? x : y, ty, th, sy, sh;
	if (x < b->r.x || x >= b->r.x + b->r.w || y < b->r.y || y >= b->r.y + b->r.h)
		return PB_NONE;
	if (p < start(b) + ah)
		return PB_UP;
	if (p >= start(b) + length(b) - ah)
		return PB_DOWN;
	pb_slider(b, &ty, &th, &sy, &sh);
	if (!sh)
		return PB_NONE;
	return p < sy ? PB_PAGE_UP : p >= sy + sh ? PB_PAGE_DOWN : PB_SLIDER;
}

/* the bar as AES objects: the box (its frame), the arrows, the track and
   the slider in it; the frames lie on each other, so the lines are single */
enum { O_BAR, O_UP, O_TRACK, O_SLIDER, O_DOWN, O_N };

/* an object at a along the bar and c across it, al long and cl wide, from
   a pixel in from the bar's edge (in 3D the box is the bar itself: d3) */
static void put(OBJECT *o, int across, int d3, short a, short c, short al, short cl)
{
	a += d3;
	c += d3;
	o->ob_x = across ? a : c;
	o->ob_y = across ? c : a;
	o->ob_width = across ? al : cl;
	o->ob_height = across ? cl : al;
}

/* the clip within r: a raised object's edge goes past its box */
static int within(GRECT *c, const GRECT *r)
{
	short x2 = c->x + c->w < r->x + r->w ? c->x + c->w : r->x + r->w;
	short y2 = c->y + c->h < r->y + r->h ? c->y + c->h : r->y + r->h;
	if (c->x < r->x)
		c->x = r->x;
	if (c->y < r->y)
		c->y = r->y;
	c->w = x2 - c->x;
	c->h = y2 - c->y;
	return c->w > 0 && c->h > 0;
}

/* the box: the bar's ground. Flat, every frame is outside its box
   (thickness -1, 0xFF: GEM's positive thickness is inside, and TOS fills
   an opaque box over a frame inside it), and the box is a pixel in from
   r's edge, so its frame is r's edge. In 3D the frames are inside (the AES
   draws a 3D object's edges in its box, and an outside frame came out a
   plain black line) and the box is r itself: nothing else would draw r's
   edge (EmuTOS showed the desktop there, EUpdate found) */
static void ground(OBJECT *o, const GRECT *r, int d3)
{
	o->ob_next = o->ob_head = o->ob_tail = -1;
	o->ob_state = 0;
	o->ob_flags = d3 ? 0x0400 : 0;		/* FL3DBAK: the AES's ground */
	o->ob_type = G_BOX;
	o->ob_spec = d3 ? 0x00011100L : 0x00FF1180L;	/* 3D: the AES's ground (it greys only
							   a transparent box); flat: white */
	o->ob_x = r->x + !d3;
	o->ob_y = r->y + !d3;
	o->ob_width = r->w - 2 * !d3;
	o->ob_height = r->h - 2 * !d3;
}

/* 3D where the AES draws its windows' own sliders in 3D: MagiC, XaAES,
   N.AES and MyAES (the AESes that run several programs). TOS 4 and
   EmuTOS draw dialogs in 3D but a window's sliders flat (a dotted track,
   a white slider), and a pane bar beside the window's own looks as that
   one does (Erez's EContact on TOS 4.04, 27 Oct 2026: two kinds of bar
   side by side). Whatever else sits in a window (a toolbar's buttons)
   takes the same look: EFTP's toolbar, 27 Oct 2026 */
int win_look_3d(void)
{
	return dlg_3d_ok() && dlg_3d_on && (aes_count() != 1 || get_cookie(0x4D616758L));	/* MagX */
}

/* ---------------- the slider's move by the VDI (pb_vdi) ---------------- */

static short bar_h;		/* the app's workstation (pb_vdi), 0 none */
static int fast;		/* 0 not checked yet, 1 on, -1 off for the run */
static long fast_n;

/* text.c's, when the program has it */
extern int text_plain_state(void) __attribute__((weak));
extern void text_reclip(short h) __attribute__((weak));
extern unsigned char *scr_at(long *bpl, short *planes, short *w, short *h) __attribute__((weak));
extern void scr_fill(short h, const short *p, short pen) __attribute__((weak));
extern int scr_pattern(short h, const short *p, const unsigned short *pat) __attribute__((weak));

/* the track's pattern as the screen has it (pat_learn): row y's sixteen
   pixels tpat[y & 15]; tpat_ok 0 not read yet, 1 read and checked, -1
   not to be had (the VDI fills it then); for the colour word tpat_spec */
static unsigned short tpat[16], tpat_spec;
static short tpat_ok;

void pb_vdi(short h)
{
	bar_h = h;
}

int pb_vdi_state(void)
{
	return fast;
}

long pb_vdi_count(void)
{
	return fast_n;
}

int pb_vdi_pattern(void)
{
	return tpat_ok;
}

/* the VDI's fill attributes as fast_draw last set them (-1: not known,
   each draw sets them afresh): a call costs a 68000 some 0.3 ms */
static short at_mode, at_int, at_style, at_col;
static GRECT at_clip;		/* the box fast_draw draws in */

/* from along a1 to a2 and across k1 to k2, filled as the AES fills an
   object whose ob_spec has colour word spec: its interior colour, pattern
   (0 hollow, 7 solid, else the VDI's pattern of that number) and mode
   (opaque or transparent) */
static void fill_as(int across, unsigned short spec, short a1, short k1, short a2, short k2)
{
	short p[4], pat = (spec >> 4) & 7, mode = (spec & 0x80) ? 1 : 2, in = pat == 0 ? 0 : pat == 7 ? 1 : 2;
	if (a1 > a2 || k1 > k2)
		return;
	p[0] = across ? a1 : k1;
	p[1] = across ? k1 : a1;
	p[2] = across ? a2 : k2;
	p[3] = across ? k2 : a2;
	if (mode == 1 && (in == 0 || (in == 1 && (spec & 15) <= 1))) {
		/* white or black, solid: by the CPU (scr_fill), cut to the box
		   here, as the VDI's clip would */
		if (p[0] < at_clip.x)
			p[0] = at_clip.x;
		if (p[1] < at_clip.y)
			p[1] = at_clip.y;
		if (p[2] > at_clip.x + at_clip.w - 1)
			p[2] = at_clip.x + at_clip.w - 1;
		if (p[3] > at_clip.y + at_clip.h - 1)
			p[3] = at_clip.y + at_clip.h - 1;
		if (p[0] <= p[2] && p[1] <= p[3])
			scr_fill(bar_h, p, in == 0 ? 0 : spec & 15);
		return;
	}
	if (in == 2 && mode == 1 && tpat_ok > 0 && spec == tpat_spec && scr_pattern) {
		/* the track's pattern, read off the screen, by the CPU */
		if (p[0] < at_clip.x)
			p[0] = at_clip.x;
		if (p[1] < at_clip.y)
			p[1] = at_clip.y;
		if (p[2] > at_clip.x + at_clip.w - 1)
			p[2] = at_clip.x + at_clip.w - 1;
		if (p[3] > at_clip.y + at_clip.h - 1)
			p[3] = at_clip.y + at_clip.h - 1;
		if (p[0] > p[2] || p[1] > p[3] || scr_pattern(bar_h, p, tpat))
			return;
	}
	if (mode != at_mode)
		vswr_mode(bar_h, at_mode = mode);
	if (in != at_int)
		vsf_interior(bar_h, at_int = in);
	if (in == 2 && pat != at_style)
		vsf_style(bar_h, at_style = pat);
	if ((spec & 15) != at_col)
		vsf_color(bar_h, at_col = spec & 15);
	vr_recfl(bar_h, p);
}

/* a frame's line, in the colour word's frame colour */
static void line_as(int across, unsigned short spec, short a1, short k1, short a2, short k2)
{
	fill_as(across, (unsigned short)(0x00F0 | ((spec >> 12) & 15)), a1, k1, a2, k2);
}

/* the fast way can be: the app's workstation, TOS's own screen (text.c's
   text_plain on), not found wrong, and the track and slider framed a
   pixel outside them (thickness -1, the flat look's) */
static int fast_can(const OBJECT *t)
{
	return bar_h > 0 && fast >= 0 && text_plain_state && text_reclip && scr_at && scr_fill && text_plain_state() == 1 &&
	       ((t[O_TRACK].ob_spec >> 16) & 0xFF) == 0xFF && ((t[O_SLIDER].ob_spec >> 16) & 0xFF) == 0xFF;
}

/* c cut to the track and its end lines along the bar: 0 when nothing is
   left */
static int in_track(const PANEBAR *b, GRECT *c, short ty, short th)
{
	short *a = b->across ? &c->x : &c->y, *l = b->across ? &c->w : &c->h;
	short a2 = *a + *l, t2 = ty + th + 1;
	if (*a < ty - 1)
		*a = ty - 1;
	if (a2 > t2)
		a2 = t2;
	*l = a2 - *a;
	return *l > 0;
}

/* a bar down's box c (inside the track and its end lines) a row at a
   time straight into the screen: across it the edges black and between
   them the track's pattern (pat_learn), or the slider's white, or black
   on the track's end lines and the slider's frame; what fast_draw's fills
   and lines put there, in one pass (as fills, a track's rows each paid
   box_op's turn some four times over: 53 ms of an 8 MHz ST). 0 when it
   can't be this way: the pattern not read, the colours not black and
   white */
static int down_rows(const PANEBAR *b, const GRECT *c, const OBJECT *t, short ty, short th, short sy, short sh)
{
	unsigned short ts = (unsigned short)t[O_TRACK].ob_spec, ss = (unsigned short)t[O_SLIDER].ob_spec;
	unsigned short im[3], em[3], wm[3];
	short k1 = b->r.x, k2 = b->r.x + b->r.w - 1, x0 = c->x, x1 = c->x + c->w - 1, g0, g, n, y, planes, sw, shh, k;
	long bpl;
	unsigned char *base, *row;
	if (b->across || tpat_ok <= 0 || ts != tpat_spec || (ts & 0xF000) != 0x1000 || ss != 0x1180 || !scr_at)
		return 0;
	if (x0 < k1)
		x0 = k1;
	if (x1 > k2)
		x1 = k2;
	base = scr_at(&bpl, &planes, &sw, &shh);
	if (!base || x0 > x1 || c->y < 0 || c->y + c->h > shh || x1 >= sw)
		return 0;
	g0 = k1 >> 4;
	n = (short)((k2 >> 4) - g0 + 1);
	if (n > 3)
		return 0;
	for (g = 0; g < 3; g++) {
		short gx = (short)((g0 + g) << 4), x;
		im[g] = em[g] = wm[g] = 0;
		for (x = gx; x < gx + 16 && g < n; x++) {
			unsigned short bit = (unsigned short)(0x8000u >> (x & 15));
			if (x == k1 || x == k2)
				em[g] |= bit;
			else if (x > k1 && x < k2)
				im[g] |= bit;
			if (x >= x0 && x <= x1)
				wm[g] |= bit;
		}
	}
	row = base + (long)c->y * bpl + (long)g0 * planes * 2;
	if (n == 2) {
		/* a bar 16 to 32 pixels wide over two groups (every bar of TOS's
		   size): the masks in registers, each plane's pair of words */
		register unsigned short i0 = im[0], i1 = im[1], e0 = em[0], e1 = em[1], w0 = wm[0], w1 = wm[1];
		register unsigned short n0 = (unsigned short)~w0, n1 = (unsigned short)~w1;
		short y1 = (short)(c->y + c->h), ta = ty - 1, tb = ty + th, sa = sh ? sy - 1 : -32000,
		      sb = sh ? sy + sh : -32000;
		for (y = c->y; y < y1; y++, row += bpl) {
			unsigned short inner, v0, v1, *d = (unsigned short *)row;
			if (y == ta || y == tb || y == sa || y == sb)
				inner = 0xFFFF;
			else if (y > sa && y < sb)
				inner = 0;
			else
				inner = tpat[y & 15];
			v0 = (unsigned short)(((inner & i0) | e0) & w0);
			v1 = (unsigned short)(((inner & i1) | e1) & w1);
			for (k = 0; k < planes; k++)
				d[k] = (unsigned short)((d[k] & n0) | v0);
			d += planes;
			for (k = 0; k < planes; k++)
				d[k] = (unsigned short)((d[k] & n1) | v1);
		}
		fast_n++;
		return 1;
	}
	for (y = c->y; y < c->y + c->h; y++, row += bpl) {
		unsigned short inner, *d = (unsigned short *)row;
		if (y == ty - 1 || y == ty + th || (sh && (y == sy - 1 || y == sy + sh)))
			inner = 0xFFFF;
		else if (sh && y >= sy && y < sy + sh)
			inner = 0;
		else
			inner = tpat[y & 15];
		for (g = 0; g < n; g++, d += planes) {
			unsigned short v = (unsigned short)(((inner & im[g]) | em[g]) & wm[g]), m = (unsigned short)~wm[g];
			for (k = 0; k < planes; k++)
				d[k] = (unsigned short)((d[k] & m) | v);
		}
	}
	fast_n++;
	return 1;
}

/* the box c, inside the track and its end lines (in_track), as the AES
   draws it in the flat look: the track's fill between the bar's edges,
   the edges and its end lines in its frame's colour, and over them the
   slider's fill and frame. White and black by the CPU (scr_fill: the
   caller's clip, text_clip's, holds the box, as for any redraw), the
   track's pattern by the VDI */
static void fast_draw(const PANEBAR *b, const GRECT *c, const OBJECT *t, short ty, short th, short sy, short sh)
{
	int across = b->across != 0;
	short a1 = across ? c->x : c->y, a2 = a1 + (across ? c->w : c->h) - 1;
	short k1 = across ? b->r.y : b->r.x, k2 = k1 + (across ? b->r.h : b->r.w) - 1;
	unsigned short ts = (unsigned short)t[O_TRACK].ob_spec, ss = (unsigned short)t[O_SLIDER].ob_spec;
	short cl[4], was[5];
	if (down_rows(b, c, t, ty, th, sy, sh))
		return;
	/* the app's own fill and writing mode, put back at the end: ECal,
	   EContact and EMail keep what they set last and don't set it again */
	vqf_attributes(bar_h, was);
	cl[0] = c->x;
	cl[1] = c->y;
	cl[2] = c->x + c->w - 1;
	cl[3] = c->y + c->h - 1;
	vs_clip(bar_h, 1, cl);
	at_clip = *c;
	at_mode = at_int = at_style = at_col = -1;
	fill_as(across, ts, a1, k1 + 1, a2, k2 - 1);
	line_as(across, ts, a1, k1, a2, k1);
	line_as(across, ts, a1, k2, a2, k2);
	if (a1 < ty)
		line_as(across, ts, ty - 1, k1, ty - 1, k2);
	if (a2 >= ty + th)
		line_as(across, ts, ty + th, k1, ty + th, k2);
	if (sh) {
		fill_as(across, ss, sy, k1 + 1, sy + sh - 1, k2 - 1);
		line_as(across, ss, sy - 1, k1, sy - 1, k2);
		line_as(across, ss, sy + sh, k1, sy + sh, k2);
		if ((ss ^ ts) & 0xF000) {
			/* its sides, unless the track's edges are them already */
			line_as(across, ss, sy, k1, sy + sh - 1, k1);
			line_as(across, ss, sy, k2, sy + sh - 1, k2);
		}
	}
	vsf_interior(bar_h, was[0]);
	vsf_color(bar_h, was[1]);
	vsf_style(bar_h, was[2]);
	vswr_mode(bar_h, was[3]);
	text_reclip(bar_h);
	fast_n++;
}

/* the track's pattern read off the screen, where the bar was just drawn
   in c (by the AES, or the VDI's pattern by the fast way): 16 rows by 16
   columns of track with no slider or frame in them, every plane the same
   (pen 1 on 0), then drawn there again by the CPU and the bytes compared.
   Nothing yet when c holds no such square (a short list's slider fills
   its track): the next whole bar tries again */
static unsigned long box_sum(const GRECT *c);

static void pat_learn(const PANEBAR *b, const GRECT *c, short ty, short th, short sy, short sh, unsigned short ts)
{
	int across = b->across != 0;
	short k1 = across ? b->r.y : b->r.x, k2 = k1 + (across ? b->r.h : b->r.w) - 1;
	short c0 = across ? c->y : c->x, c1 = c0 + (across ? c->h : c->w) - 1;
	short ca = across ? c->x : c->y, ce = ca + (across ? c->w : c->h) - 1;
	short run[2][2], i, a0 = -1, q[4], x, y, planes, sw, shh;
	long bpl;
	unsigned char *base;
	unsigned short pat[16];
	unsigned long was;
	GRECT r;
	if (tpat_ok || !scr_pattern || ((ts >> 4) & 7) == 0 || ((ts >> 4) & 7) == 7 || !(ts & 0x80) ||
	    (ts & 15) != 1 || k1 + 1 < c0 || k1 + 16 > c1 || k1 + 16 > k2 - 1)
		return;
	/* along: the track above and below the slider, its frame left out */
	run[0][0] = ty;
	run[0][1] = sh ? sy - 2 : ty + th - 1;
	run[1][0] = sh ? sy + sh + 1 : 1;
	run[1][1] = sh ? ty + th - 1 : 0;
	for (i = 0; i < 2 && a0 < 0; i++) {
		short s = run[i][0] > ca ? run[i][0] : ca, e = run[i][1] < ce ? run[i][1] : ce;
		if (e - s + 1 >= 16)
			a0 = s;
	}
	if (a0 < 0)
		return;
	r.x = across ? a0 : k1 + 1;
	r.y = across ? k1 + 1 : a0;
	r.w = r.h = 16;
	base = scr_at(&bpl, &planes, &sw, &shh);
	if (!base || r.x + 16 > sw || r.y + 16 > shh)
		return;
	for (i = 0; i < 16; i++)
		pat[i] = 0;
	for (y = r.y; y < r.y + 16; y++) {
		const unsigned short *row = (const unsigned short *)(base + (long)y * bpl);
		for (x = r.x; x < r.x + 16; x++) {
			const unsigned short *w = row + (x >> 4) * planes;
			unsigned short bit = (unsigned short)(0x8000u >> (x & 15));
			short k, on = (w[0] & bit) != 0;
			for (k = 1; k < planes; k++)
				if (((w[k] & bit) != 0) != on) {
					tpat_ok = -1;	/* not pen 1 on 0 */
					return;
				}
			if (on)
				pat[y & 15] |= (unsigned short)(0x8000u >> (x & 15));
		}
	}
	was = box_sum(&r);
	for (i = 0; i < 16; i++)
		tpat[i] = pat[i];
	q[0] = r.x;
	q[1] = r.y;
	q[2] = r.x + 15;
	q[3] = r.y + 15;
	if (!was || !scr_pattern(bar_h, q, tpat))
		return;
	tpat_spec = ts;
	tpat_ok = box_sum(&r) == was ? 1 : -1;
}

/* the part of c along the bar from a0 to a1 - 1, an arrow and its frame:
   its box white (the bar's ground, under the arrow's transparent box) by
   the CPU, then the arrow alone by the AES (its frame and its character);
   0 when the ground isn't white */
static int arrow_part(const PANEBAR *b, OBJECT *t, const GRECT *c, short obj, short a0, short a1)
{
	int across = b->across != 0;
	GRECT q = *c;
	short *a = across ? &q.x : &q.y, *l = across ? &q.w : &q.h, e = *a + *l;
	short k1 = across ? b->r.y : b->r.x, k2 = k1 + (across ? b->r.h : b->r.w) - 1;
	unsigned short g = (unsigned short)t[O_BAR].ob_spec;
	if ((g & 0xF0) != 0x80 || (g & 15))
		return 0;		/* not an opaque hollow ground (white) */
	if (*a < a0)
		*a = a0;
	if (e > a1)
		e = a1;
	*l = e - *a;
	if (*l <= 0)
		return 1;
	at_clip = q;
	fill_as(across, g, a0 + 1, k1 + 1, a1 - 2, k2 - 1);
	objc_draw(t, obj, 0, q.x, q.y, q.w, q.h);
	return 1;
}

/* the screen's bytes under c, summed (the first box's check) */
static unsigned long box_sum(const GRECT *c)
{
	long bpl;
	short planes, sw, sh, r, k, n, g0;
	unsigned char *base = scr_at(&bpl, &planes, &sw, &sh), *row;
	unsigned long s = 0, u = 0;
	if (!base || c->x < 0 || c->y < 0 || c->x + c->w > sw || c->y + c->h > sh)
		return 0;
	g0 = c->x >> 4;
	n = (short)((((c->x + c->w - 1) >> 4) - g0 + 1) * planes * 2);
	row = base + (long)c->y * bpl + (long)g0 * planes * 2;
	for (r = 0; r < c->h; r++, row += bpl)
		for (k = 0; k < n; k++) {
			s = ((s << 5) | (s >> 27)) ^ row[k];
			u += s;
		}
	return s ^ (u << 1) ^ 1;	/* never 0 */
}

void pb_draw(const PANEBAR *b, const GRECT *clip)
{
	OBJECT t[O_N];
	short ah = arrow(b), ty, th, sy, sh, i;
	short len = length(b), wide = b->across ? b->r.h : b->r.w;
	int d3 = win_look_3d(), across = b->across != 0;
	GRECT c = *clip;
	if (len < 2 * ah + 4 || !within(&c, &b->r))
		return;
	pb_slider(b, &ty, &th, &sy, &sh);
	ground(&t[O_BAR], &b->r, d3);
	for (i = O_UP; i < O_N; i++) {
		t[i].ob_next = t[i].ob_head = t[i].ob_tail = -1;
		t[i].ob_state = 0;
		t[i].ob_flags = 0;
	}
	t[O_BAR].ob_head = O_UP;
	t[O_BAR].ob_tail = O_DOWN;
	t[O_UP].ob_next = O_TRACK;
	t[O_TRACK].ob_next = O_DOWN;
	t[O_DOWN].ob_next = O_BAR;
	t[O_TRACK].ob_head = t[O_TRACK].ob_tail = O_SLIDER;
	t[O_SLIDER].ob_next = O_TRACK;
	t[O_DOWN].ob_flags = LASTOB;

	/* the parts lie in the box so that neighbours' frames are one line,
	   and nothing is drawn outside the bar. The arrows: the bar's first
	   and last ah pixels, frames included (in 3D the boxes themselves,
	   their edges in them); TOS's arrows, transparent */
	t[O_UP].ob_type = t[O_DOWN].ob_type = G_BOXCHAR;
	t[O_UP].ob_spec = across ? 0x04FF1100L : 0x01FF1100L;
	t[O_DOWN].ob_spec = across ? 0x03FF1100L : 0x02FF1100L;
	t[O_UP].ob_flags |= TOUCHEXIT;
	t[O_DOWN].ob_flags |= TOUCHEXIT;
	put(&t[O_UP], across, d3, -d3, -d3, ah - 2 + 2 * d3, wide - 2 + 2 * d3);
	put(&t[O_DOWN], across, d3, len - ah - d3, -d3, ah - 2 + 2 * d3, wide - 2 + 2 * d3);

	/* the track between them, its end frames on theirs: TOS's dotted grey
	   flat; in 3D the AES's ground with no frame of its own, so the bar is
	   one channel (its box's frame) with the raised arrows and slider in it
	   (framed, the track beside the slider read as a second box: EWeb) */
	t[O_TRACK].ob_type = G_BOX;
	t[O_TRACK].ob_spec = d3 ? 0x00001100L : 0x00FF1191L;
	put(&t[O_TRACK], across, d3, ah - 1, 0, th, wide - 2);

	/* the slider over the track's sy .. sy+sh-1 (flat, its frames one
	   over and under them, on the track's at either end) */
	t[O_SLIDER].ob_type = G_BOX;
	t[O_SLIDER].ob_spec = d3 ? 0x00011100L : 0x00FF1180L;	/* flat: white over the dots */
	t[O_SLIDER].ob_flags |= TOUCHEXIT;
	put(&t[O_SLIDER], across, 0, sh ? sy - ty : 0, 0, sh, wide - 2);	/* in the track */
	if (!sh)
		t[O_SLIDER].ob_flags |= HIDETREE;

	/* the 3D flags set here, not by dlg_3d: its rule for lists (a white
	   framed box stays white, 5.6) would take the track for one */
	if (d3) {
		t[O_TRACK].ob_flags |= 0x0400;		/* FL3DBAK: the ground */
		t[O_UP].ob_flags |= 0x0600;		/* FL3DACT: raised */
		t[O_DOWN].ob_flags |= 0x0600;
		t[O_SLIDER].ob_flags |= 0x0600;
	}
	if (!d3 && fast > 0 && fast_can(t)) {
		/* the track and the slider the fast way; an arrow in the box,
		   its ground white and the arrow alone by the AES (a whole bar
		   by objc_draw: 70 ms of an 8 MHz ST) */
		GRECT k = c;
		short ca = b->across ? c.x : c.y, ce = ca + (b->across ? c.w : c.h);
		short st = start(b), en = st + length(b);
		int ok = 1;
		if (ca < ty - 1)
			ok = arrow_part(b, t, &c, O_UP, st, ty - 1);
		if (ok && ce > ty + th + 1)
			ok = arrow_part(b, t, &c, O_DOWN, ty + th + 1, en);
		if (ok) {
			if (in_track(b, &k, ty, th)) {
				fast_draw(b, &k, t, ty, th, sy, sh);
				if (!tpat_ok)
					pat_learn(b, &k, ty, th, sy, sh, (unsigned short)t[O_TRACK].ob_spec);
			}
			return;
		}
	}
	objc_draw(t, O_BAR, 2, c.x, c.y, c.w, c.h);
	if (!d3 && !tpat_ok && fast_can(t)) {
		GRECT k = c;
		if (in_track(b, &k, ty, th))
			pat_learn(b, &k, ty, th, sy, sh, (unsigned short)t[O_TRACK].ob_spec);
	}
	if (!d3 && !fast && fast_can(t)) {
		/* the first time: a little of the track the AES drew (round the
		   slider), drawn again the VDI's way over it, the same bytes or
		   the AES's from now on (at the bar's first drawing, a window's
		   opening, not a scroll's; a long slider's all took an ST 35 ms) */
		GRECT k = c;
		unsigned long was;
		short *a = b->across ? &k.x : &k.y, *l = b->across ? &k.w : &k.h;
		short f = sh ? sy - 4 : ty, e = f + 12;	/* a little: the track, the slider's top and frame */
		if (*a + *l > e)
			*l = e - *a;
		if (*a < f) {
			*l -= f - *a;
			*a = f;
		}
		if (*l > 0 && in_track(b, &k, ty, th) && (was = box_sum(&k)) != 0) {
			fast_draw(b, &k, t, ty, th, sy, sh);
			fast = box_sum(&k) == was ? 1 : -1;
			if (fast < 0)
				objc_draw(t, O_BAR, 2, k.x, k.y, k.w, k.h);
		}
	}
}

void pb_corner(const GRECT *r, const GRECT *clip)
{
	OBJECT t[1];
	GRECT c = *clip;
	if (!within(&c, r))
		return;
	ground(&t[0], r, win_look_3d());
	t[0].ob_flags |= LASTOB;
	objc_draw(t, 0, 0, c.x, c.y, c.w, c.h);
}

GRECT pb_moved(const PANEBAR *b, long old_top)
{
	PANEBAR o = *b;
	GRECT r = b->r;
	short ty, th, sy, sh, ny, nh;
	o.top = old_top;
	pb_slider(&o, &ty, &th, &sy, &sh);
	pb_slider(b, &ty, &th, &ny, &nh);
	if (sh && nh && sh == nh) {
		short p1 = sy < ny ? sy : ny, p2 = sy + sh > ny + nh ? sy + sh : ny + nh;
		/* a raised slider's edge stands a few pixels out each way, over
		   the arrows' feet too (EContact: a line left under the up
		   arrow in 3D): within the bar, not the track. Flat, its frame
		   is the one pixel outside it, and the box stays off the arrows
		   (pb_vdi's fast way: some 15 ms a line on an ST) */
		short e = win_look_3d() ? 3 : 1;
		short b0 = b->across ? b->r.x : b->r.y, b1 = b0 + (b->across ? b->r.w : b->r.h);
		short a = p1 - e < b0 ? b0 : p1 - e;
		short l = (p2 + e > b1 ? b1 : p2 + e) - a;
		if (b->across) {
			r.x = a;
			r.w = l;
		} else {
			r.y = a;
			r.h = l;
		}
	}
	return r;
}

/* the button still down; *p where the pointer is along the bar */
static short left_down(const PANEBAR *b, short *p)
{
	short mx, my, mb, ks;
	graf_mkstate(&mx, &my, &mb, &ks);
	if (p)
		*p = b->across ? mx : my;
	return mb & 1;
}

/* an arrow held: some 15 rows a second, each step as many rows as the
   time the one before took asks for, so a slow machine takes bigger
   steps rather than scrolling slower (ECal's hours glide so) */
static void arrow_held(const PANEBAR *b, int d, int (*scroll)(long n))
{
	unsigned long last, now;
	long due = 0, n;
	if (!scroll(d))
		return;
	evnt_timer_(250);
	last = tos_ms();
	while (left_down(b, 0)) {
		now = tos_ms();
		due += (long)(now - last) * 15;	/* rows due, in thousandths */
		last = now;
		n = due / 1000;
		if (n < 1)
			n = 1;
		if (n > 8)
			n = 8;
		due -= n * 1000;
		if (due < 0)
			due = 0;
		evnt_timer_(scroll(d * n) ? 20 : 50);	/* at an end: wait for the button */
	}
}

void pb_click(PANEBAR *b, short x, short y, int (*scroll)(long n), int (*goto_top)(long t))
{
	short ty, th, sy, sh, p = b->across ? x : y;
	long page = b->shown > 1 ? b->shown - 1 : 1;
	int part = pb_part(b, x, y);
	if (part == PB_UP || part == PB_DOWN) {
		arrow_held(b, part == PB_UP ? -1 : 1, scroll);
	} else if (part == PB_PAGE_UP || part == PB_PAGE_DOWN) {
		int up = part == PB_PAGE_UP, first = 1;
		for (;;) {
			pb_slider(b, &ty, &th, &sy, &sh);
			if (up ? p >= sy : p < sy + sh)
				break;		/* the slider got to the pointer */
			if (!scroll(up ? -page : page))
				break;
			evnt_timer_(first ? 300 : 100);
			first = 0;
			if (!left_down(b, &p))
				break;
		}
	} else if (part == PB_SLIDER) {
		short grab, q;
		pb_slider(b, &ty, &th, &sy, &sh);
		grab = p - sy;
		wind_update(3);		/* BEG_MCTRL: the pointer is the bar's while held */
		do {
			short down = left_down(b, &q);
			goto_top(pb_top_at(b, q - grab));
			evnt_timer_(20);
			if (!down)
				break;
		} while (1);
		wind_update(2);		/* END_MCTRL */
	}
	while (left_down(b, 0))		/* the rest of the press does nothing */
		evnt_timer_(20);
}
