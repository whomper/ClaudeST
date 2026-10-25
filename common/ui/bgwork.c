/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bgwork.c - background work in short slices that gives way to the user
 * (see bgwork.h). From the start-up measurements of ECal, EContact and
 * EHypView's accessories (16 Oct 2026): ECal read a 250-event calendar in
 * one go (1.6 s on a Falcon, 15 s on an ST) and checked its alarms the
 * same way every minute; EHypView indexed its library before its first
 * event call.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "twin.h"
#include "bgwork.h"

static long start = BG_QUIET;		/* BG_OPENED, or seconds after boot */
static int opened;
static unsigned long rest_until;	/* no work before this (tos_ms) */
static unsigned long slice_end, slice_len;
static unsigned long asked, asked_at;	/* the last timer we asked for */
static unsigned long twin_at;
static int twin_runs;
static short last_x = -1, last_y, last_ks;

long bg_parse(const char *s)
{
	long n = 0;
	while (*s == ' ')
		s++;
	if (!strncmp(s, "now", 3) || !strncmp(s, "NOW", 3))
		return BG_NOW;
	if (!strncmp(s, "opened", 6) || !strncmp(s, "OPENED", 6) || !strncmp(s, "when", 4) ||
	    !strncmp(s, "WHEN", 4) || !strcmp(s, "-1"))
		return BG_OPENED;
	if (*s < '0' || *s > '9')
		return BG_QUIET;
	while (*s >= '0' && *s <= '9' && n < 86400L)
		n = n * 10 + (*s++ - '0');
	return n;
}

void bg_init(long when)
{
	start = when;
	opened = 0;
	slice_len = (get_cookie(0x5F435055L) >= 20) ? BG_SLICE_030 : BG_SLICE_000;	/* '_CPU' */
	rest_until = 0;
	last_x = -1;
	asked = 0;
	twin_at = 0;
	twin_runs = 0;
}

void bg_opened(void)
{
	opened = 1;
}

static void rest(unsigned long now)
{
	rest_until = now + BG_RESTING;
}

void bg_tick(void)
{
	unsigned long now = tos_ms();
	short x, y, b, ks;
	graf_mkstate(&x, &y, &b, &ks);
	if (last_x >= 0 && (x != last_x || y != last_y || b || ks != last_ks))
		rest(now);
	last_x = x;
	last_y = y;
	last_ks = ks;
	/* our timer came back a second late or more: the program in front
	   kept the machine, so it has work of its own */
	if (asked && now - asked_at > asked + 1000UL)
		rest(now);
	asked = 0;
	if (now - twin_at >= 1000UL || !twin_at) {
		twin_at = now ? now : 1;
		twin_runs = twin_prg() >= 0;
	}
}

int bg_may_work(void)
{
	unsigned long now = tos_ms();
	if (!opened) {
		if (start == BG_OPENED)
			return 0;
		if (now < (unsigned long)start * 1000UL)
			return 0;
	}
	if ((long)(now - rest_until) < 0)
		return 0;
	return !twin_runs;
}

void bg_begin(void)
{
	slice_end = tos_ms() + slice_len;
}

int bg_room(unsigned long ms)
{
	return (long)(slice_end - tos_ms()) > (long)ms;
}

int bg_more(void)
{
	return bg_room(0);
}

unsigned long bg_timer(int pending, unsigned long idle_ms)
{
	unsigned long t = idle_ms;
	if (pending)
		t = bg_may_work() ? BG_PACE : (idle_ms < 500UL ? idle_ms : 500UL);
	asked = t;
	asked_at = tos_ms();
	return t;
}
