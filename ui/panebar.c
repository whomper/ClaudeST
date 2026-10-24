/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * panebar.c - a pane's own vertical scroll bar, of AES objects (panebar.h;
 * the guidelines, 5.1). The click handling is EContact's list bar's
 * (2d1e7f8d), made the family's.
 */
#include "tos.h"
#include "gemui.h"
#include "dialog.h"		/* dlg_3d_ok, dlg_3d_on */
#include "panebar.h"

static short box_w, box_h;

/* the bar as wide as the AES's own slider column: what a vertical slider
   adds to a window's border (wind_calc), and the line it shares with the
   border (TOS 1.04 and 4, XaAES: 19, 23 and 20 pixels in ST high and VGA;
   graf_handle's box is XaAES's character cell, 10). An arrow box is
   graf_handle's box high, as each AES's gadgets */
static void sizes(void)
{
	short wc, hc, x0, y0, w0, h0, x1, y1, w1, h1;
	if (box_w)
		return;
	graf_handle(&wc, &hc, &box_w, &box_h);
	if (wind_calc(0, 0, 100, 100, 200, 200, &x0, &y0, &w0, &h0) &&
	    wind_calc(0, VSLIDE | UPARROW | DNARROW, 100, 100, 200, 200, &x1, &y1, &w1, &h1) &&
	    w1 - w0 + 1 > box_w)
		box_w = w1 - w0 + 1;
}

short pb_width(void)
{
	sizes();
	return box_w;
}

short pb_arrow_h(void)
{
	sizes();
	return box_h;
}

/* the track between the arrows, and the slider in it */
void pb_slider(const PANEBAR *b, short *ty, short *th, short *sy, short *sh)
{
	short ah = pb_arrow_h(), least = ah / 2 > 4 ? ah / 2 : 4;
	*ty = b->r.y + ah;
	*th = b->r.h - 2 * ah;
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

long pb_top_at(const PANEBAR *b, short y)
{
	short ty, th, sy, sh;
	long range = b->total - b->shown;
	pb_slider(b, &ty, &th, &sy, &sh);
	if (range <= 0 || th <= sh)
		return 0;
	if (y < ty)
		y = ty;
	if (y > ty + th - sh)
		y = ty + th - sh;
	return ((long)(y - ty) * range + (th - sh) / 2) / (th - sh);
}

int pb_part(const PANEBAR *b, short x, short y)
{
	short ah = pb_arrow_h(), ty, th, sy, sh;
	if (x < b->r.x || x >= b->r.x + b->r.w || y < b->r.y || y >= b->r.y + b->r.h)
		return PB_NONE;
	if (y < b->r.y + ah)
		return PB_UP;
	if (y >= b->r.y + b->r.h - ah)
		return PB_DOWN;
	pb_slider(b, &ty, &th, &sy, &sh);
	if (!sh)
		return PB_NONE;
	return y < sy ? PB_PAGE_UP : y >= sy + sh ? PB_PAGE_DOWN : PB_SLIDER;
}

/* the bar as AES objects: the box (its frame), the arrows, the track and
   the slider in it; the frames lie on each other, so the lines are single */
enum { O_BAR, O_UP, O_TRACK, O_SLIDER, O_DOWN, O_N };

void pb_draw(const PANEBAR *b, const GRECT *clip)
{
	OBJECT t[O_N];
	short ah = pb_arrow_h(), ty, th, sy, sh, i;
	int d3 = dlg_3d_ok() && dlg_3d_on;
	GRECT c = *clip;
	short x2, y2;
	if (b->r.h < 2 * ah + 4)
		return;
	/* only the bar: a raised object's edge goes past its box */
	x2 = c.x + c.w < b->r.x + b->r.w ? c.x + c.w : b->r.x + b->r.w;
	y2 = c.y + c.h < b->r.y + b->r.h ? c.y + c.h : b->r.y + b->r.h;
	if (c.x < b->r.x)
		c.x = b->r.x;
	if (c.y < b->r.y)
		c.y = b->r.y;
	c.w = x2 - c.x;
	c.h = y2 - c.y;
	if (c.w <= 0 || c.h <= 0)
		return;
	pb_slider(b, &ty, &th, &sy, &sh);
	for (i = 0; i < O_N; i++) {
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

	/* flat, every frame outside its box (thickness -1, 0xFF: GEM's
	   positive thickness is inside, and TOS fills an opaque box over a
	   frame inside it); in 3D inside (the AES draws a 3D object's edges in
	   its box, and an outside frame came out a plain black line). The box
	   is a pixel in from the bar's edge, so a frame outside it is the
	   bar's edge; the parts in it lie so that neighbours' frames are one
	   line, and nothing is drawn outside the bar */
	t[O_BAR].ob_type = G_BOX;
	t[O_BAR].ob_spec = d3 ? 0x00011100L : 0x00FF1180L;	/* 3D: the AES's ground (it greys only
								   a transparent box); flat: white */
	t[O_BAR].ob_x = b->r.x + 1;
	t[O_BAR].ob_y = b->r.y + 1;
	t[O_BAR].ob_width = b->r.w - 2;
	t[O_BAR].ob_height = b->r.h - 2;

	/* the arrows: the bar's first and last ah rows, frames included (in
	   3D the boxes themselves, their edges in them) */
	t[O_UP].ob_type = t[O_DOWN].ob_type = G_BOXCHAR;
	t[O_UP].ob_spec = 0x01FF1100L;		/* TOS's up arrow, transparent */
	t[O_DOWN].ob_spec = 0x02FF1100L;
	t[O_UP].ob_flags |= TOUCHEXIT;
	t[O_DOWN].ob_flags |= TOUCHEXIT;
	t[O_UP].ob_x = t[O_DOWN].ob_x = -d3;
	t[O_UP].ob_width = t[O_DOWN].ob_width = b->r.w - 2 + 2 * d3;
	t[O_UP].ob_y = -d3;
	t[O_DOWN].ob_y = b->r.h - ah - d3;
	t[O_UP].ob_height = t[O_DOWN].ob_height = ah - 2 + 2 * d3;

	/* the track between them, its end frames on theirs: TOS's dotted grey
	   flat, the AES's ground in 3D */
	t[O_TRACK].ob_type = G_BOX;
	t[O_TRACK].ob_spec = d3 ? 0x00011100L : 0x00FF1191L;
	t[O_TRACK].ob_x = 0;
	t[O_TRACK].ob_y = ah - 1;
	t[O_TRACK].ob_width = b->r.w - 2;
	t[O_TRACK].ob_height = th;

	/* the slider over the track's rows sy .. sy+sh-1 (flat, its frames a
	   row over and under them, on the track's at either end) */
	t[O_SLIDER].ob_type = G_BOX;
	t[O_SLIDER].ob_spec = d3 ? 0x00011100L : 0x00FF1180L;	/* flat: white over the dots */
	t[O_SLIDER].ob_flags |= TOUCHEXIT;
	t[O_SLIDER].ob_x = 0;
	t[O_SLIDER].ob_y = sh ? sy - ty : 0;
	t[O_SLIDER].ob_width = b->r.w - 2;
	t[O_SLIDER].ob_height = sh;
	if (!sh)
		t[O_SLIDER].ob_flags |= HIDETREE;

	/* the 3D flags set here, not by dlg_3d: its rule for lists (a white
	   framed box stays white, 5.6) would take the track for one */
	if (d3) {
		t[O_BAR].ob_flags |= 0x0400;		/* FL3DBAK: the ground */
		t[O_TRACK].ob_flags |= 0x0400;
		t[O_UP].ob_flags |= 0x0600;		/* FL3DACT: raised */
		t[O_DOWN].ob_flags |= 0x0600;
		t[O_SLIDER].ob_flags |= 0x0600;
	}
	objc_draw(t, O_BAR, 2, c.x, c.y, c.w, c.h);
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
		short y1 = sy < ny ? sy : ny, y2 = sy + sh > ny + nh ? sy + sh : ny + nh;
		/* (a raised slider's edge: a few pixels more each way) */
		r.y = y1 - 3 < ty ? ty : y1 - 3;
		r.h = (y2 + 3 > ty + th ? ty + th : y2 + 3) - r.y;
	}
	return r;
}

/* the button still down; *y where the pointer is */
static short left_down(short *y)
{
	short mx, my, mb, ks;
	graf_mkstate(&mx, &my, &mb, &ks);
	if (y)
		*y = my;
	return mb & 1;
}

/* an arrow held: some 15 rows a second, each step as many rows as the
   time the one before took asks for, so a slow machine takes bigger
   steps rather than scrolling slower (ECal's hours glide so) */
static void arrow_held(int d, int (*scroll)(long n))
{
	unsigned long last, now;
	long due = 0, n;
	if (!scroll(d))
		return;
	evnt_timer_(250);
	last = tos_ms();
	while (left_down(0)) {
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
	short ty, th, sy, sh;
	long page = b->shown > 1 ? b->shown - 1 : 1;
	int part = pb_part(b, x, y);
	if (part == PB_UP || part == PB_DOWN) {
		arrow_held(part == PB_UP ? -1 : 1, scroll);
	} else if (part == PB_PAGE_UP || part == PB_PAGE_DOWN) {
		int up = part == PB_PAGE_UP, first = 1;
		for (;;) {
			pb_slider(b, &ty, &th, &sy, &sh);
			if (up ? y >= sy : y < sy + sh)
				break;		/* the slider got to the pointer */
			if (!scroll(up ? -page : page))
				break;
			evnt_timer_(first ? 300 : 100);
			first = 0;
			if (!left_down(&y))
				break;
		}
	} else if (part == PB_SLIDER) {
		short grab, mx, my, mb, ks;
		pb_slider(b, &ty, &th, &sy, &sh);
		grab = y - sy;
		wind_update(3);		/* BEG_MCTRL: the pointer is the bar's while held */
		do {
			graf_mkstate(&mx, &my, &mb, &ks);
			goto_top(pb_top_at(b, my - grab));
			evnt_timer_(20);
		} while (mb & 1);
		wind_update(2);		/* END_MCTRL */
	}
	while (left_down(0))		/* the rest of the press does nothing */
		evnt_timer_(20);
}
