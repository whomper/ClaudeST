/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * HEAPT.PRG - the heap (atari/libc.c) on an Atari, in an emulator, with
 * TOS's own GEMDOS: first a mix of calls with every block's bytes
 * checked (on a 68000 a block at an odd address bombs), then a run
 * shaped like ECal reading a calendar: per event a few blocks kept and
 * more freed again at once. It says on the emulator's console (NatFeats)
 * how it went and how long the second part took, in 200 Hz ticks.
 * run.py drives it in Hatari.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"

static unsigned long seed = 1;

static unsigned rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (unsigned)(seed >> 16) & 0x7FFF;
}

static long ticks;

static long read_hz200(void)
{
	__asm__ volatile ("move.l 0x4ba.w,%0" : "=d" (ticks));	/* _hz_200 */
	return 0;
}

static long now(void)
{
	Supexec(read_hz200);
	return ticks;
}

#define SLOTS 1500

static unsigned char *slot[SLOTS];
static unsigned short len[SLOTS], tag[SLOTS];

static int intact(int i)
{
	unsigned n;
	for (n = 0; n < len[i]; n++)
		if (slot[i][n] != (unsigned char)(tag[i] + n))
			return 0;
	return 1;
}

static void fill(int i)
{
	unsigned n;
	for (n = 0; n < len[i]; n++)
		slot[i][n] = (unsigned char)(tag[i] + n);
}

static unsigned some_size(void)
{
	unsigned r = rnd() % 100;
	return r < 70 ? rnd() % 80 : r < 95 ? rnd() % 1200 : rnd() % 12000;
}

static char why[60];

static int wrong(const char *what, long c)
{
	snprintf(why, sizeof(why), "%s at call %ld", what, c);
	return 0;
}

/* calls with every block's bytes checked; 0 when something is wrong */
static int mix(long calls)
{
	long c;
	int i;
	unsigned short t = 1;
	for (c = 0; c < calls; c++) {
		unsigned r = rnd() % 100;
		i = (int)(rnd() % SLOTS);
		if (r < 55 && !slot[i]) {
			len[i] = (unsigned short)some_size();
			tag[i] = t++;
			if (!(slot[i] = r < 5 ? calloc(len[i], 1) : malloc(len[i])))
				return wrong("no memory", c);
			if (r < 5) {
				unsigned n;
				for (n = 0; n < len[i]; n++)
					if (slot[i][n])
						return wrong("calloc not zeroed", c);
			}
			fill(i);
		} else if (r < 90 && slot[i]) {
			if (!intact(i))
				return wrong("bytes changed", c);
			free(slot[i]);
			slot[i] = 0;
		} else if (slot[i]) {
			unsigned n = some_size(), keep = n < len[i] ? n : len[i], k;
			unsigned char *p;
			if (!intact(i))
				return wrong("bytes changed", c);
			if (!(p = realloc(slot[i], n)))
				return wrong("realloc: no memory", c);
			for (k = 0; k < keep; k++)
				if (p[k] != (unsigned char)(tag[i] + k))
					return wrong("realloc lost bytes", c);
			slot[i] = p;
			len[i] = (unsigned short)n;
			tag[i] = t++;
			fill(i);
		}
	}
	for (i = 0; i < SLOTS; i++)
		if (slot[i]) {
			if (!intact(i))
				return wrong("bytes changed", c);
			free(slot[i]);
			slot[i] = 0;
		}
	return 1;
}

/* per event: three blocks kept (its fields), eight freed again (the
   lines read for it); then everything freed, as when a calendar goes */
static void *kept[3 * 2000];

static void events(int n)
{
	void *tmp[8];
	int e, k, m = 0;
	for (e = 0; e < n; e++) {
		for (k = 0; k < 8; k++)
			tmp[k] = malloc(16 + rnd() % 180);
		for (k = 0; k < 3; k++)
			kept[m++] = malloc(20 + rnd() % 100);
		for (k = 0; k < 8; k++)
			free(tmp[(k * 5) & 7]);
	}
	for (k = 0; k < m; k++)
		free(kept[k]);
}

int main(void)
{
	char s[80];
	long t0, t1;
	nf_debug("HEAPT start\n");
	if (!mix(20000)) {
		snprintf(s, sizeof(s), "HEAPT mix FAIL: %s\n", why);
		nf_debug(s);
		return 1;
	}
	nf_debug("HEAPT mix ok\n");
	t0 = now();
	events(1500);
	events(1500);
	t1 = now();
	snprintf(s, sizeof(s), "HEAPT events %ld ticks\n", t1 - t0);
	nf_debug(s);
	nf_debug("HEAPT done\n");
	return 0;
}
