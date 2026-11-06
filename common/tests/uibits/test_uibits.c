/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The small UI helpers on the build machine, against a stand-in AES and
 * VDI: fsel_settle (fsel.c), scr_work_size (scrsize.c), enlarge_mark
 * (enlarge.c). make test
 */
#include <stdio.h>
#include <string.h>
#include "gemui.h"
#include "fsel.h"
#include "scrsize.h"
#include "enlarge.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes, gl_wchar = 8, gl_hchar = 16;

static int checks, failed;
static void check(int ok, const char *what)
{
	checks++;
	failed += !ok;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

/* the AES: the button down for some polls, button events queued */
static int down_polls, polls, queued, multis;
short graf_mkstate(short *mx, short *my, short *mb, short *ks)
{
	*mx = *my = *ks = 0;
	polls++;
	*mb = down_polls > 0 ? (down_polls--, 1) : 0;
	return 1;
}

short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev)
{
	(void)clicks, (void)mask, (void)state, (void)timer, (void)msg;
	multis++;
	memset(ev, 0, sizeof *ev);
	ev->which = (flags & MU_BUTTON) && queued > 0 ? (queued--, MU_BUTTON) : MU_TIMER;
	return ev->which;
}

/* the desktop's work area and a window's borders */
short wind_get(short h, short field, short *a, short *b, short *c, short *d)
{
	(void)h, (void)field;
	*a = 0, *b = 19, *c = 640, *d = 461;
	return 1;
}

short wind_calc(short type, short kind, short x, short y, short w, short h, short *ox, short *oy, short *ow, short *oh)
{
	(void)type;
	/* a title bar (19), and a slider at the right (20) when the kind has one */
	*ox = (short)(x + 1);
	*oy = (short)(y + 20);
	*ow = (short)(w - 2 - ((kind & VSLIDE) ? 19 : 0));
	*oh = (short)(h - 21 - ((kind & SIZER) ? 19 : 0));
	return 1;
}

/* the VDI: what was drawn */
static int fills, lines, fill_pen, line_pen, mode;
static short box[4], seen_x0, seen_y0, seen_x1, seen_y1;
void vswr_mode(short h, short m) { (void)h; mode = m; }
void vsf_interior(short h, short s) { (void)h, (void)s; }
void vsf_perimeter(short h, short p) { (void)h, (void)p; }
void vsf_color(short h, short c) { (void)h; fill_pen = c; }
void vsl_color(short h, short c) { (void)h; line_pen = c; }
void vsl_type(short h, short t) { (void)h, (void)t; }
void vr_recfl(short h, const short *p)
{
	(void)h;
	fills++;
	memcpy(box, p, sizeof box);
}

void v_pline(short h, short n, const short *p)
{
	int i;
	(void)h;
	lines++;
	for (i = 0; i < n; i++) {
		if (p[2 * i] < seen_x0) seen_x0 = p[2 * i];
		if (p[2 * i] > seen_x1) seen_x1 = p[2 * i];
		if (p[2 * i + 1] < seen_y0) seen_y0 = p[2 * i + 1];
		if (p[2 * i + 1] > seen_y1) seen_y1 = p[2 * i + 1];
	}
}

int main(void)
{
	short w, h;

	setvbuf(stdout, 0, _IOLBF, 0);
	/* fsel_settle */
	down_polls = 3, queued = 2;
	fsel_settle();
	check(polls == 4 && down_polls == 0, "fsel_settle: waits for the button to be let go");
	check(queued == 0 && multis == 3, "fsel_settle: the clicks queued taken off, then no more asked");
	polls = multis = 0, down_polls = 0, queued = 100;
	fsel_settle();
	check(polls == 1 && multis == 16, "fsel_settle: a flood of clicks: taken off 16 at most");
	queued = 0;

	/* scr_work_size */
	scr_work_size(NAME | MOVER, 8, &w, &h);
	check(w == 616 && h == 424, "scr_work_size: 640 x 461 less borders and 8 at each side, in 8-pixel cells");
	scr_work_size(NAME | MOVER | VSLIDE | SIZER, 8, &w, &h);
	check(w == 600 && h == 405, "scr_work_size: a slider and a sizer take theirs, the width in cells");
	gl_wchar = 6;
	scr_work_size(NAME, 0, &w, &h);
	check(w == 636 && w % 6 == 0, "scr_work_size: the system font's cell");
	scr_work_size(NAME, 400, &w, &h);
	check(w == 64 && h == 64, "scr_work_size: never under 64 x 64");

	/* enlarge_mark */
	seen_x0 = seen_y0 = 32767, seen_x1 = seen_y1 = -32768;
	enlarge_mark(1, 100, 50);
	check(fills == 1 && fill_pen == 0 && box[0] == 100 && box[1] == 50 && box[2] == 100 + ENLARGE_SIZE - 1 &&
	      box[3] == 50 + ENLARGE_SIZE - 1, "enlarge_mark: a white square of ENLARGE_SIZE at x, y");
	check(lines == 9 && line_pen == 1, "enlarge_mark: its edge and arrow in black, 9 strokes");
	check(seen_x0 == 100 && seen_y0 == 50 && seen_x1 == 100 + ENLARGE_SIZE - 1 && seen_y1 == 50 + ENLARGE_SIZE - 1,
	      "enlarge_mark: nothing drawn outside its square");
	check(mode == MD_REPLACE, "enlarge_mark: the writing mode left MD_REPLACE");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
