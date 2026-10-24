/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * PBTEST.PRG: a window with two panes, each with its own scroll bar
 * (ui/panebar.c), for looking at them as each AES draws them: the left
 * pane's slider a tenth of the track, a third of the way down, the right
 * pane with nothing to scroll (no slider). A file FLAT beside it draws them
 * as DIALOGS3D=0 would. A click on a bar scrolls it (pb_click). It says
 * "PBT ready" on the console (NatFeats) once the window is drawn, and
 * "PBT top N" after each click.
 */
#include <stdio.h>
#include "tos.h"
#include "gemui.h"
#include "dialog.h"
#include "panebar.h"

static short win, wx, wy, ww, wh, said;
static PANEBAR bar[2];

static void place(void)
{
	short pw = ww / 2;
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	pw = ww / 2;
	bar[0].r.x = wx + pw - pb_width();
	bar[1].r.x = wx + ww - pb_width();
	bar[0].r.y = bar[1].r.y = wy;
	bar[0].r.w = bar[1].r.w = pb_width();
	bar[0].r.h = bar[1].r.h = wh;
}

/* the panes white, the bars at their right */
static void draw(const GRECT *c)
{
	OBJECT t[3];
	short i;
	for (i = 0; i < 3; i++) {
		t[i].ob_next = i == 0 ? -1 : i == 1 ? 2 : 0;
		t[i].ob_head = t[i].ob_tail = -1;
		t[i].ob_type = G_BOX;
		t[i].ob_flags = i == 2 ? LASTOB : 0;
		t[i].ob_state = 0;
		t[i].ob_spec = 0x00001180L;	/* white, no frame */
		t[i].ob_y = 0;
		t[i].ob_height = wh;
	}
	t[0].ob_head = 1;
	t[0].ob_tail = 2;
	t[0].ob_x = wx;
	t[0].ob_y = wy;
	t[0].ob_width = ww;
	t[1].ob_x = 0;
	t[1].ob_width = ww / 2;
	t[2].ob_x = ww / 2 - 1;
	t[2].ob_width = ww - ww / 2 + 1;
	objc_draw(t, 0, 2, c->x, c->y, c->w, c->h);
	pb_draw(&bar[0], c);
	pb_draw(&bar[1], c);
}

static int inter(GRECT *a, const GRECT *b)
{
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	a->x = a->x > b->x ? a->x : b->x;
	a->y = a->y > b->y ? a->y : b->y;
	a->w = x2 - a->x;
	a->h = y2 - a->y;
	return a->w > 0 && a->h > 0;
}

static void redraw(const GRECT *area)
{
	GRECT r;
	wind_update(1);
	graf_mouse(256, 0);
	wind_get(win, WF_FIRSTXYWH, &r.x, &r.y, &r.w, &r.h);
	while (r.w && r.h) {
		if (inter(&r, area))
			draw(&r);
		wind_get(win, WF_NEXTXYWH, &r.x, &r.y, &r.w, &r.h);
	}
	graf_mouse(257, 0);
	wind_update(0);
}

static PANEBAR *clicked;

static void redraw_bar(void)
{
	redraw(&clicked->r);
}

static int scroll(long n)
{
	long t = clicked->top + n;
	if (t > clicked->total - clicked->shown)
		t = clicked->total - clicked->shown;
	if (t < 0)
		t = 0;
	if (t == clicked->top)
		return 0;
	{
		long old = clicked->top;
		GRECT m;
		clicked->top = t;
		m = pb_moved(clicked, old);
		redraw(&m);
	}
	return 1;
}

static int goto_top(long t)
{
	return scroll(t - clicked->top);
}

int main(void)
{
	short msg[8], i;
	AEVENT ev;
	char line[40];
	appl_init();
	graf_mouse(0, 0);
	{
		long f = Fopen("FLAT", 0);
		if (f >= 0) {
			Fclose((short)f);
			dlg_3d_on = 0;
		}
	}
	bar[0].total = 100;
	bar[0].shown = 10;
	bar[0].top = 30;
	bar[1].total = 5;
	bar[1].shown = 10;
	win = wind_create(NAME | CLOSER | MOVER | FULLER | SIZER, 0, 20, 640, 460);
	wind_set_str(win, WF_NAME, " Pane bars ");
	wind_open(win, 60, 60, 400, 300);
	place();
	for (;;) {
		short w = evnt_multi_(MU_MESAG | MU_BUTTON | MU_TIMER, 1, 1, 1, said ? 0x7fffffffUL : 1500, msg, &ev);
		if ((w & MU_TIMER) && !said) {
			nf_debug("PBT ready\n");
			said = 1;
		}
		if (w & MU_BUTTON) {
			for (i = 0; i < 2; i++)
				if (pb_part(&bar[i], ev.mx, ev.my) != PB_NONE) {
					clicked = &bar[i];
					pb_click(clicked, ev.mx, ev.my, scroll, goto_top);
					snprintf(line, sizeof(line), "PBT top %ld\n", clicked->top);
					nf_debug(line);
				}
		}
		if (!(w & MU_MESAG))
			continue;
		if (msg[0] == WM_REDRAW) {
			GRECT a;
			a.x = msg[4];
			a.y = msg[5];
			a.w = msg[6];
			a.h = msg[7];
			redraw(&a);
		} else if (msg[0] == WM_TOPPED) {
			wind_set(win, WF_TOP, 0, 0, 0, 0);
		} else if (msg[0] == WM_SIZED || msg[0] == WM_MOVED) {
			wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6] < 120 ? 120 : msg[6], msg[7] < 120 ? 120 : msg[7]);
			place();
		} else if (msg[0] == WM_CLOSED) {
			break;
		}
	}
	(void)redraw_bar;
	wind_close(win);
	wind_delete(win);
	appl_exit();
	return 0;
}
