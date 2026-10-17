/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bgwork.h - work done in the background without making the machine feel
 * slow (the guidelines, 2.2). Under plain TOS an accessory runs only while
 * the program in front calls the AES, and every moment it holds the
 * machine between its own event calls is a moment nothing else answers.
 * So background work waits until it may start, runs in short slices with
 * an event call between them, and gives way to the user.
 *
 *   bg_init(bg_parse(inf_value));     at start: when work may begin
 *   bg_opened();                      the user opened the window or asked for
 *                                     something: from now on work may run
 *   each turn of the event loop:
 *     bg_tick();                      after evnt_multi returns
 *     if (pending && bg_may_work()) {
 *         bg_begin();
 *         do one_step(); while (pending && bg_more());
 *     }
 *     timer = bg_timer(pending, 1000);   the next evnt_multi's timer, in ms
 *
 * bg_may_work() is false
 *   - before the start (BG_OPENED: until bg_opened(); N: N seconds after
 *     the machine started, counted from boot, not from our own start),
 *   - for 2 s after the mouse moved, a button or Shift/Control/Alternate
 *     was pressed, or our timer came back late (the program in front is
 *     busy),
 *   - while our program edition runs (twin_prg(): the accessory leaves the
 *     work to it).
 * A slice lasts BG_SLICE_030 ms on a 68020 or better, BG_SLICE_000 on a
 * 68000 (the _CPU cookie); one step must be shorter than that.
 * Nothing here allocates memory, so an accessory may use it at any time.
 */
#ifndef BGWORK_H
#define BGWORK_H

#define BG_NOW		0L		/* work may start at once */
#define BG_OPENED	(-1L)		/* only once the user opened us */
#define BG_QUIET	60L		/* the family's default: a minute after boot */
#define BG_SLICE_030	40		/* ms of work between event calls */
#define BG_SLICE_000	80
#define BG_RESTING	2000UL		/* ms after the user did something */
#define BG_PACE		50		/* evnt_multi timer while work waits */

/* the INF's value: "now", "opened" (or "when opened"), or seconds after
   boot; anything else gives BG_QUIET */
long bg_parse(const char *s);
void bg_init(long start);
void bg_opened(void);
void bg_tick(void);
int bg_may_work(void);
void bg_begin(void);
int bg_more(void);		/* 1 while the slice has time left */
unsigned long bg_timer(int pending, unsigned long idle_ms);

#endif
