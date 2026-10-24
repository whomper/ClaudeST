/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * A pane's scroll bar (ui/panebar.c) on the build machine: where its
 * slider is, what a point on it is, the AES objects it hands objc_draw
 * (flat and in 3D), what to draw again after a scroll, and a press on it
 * against a scripted mouse (an arrow held, the track held, the slider
 * dragged).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "dialog.h"
#include "panebar.h"

static int failed, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

/* ---------------- the stand-in AES ---------------- */

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
	*wchar = 8;
	*hchar = 16;
	*wbox = 10;	/* XaAES's: the character cell, narrower than its gadgets */
	*hbox = 19;
	return 1;
}

/* a vertical slider adds 15 pixels to the border, a horizontal one 20, and
   each shares a line with it */
short wind_calc(short type, short kind, short x, short y, short w, short h,
		short *ox, short *oy, short *ow, short *oh)
{
	(void)type;
	*ox = x - 1;
	*oy = y - 1;
	*ow = w + 2 + (kind & VSLIDE ? 15 : 0);
	*oh = h + 2 + (kind & HSLIDE ? 20 : 0);
	return 1;
}

static int d3_ok;
int dlg_3d_on = 1;
static int d3_calls;

int dlg_3d_ok(void)
{
	return d3_ok;
}

void dlg_3d(OBJECT *tree)
{
	(void)tree;
	d3_calls++;
}

static OBJECT drawn[8];
static short drawn_clip[4], drawn_start, draws;

short objc_draw(OBJECT *tree, short start, short depth, short x, short y, short w, short h)
{
	(void)depth;
	int n = 0;
	while (n < 8 && !(tree[n++].ob_flags & LASTOB))	/* the tree up to its last object */
		;
	memcpy(drawn, tree, n * sizeof(OBJECT));
	drawn_start = start;
	drawn_clip[0] = x;
	drawn_clip[1] = y;
	drawn_clip[2] = w;
	drawn_clip[3] = h;
	draws++;
	return 1;
}

/* the mouse: a script of (y, button) read by graf_mkstate */
static const short *mouse;
static int mpos, mlen;

short graf_mkstate(short *mx, short *my, short *mb, short *ks)
{
	int i = mpos < mlen ? mpos : mlen - 1;
	*mx = *my = mouse[2 * i];	/* a bar across reads x, one down y */
	*mb = mouse[2 * i + 1];
	*ks = 0;
	mpos++;
	return 1;
}

static unsigned long now;
static int updates;

short evnt_timer_(unsigned long ms)
{
	now += ms;
	return 1;
}

unsigned long tos_ms(void)
{
	return now;
}

short wind_update(short mode)
{
	updates += mode == 3 ? 1 : -1;
	return 1;
}

/* the pane: scroll and goto_top as an app's */
static PANEBAR bar;
static long scrolled[64];
static int nscrolls;
static long went[64];
static int ngoes;

static int scroll(long n)
{
	long t = bar.top + n;
	if (t > bar.total - bar.shown)
		t = bar.total - bar.shown;
	if (t < 0)
		t = 0;
	if (nscrolls < 64)
		scrolled[nscrolls++] = n;
	if (t == bar.top)
		return 0;
	bar.top = t;
	return 1;
}

static int goto_top(long t)
{
	if (ngoes < 64)
		went[ngoes++] = t;
	bar.top = t;
	return 1;
}

static void press(const short *script, int n)
{
	mouse = script;
	mlen = n;
	mpos = 0;
	nscrolls = ngoes = 0;
}

int main(void)
{
	short ty, th, sy, sh;
	GRECT clip = { 0, 0, 640, 400 }, m;
	setvbuf(stdout, 0, _IOLBF, 0);

	bar.r.x = 100;
	bar.r.y = 50;
	bar.r.w = pb_width();
	bar.r.h = 300;
	bar.total = 100;
	bar.shown = 10;
	bar.top = 0;
	check(pb_width() == 16 && pb_arrow_h() == 19, "pb_width: the AES's slider column (wind_calc); pb_arrow_h: its gadget box (graf_handle)");

	pb_slider(&bar, &ty, &th, &sy, &sh);
	check(ty == 69 && th == 262 && sy == 69 && sh == 26, "the track under the up arrow, the slider a tenth of it, at the top");
	bar.top = 90;
	pb_slider(&bar, &ty, &th, &sy, &sh);
	check(sy == 69 + 262 - 26, "... at the bottom when the last rows are shown");
	bar.top = 45;
	pb_slider(&bar, &ty, &th, &sy, &sh);
	check(sy == 69 + 118, "... half way, half way");
	bar.total = 8;
	pb_slider(&bar, &ty, &th, &sy, &sh);
	check(sh == 0, "everything shown: no slider");
	bar.total = 100000;
	bar.top = 0;
	pb_slider(&bar, &ty, &th, &sy, &sh);
	check(sh == 9, "a long list: the slider no less than half an arrow box");
	bar.total = 100;

	check(pb_part(&bar, 105, 55) == PB_UP && pb_part(&bar, 105, 345) == PB_DOWN, "the arrows");
	check(pb_part(&bar, 105, 75) == PB_SLIDER && pb_part(&bar, 105, 200) == PB_PAGE_DOWN, "the slider, the track under it");
	check(pb_part(&bar, 99, 200) == PB_NONE && pb_part(&bar, 116, 200) == PB_NONE, "beside the bar: nothing");
	bar.top = 90;
	check(pb_part(&bar, 105, 200) == PB_PAGE_UP, "the track over the slider");
	check(pb_top_at(&bar, 0) == 0 && pb_top_at(&bar, 999) == 90 && pb_top_at(&bar, 69 + 118) == 45,
	      "pb_top_at: the top, the end, half way");

	/* drawn: the objects, flat */
	bar.top = 0;
	d3_ok = 0;
	pb_draw(&bar, &clip);
	check(draws == 1 && drawn_start == 0, "pb_draw: one objc_draw of the bar");
	check(drawn[0].ob_type == G_BOX && drawn[0].ob_x == 101 && drawn[0].ob_y == 51 && drawn[0].ob_width == 14 &&
	      drawn[0].ob_height == 298 && ((drawn[0].ob_spec >> 16) & 0xff) == 0xff,
	      "... the box a pixel in from the bar's edge, its frame outside it (-1: TOS fills over a frame inside)");
	check(drawn[0].ob_head == 1 && drawn[0].ob_tail == 4 && drawn[1].ob_next == 2 && drawn[2].ob_next == 4 &&
	      drawn[4].ob_next == 0 && drawn[2].ob_head == 3 && drawn[3].ob_next == 2 && (drawn[4].ob_flags & LASTOB),
	      "... a tree: the arrows and the track in the box, the slider in the track, the last object marked");
	check(drawn[1].ob_type == G_BOXCHAR && (drawn[1].ob_spec >> 24) == 1 && drawn[4].ob_type == G_BOXCHAR &&
	      (drawn[4].ob_spec >> 24) == 2 && drawn[4].ob_y == 300 - 19 && drawn[1].ob_height == 17 &&
	      drawn[1].ob_x == 0 && drawn[1].ob_y == 0 && drawn[1].ob_width == 14 &&
	      ((drawn[1].ob_spec >> 16) & 0xff) == 0xff, "... TOS's arrows at each end, their frames on the bar's");
	check((drawn[2].ob_spec & 0xff) == 0x91, "... the track dotted, as TOS's window bars, when flat");
	check(drawn[2].ob_y == 18 && drawn[2].ob_height == 262 && drawn[3].ob_y == 0 && drawn[3].ob_height == 26 &&
	      !(drawn[3].ob_flags & HIDETREE), "... the track between the arrows, the slider at its top");
	check(drawn_clip[0] == 100 && drawn_clip[1] == 50 && drawn_clip[2] == 16 && drawn_clip[3] == 300,
	      "... clipped to the bar (a raised edge goes past its box)");
	check(d3_calls == 0 && !(drawn[0].ob_flags & 0x0600) && !(drawn[3].ob_flags & 0x0600),
	      "... no 3D flags where the AES has no 3D");

	d3_ok = 1;
	pb_draw(&bar, &clip);
	check((drawn[2].ob_spec & 0xff) == 0x00 && (drawn[0].ob_spec & 0xff) == 0x00 && (drawn[3].ob_spec & 0xff) == 0x00,
	      "in 3D the box, the track and the slider transparent: the AES gives them its ground and edges");
	check((drawn[0].ob_flags & 0x0600) == 0x0400 && (drawn[2].ob_flags & 0x0600) == 0x0400 &&
	      (drawn[1].ob_flags & 0x0600) == 0x0600 && (drawn[4].ob_flags & 0x0600) == 0x0600 &&
	      (drawn[3].ob_flags & 0x0600) == 0x0600, "... the box and the track the ground, the arrows and the slider raised");
	check(((drawn[0].ob_spec >> 16) & 0xff) == 1 && drawn[0].ob_x == 100 && drawn[0].ob_y == 50 &&
	      drawn[0].ob_width == 16 && drawn[0].ob_height == 300 && drawn[1].ob_x == 0 && drawn[1].ob_y == 0 &&
	      drawn[1].ob_width == 16 && drawn[1].ob_height == 19 && drawn[4].ob_y == 300 - 19 &&
	      drawn[2].ob_x == 1 && drawn[2].ob_y == 19 && drawn[3].ob_y == 0,
	      "... frames inside (the AES draws 3D edges in the box), the box the whole bar (nothing left undrawn)");
	dlg_3d_on = 0;
	pb_draw(&bar, &clip);
	check((drawn[2].ob_spec & 0xff) == 0x91, "DIALOGS3D=0: flat again");
	dlg_3d_on = 1;
	bar.total = 5;
	pb_draw(&bar, &clip);
	check(drawn[3].ob_flags & HIDETREE, "everything shown: the slider hidden");
	bar.total = 100;
	{
		GRECT off = { 300, 0, 50, 50 };
		int before = draws;
		pb_draw(&bar, &off);
		check(draws == before, "a clip off the bar: nothing drawn");
	}

	/* after a scroll: the slider's old and new places, a little more */
	bar.top = 10;
	m = pb_moved(&bar, 0);
	check(m.y == 69 && m.h == (69 + 26 + 26 + 3) - 69 && m.x == bar.r.x && m.w == bar.r.w,
	      "pb_moved: from the old slider's top to the new one's bottom");

	/* pressed */
	{
		/* the down arrow held for a while, then let go */
		static const short s[] = { 345, 1, 345, 1, 345, 1, 345, 1, 345, 0 };
		bar.top = 0;
		press(s, 5);
		pb_click(&bar, 105, 345, scroll, goto_top);
		check(nscrolls >= 2 && scrolled[0] == 1 && bar.top > 0, "the down arrow held: a row, then more while held");
	}
	{
		/* the up arrow at the top: nothing to do */
		static const short s[] = { 55, 0 };
		bar.top = 0;
		press(s, 1);
		pb_click(&bar, 105, 55, scroll, goto_top);
		check(nscrolls == 1 && scrolled[0] == -1 && bar.top == 0, "the up arrow at the top: tried once");
	}
	{
		/* the track under the slider held: pages until the slider reaches the pointer */
		static const short s[] = { 300, 1 };
		bar.top = 0;
		press(s, 1);
		{
			static const short up[] = { 300, 1, 300, 1, 300, 1, 300, 1, 300, 1, 300, 1, 300, 1, 300, 1, 300, 1,
				300, 1, 300, 1, 300, 1, 300, 0 };
			mouse = up;
			mlen = 13;
		}
		pb_click(&bar, 105, 300, scroll, goto_top);
		pb_slider(&bar, &ty, &th, &sy, &sh);
		check(nscrolls >= 2 && scrolled[0] == 9 && sy + sh > 300, "the track held: a page (9 rows) at a time, up to the pointer");
	}
	{
		/* the slider dragged down by 118 pixels */
		static const short s[] = { 80, 1, 150, 1, 198, 1, 198, 0 };
		bar.top = 0;
		press(s, 4);
		updates = 0;
		pb_click(&bar, 105, 80, scroll, goto_top);
		check(ngoes >= 3 && went[ngoes - 1] == 45, "the slider dragged half way: the middle rows");
		check(updates == 0, "... the mouse the bar's while held (BEG_MCTRL), given back");
	}

	/* a bar across a pane's foot: the same along x */
	{
		static const short s[] = { 60, 1, 120, 1, 181, 1, 181, 0 };
		PANEBAR h;
		memset(&h, 0, sizeof(h));
		h.r.x = 40;
		h.r.y = 360;
		h.r.w = 300;
		h.r.h = pb_height();
		h.total = 100;
		h.shown = 10;
		h.across = 1;
		check(pb_height() == 21, "pb_height: the AES's slider row (wind_calc)");
		pb_slider(&h, &ty, &th, &sy, &sh);
		check(ty == 56 && th == 268 && sy == 56 && sh == 26, "across: the track right of the left arrow (pb_width() long)");
		check(pb_part(&h, 45, 365) == PB_UP && pb_part(&h, 335, 365) == PB_DOWN &&
		      pb_part(&h, 60, 365) == PB_SLIDER && pb_part(&h, 200, 365) == PB_PAGE_DOWN &&
		      pb_part(&h, 200, 359) == PB_NONE, "... the arrows, the slider, the track, above the bar");
		d3_ok = 0;
		pb_draw(&h, &clip);
		check(drawn[1].ob_type == G_BOXCHAR && (drawn[1].ob_spec >> 24) == 4 && (drawn[4].ob_spec >> 24) == 3 &&
		      drawn[1].ob_x == 0 && drawn[1].ob_y == 0 && drawn[1].ob_width == 14 && drawn[1].ob_height == 19 &&
		      drawn[4].ob_x == 300 - 16, "... TOS's left and right arrows at each end");
		check(drawn[2].ob_x == 15 && drawn[2].ob_y == 0 && drawn[2].ob_width == 268 && drawn[2].ob_height == 19 &&
		      drawn[3].ob_x == 0 && drawn[3].ob_width == 26 && drawn[3].ob_height == 19,
		      "... the track between them, the slider at its left");
		h.top = 10;
		m = pb_moved(&h, 0);
		check(m.x == 56 && m.w == 55 && m.y == 360 && m.h == 21, "... pb_moved along x");
		h.top = 0;
		press(s, 4);
		bar = h;	/* scroll and goto_top move bar */
		pb_click(&bar, 60, 365, scroll, goto_top);
		check(ngoes >= 3 && went[ngoes - 1] == 45, "... the slider dragged half way along x: the middle");
		d3_ok = 1;
	}
	{
		GRECT r = { 600, 380, 16, 21 };
		int before = draws;
		pb_corner(&r, &clip);
		check(draws == before + 1 && drawn_start == 0 && drawn[0].ob_type == G_BOX && drawn[0].ob_x == 600 &&
		      drawn[0].ob_width == 16 && (drawn[0].ob_flags & LASTOB) && (drawn[0].ob_flags & 0x0600) == 0x0400,
		      "pb_corner: the bars' ground, in 3D the AES's, all of the square");
	}

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
