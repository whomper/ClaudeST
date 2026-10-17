/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/bgwork.c on the build machine: when background work may run, how
 * long a slice lasts and what timer the event loop gets, with the clock,
 * the mouse, the CPU and the program edition played by the test.
 */
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "twin.h"
#include "bgwork.h"

static u32 now;
static short mx = 100, my = 100, mb, mks;
static long cpu = 30;
static short prg = -1;
static int fails;

u32 tos_ms(void) { return now; }
long get_cookie(long id) { return id == 0x5F435055L ? cpu : 0; }
short graf_mkstate(short *x, short *y, short *b, short *k) { *x = mx; *y = my; *b = mb; *k = mks; return 1; }
short twin_prg(void) { return prg; }

#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } } while (0)

/* one turn of an event loop that asked for timer t and got it on time */
static void turn(unsigned long t)
{
	now += t;
	bg_tick();
}

int main(void)
{
	unsigned long t;
	int steps;

	/* the INF's values */
	CHECK(bg_parse("now") == BG_NOW, "parse now");
	CHECK(bg_parse("opened") == BG_OPENED && bg_parse("when opened") == BG_OPENED && bg_parse("-1") == BG_OPENED, "parse opened");
	CHECK(bg_parse("120") == 120 && bg_parse(" 0") == 0, "parse seconds");
	CHECK(bg_parse("") == BG_QUIET && bg_parse("soon") == BG_QUIET, "parse other");

	/* a minute after boot: nothing before, work after, counted from boot */
	now = 20000;
	bg_init(BG_QUIET);
	bg_tick();
	CHECK(!bg_may_work(), "quiet at 20 s");
	t = bg_timer(1, 1000);
	CHECK(t == 500, "timer while waiting with work pending");
	while (now < 59000)
		turn(bg_timer(1, 1000));
	CHECK(!bg_may_work(), "quiet at 59 s");
	while (now < 60600)
		turn(bg_timer(1, 1000));
	CHECK(bg_may_work(), "works after 60 s");
	CHECK(bg_timer(1, 1000) == BG_PACE, "paced timer while working");
	CHECK(bg_timer(0, 1000) == 1000, "idle timer with nothing to do");

	/* a slice: 40 ms on a 68030 */
	bg_begin();
	for (steps = 0; bg_more() && steps < 1000; steps++)
		now += 3;
	CHECK(steps >= 13 && steps <= 14, "40 ms slice on a 68030");

	/* the user moves the mouse: 2 s of rest, then work again */
	turn(bg_timer(1, 1000));
	mx += 5;
	turn(bg_timer(1, 1000));
	CHECK(!bg_may_work(), "rests after the mouse moved");
	now += 1500;
	bg_tick();
	CHECK(!bg_may_work(), "still resting 1.5 s later");
	now += 600;
	bg_tick();
	CHECK(bg_may_work(), "works 2 s after the mouse");

	/* a button held, then Shift */
	mb = 1;
	turn(bg_timer(1, 1000));
	CHECK(!bg_may_work(), "rests while a button is down");
	mb = 0;
	now += 2100;
	bg_tick();
	CHECK(bg_may_work(), "works after the button");
	mks = 1;
	turn(bg_timer(1, 1000));
	CHECK(!bg_may_work(), "rests when Shift changes");
	mks = 0;
	now += 2100;
	bg_tick();
	now += 2100;
	bg_tick();
	CHECK(bg_may_work(), "works after Shift");

	/* the program in front keeps the machine: our 50 ms timer comes back
	   after 1.2 s */
	t = bg_timer(1, 1000);
	now += t + 1200;
	bg_tick();
	CHECK(!bg_may_work(), "rests when our timer came back late");
	now += 2100;
	bg_tick();
	CHECK(bg_may_work(), "works again after the late timer");

	/* the program edition starts: the accessory leaves the work to it */
	prg = 3;
	now += 1100;
	bg_tick();
	CHECK(!bg_may_work(), "no work while the PRG runs");
	prg = -1;
	now += 1100;
	bg_tick();
	CHECK(bg_may_work(), "works after the PRG quit");

	/* when opened: never before bg_opened(), whatever the time */
	now = 600000;
	bg_init(BG_OPENED);
	bg_tick();
	turn(1000);
	CHECK(!bg_may_work(), "opened-only waits after 10 min");
	bg_opened();
	CHECK(bg_may_work(), "works once opened");

	/* opened before the quiet period ends: work at once */
	now = 5000;
	bg_init(BG_QUIET);
	bg_tick();
	CHECK(!bg_may_work(), "quiet at 5 s");
	bg_opened();
	CHECK(bg_may_work(), "opening ends the quiet period");

	/* now: at once; an 80 ms slice on a 68000 */
	cpu = 0;
	now = 1000;
	bg_init(BG_NOW);
	bg_tick();
	CHECK(bg_may_work(), "now works at once");
	bg_begin();
	for (steps = 0; bg_more() && steps < 1000; steps++)
		now += 10;
	CHECK(steps == 8, "80 ms slice on a 68000");

	printf("%s\n", fails ? "bgwork: FAILED" : "bgwork: ok");
	return fails != 0;
}
