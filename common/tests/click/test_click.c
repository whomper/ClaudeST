/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * click.c on the build machine (the guidelines, 5.8), against a stand-in
 * evnt_multi that plays a script of events: released over the object or
 * elsewhere, a drag reported while the button is still down, the idle
 * call each timer turn, messages passed on, the double click timed from
 * evnt_dclick (too slow, too far, another object, a third press, after a
 * drag), and what evnt_multi was asked for.
 */
#include <stdio.h>
#include <string.h>
#include "click.h"
#include "tos.h"

static int checks, failed;
#define CHECK(c, what) do { checks++; if (c) printf("ok   %s\n", what); \
	else { failed++; printf("FAIL %s\n", what); } } while (0)

static unsigned long now = 10000;
static short rate = 2;			/* 275 ms */
static short m1_leave, m1_x, m1_y, m1_w, m1_h;
static short asked_clicks, asked_mask, asked_state;
static unsigned long asked_timer;

typedef struct {
	short which, mx, my, msg0;
	unsigned long ms;		/* how long the wait took */
} EV;
static const EV *script;
static int at, n_script, idles, msgs, last_msg;

u32 tos_ms(void) { return now; }
short evnt_dclick(short r, short s) { (void)r; (void)s; return rate; }
void evnt_set_m1(short leave, short x, short y, short w, short h)
{
	m1_leave = leave, m1_x = x, m1_y = y, m1_w = w, m1_h = h;
}
short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev)
{
	const EV *e;
	(void)flags;
	asked_clicks = clicks, asked_mask = mask, asked_state = state, asked_timer = timer;
	if (at >= n_script) {		/* the script ran out: let go where it was */
		ev->mx = ev->my = 0;
		return MU_BUTTON;
	}
	e = &script[at++];
	now += e->ms;
	ev->mx = e->mx;
	ev->my = e->my;
	if (e->which & MU_MESAG)
		msg[0] = e->msg0;
	return e->which;
}
static void idle(void) { idles++; }
static void on_msg(short *m) { msgs++; last_msg = m[0]; }

static int run(const EV *s, int n, const GRECT *r, short x, short y, short *dx, short *dy)
{
	script = s, n_script = n, at = 0, idles = 0, msgs = 0;
	return press_track(r, x, y, idle, on_msg, dx, dy);
}

#define RUN(s, r, x, y) run(s, (int)(sizeof(s) / sizeof(s[0])), r, x, y, &dx, &dy)

int main(void)
{
	GRECT row = { 16, 100, 300, 16 }, row2 = { 16, 116, 300, 16 };
	short dx = 0, dy = 0;
	static const EV up_in[] = { { MU_TIMER, 50, 105, 0, 20 }, { MU_TIMER, 51, 105, 0, 20 }, { MU_BUTTON, 52, 106, 0, 10 } };
	static const EV up_out[] = { { MU_BUTTON, 50, 140, 0, 30 } };
	static const EV dragged[] = { { MU_TIMER, 52, 105, 0, 20 }, { MU_M1, 56, 105, 0, 10 } };
	static const EV with_msg[] = { { MU_MESAG, 50, 105, 20, 5 }, { MU_MESAG | MU_TIMER, 50, 105, 20, 15 }, { MU_BUTTON, 50, 105, 0, 5 } };
	static const EV quick[] = { { MU_BUTTON, 50, 105, 0, 60 } };
	static const EV both[] = { { MU_BUTTON | MU_M1, 70, 105, 0, 60 } };

	CHECK(press_dclick() == 275, "the double-click time from evnt_dclick's speed 2");

	CHECK(press_double(1, 50, 105) == 0, "a first press: no double click");
	CHECK(RUN(up_in, &row, 50, 105) == PRESS_IN, "let go over the row: PRESS_IN");
	CHECK(idles == 2, "idle() at each timer turn while held");
	CHECK(asked_clicks == 1 && asked_mask == 1 && asked_state == 0 && asked_timer == 20,
	      "evnt_multi asked for the left button's release, a 20 ms timer");
	CHECK(m1_leave == 1 && m1_x == 46 && m1_y == 101 && m1_w == 9 && m1_h == 9, "and the mouse leaving a box round the press");

	now += 100;
	CHECK(press_double(1, 51, 106) == 1, "pressed again soon and near: a double click's second");
	CHECK(RUN(quick, &row, 51, 106) == PRESS_IN, "its release over the row");
	now += 100;
	CHECK(press_double(1, 51, 106) == 0, "a third press starts afresh");
	RUN(quick, &row, 51, 106);

	now += 100;
	CHECK(press_double(1, 50, 105) == 1, "the third, a single click again, and a fourth soon after: a double click");
	RUN(quick, &row, 50, 105);

	now += 2000;
	press_double(2, 50, 121);
	RUN(quick, &row2, 50, 121);
	now += 400;
	CHECK(press_double(2, 50, 121) == 0, "pressed again too late: no double click");
	RUN(quick, &row2, 50, 121);

	now += 2000;
	press_double(2, 50, 121);
	RUN(quick, &row2, 50, 121);
	now += 50;
	CHECK(press_double(2, 60, 121) == 0, "pressed again 10 pixels away: no double click");
	RUN(quick, &row2, 60, 121);

	now += 2000;
	press_double(1, 50, 105);
	RUN(quick, &row, 50, 105);
	now += 50;
	CHECK(press_double(2, 50, 107) == 0, "another object soon after: no double click");

	now += 2000;
	press_double(1, 50, 105);
	CHECK(RUN(up_out, &row, 50, 105) == PRESS_OUT, "let go elsewhere: PRESS_OUT");
	now += 50;
	CHECK(press_double(1, 50, 105) == 0, "after a release elsewhere, no double click");

	now += 2000;
	press_double(1, 52, 105);
	CHECK(RUN(dragged, &row, 52, 105) == PRESS_DRAG && dx == 4 && dy == 0,
	      "moved 4 pixels while held: PRESS_DRAG at once, with how far");
	CHECK(at == 2, "the drag reported on the move, not waiting for the release");
	now += 50;
	CHECK(press_double(1, 52, 105) == 0, "after a drag, no double click");

	now += 2000;
	press_double(1, 50, 105);
	CHECK(RUN(with_msg, &row, 50, 105) == PRESS_IN && msgs == 2 && last_msg == 20,
	      "messages while held passed to msg() (WM_REDRAW)");
	CHECK(idles == 2, "and idle() on each turn, a message's too");

	now += 2000;
	press_double(1, 50, 105);
	CHECK(RUN(both, &row, 50, 105) == PRESS_IN, "a move and the release in one event: the release");

	press_set_dclick(500);
	now += 2000;
	press_double(3, 50, 105);
	RUN(quick, &row, 50, 105);
	now += 450;
	CHECK(press_double(3, 50, 105) == 1, "a set double-click time (500 ms)");
	RUN(quick, &row, 50, 105);
	press_double(3, 50, 105);
	RUN(quick, &row, 50, 105);
	press_forget();
	now += 50;
	CHECK(press_double(3, 50, 105) == 0, "press_forget: the last press gone");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
