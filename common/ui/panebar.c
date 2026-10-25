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

void pb_draw(const PANEBAR *b, const GRECT *clip)
{
	OBJECT t[O_N];
	short ah = arrow(b), ty, th, sy, sh, i;
	short len = length(b), wide = b->across ? b->r.h : b->r.w;
	int d3 = dlg_3d_ok() && dlg_3d_on, across = b->across != 0;
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
	objc_draw(t, O_BAR, 2, c.x, c.y, c.w, c.h);
}

void pb_corner(const GRECT *r, const GRECT *clip)
{
	OBJECT t[1];
	GRECT c = *clip;
	if (!within(&c, r))
		return;
	ground(&t[0], r, dlg_3d_ok() && dlg_3d_on);
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
		/* (a raised slider's edge: a few pixels more each way) */
		short a = p1 - 3 < ty ? ty : p1 - 3;
		short l = (p2 + 3 > ty + th ? ty + th : p2 + 3) - a;
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
