/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * click.c - selected on press, done on release (click.h; the guidelines,
 * 5.8): the wait for the release while the program's work goes on, and
 * the double click timed by the program.
 */
#include "tos.h"
#include "click.h"

static struct {
	short id, x, y;		/* this press's, from press_double */
	int second;		/* it ends a double click: not kept for the next */
} cur;

static struct {
	int valid;
	short id, x, y;
	unsigned long up;	/* when it was let go */
} last;

static unsigned long dclick_ms;

void press_set_dclick(unsigned long ms)
{
	dclick_ms = ms;
}

unsigned long press_dclick(void)
{
	if (!dclick_ms) {
		/* GEM's speeds 0 (slow) to 4 (fast), as its own double clicks
		   time them */
		static const unsigned short ms[5] = { 450, 330, 275, 220, 165 };
		short r = evnt_dclick(0, 0);
		dclick_ms = ms[r >= 0 && r <= 4 ? r : 2];
	}
	return dclick_ms;
}

void press_forget(void)
{
	last.valid = 0;
}

static int near(short a, short b)
{
	return a - b <= CLICK_SLOP && b - a <= CLICK_SLOP;
}

int press_double(short id, short x, short y)
{
	int second = last.valid && last.id == id && near(last.x, x) && near(last.y, y) &&
		     tos_ms() - last.up <= press_dclick();
	cur.id = id;
	cur.x = x;
	cur.y = y;
	cur.second = second;	/* a third press starts afresh */
	last.valid = 0;
	return second;
}

int press_track(const GRECT *r, short x, short y, void (*idle)(void), void (*msg)(short *m), short *dx,
		short *dy)
{
	short m[8];
	AEVENT e;
	evnt_set_m1(1, (short)(x - CLICK_SLOP), (short)(y - CLICK_SLOP), 2 * CLICK_SLOP + 1, 2 * CLICK_SLOP + 1);
	for (;;) {
		short which = evnt_multi_(MU_BUTTON | MU_M1 | MU_TIMER | MU_MESAG, 1, 1, 0, 20, m, &e);
		if ((which & MU_MESAG) && msg)
			msg(m);
		if (which & MU_BUTTON) {
			/* let go (a move and the release in one event: the release) */
			int in = e.mx >= r->x && e.mx < r->x + r->w && e.my >= r->y && e.my < r->y + r->h;
			if (in) {
				last.valid = !cur.second;
				last.id = cur.id;
				last.x = cur.x;
				last.y = cur.y;
				last.up = tos_ms();
			}
			return in ? PRESS_IN : PRESS_OUT;
		}
		if (which & MU_M1) {
			last.valid = 0;		/* no double click after a drag */
			if (dx)
				*dx = (short)(e.mx - x);
			if (dy)
				*dy = (short)(e.my - y);
			return PRESS_DRAG;
		}
		if (idle)
			idle();
	}
}
