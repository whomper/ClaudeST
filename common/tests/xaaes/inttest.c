/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * INTTEST.PRG / INTTESTG.PRG: a program that touches its own memory from
 * an interrupt, under FreeMiNT's memory protection (run.py --mem-prot; the
 * hub, 9 Nov 2026: EPlay halted Hatari's Falcon with PRGFLAGS 7 and played
 * with 0x17). The same program built twice, INTTEST.PRG with PRGFLAGS 7
 * (fast load, TT RAM; memory protection bits 4-5 = 0: private) and
 * INTTESTG.PRG with 0x17 (bits 4-5 = 1: global). It puts a routine on the
 * vertical blank queue (vblqueue, $456), whose code and counter are in the
 * program's own memory; the interrupt comes while the AES or another
 * process is the one running, so the MMU context isn't this program's.
 * It then waits 1.5 s and says what the counter reached, as NatFeats
 * lines: "INT start", "INT count N" (N > 0: the interrupt got at the
 * program's memory), "INT done". A machine that halts says nothing more
 * after "INT start".
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

volatile long vbl_count;		/* global: the handler below names it */
void vbl_h(void);
__asm__(".text\n\t.globl vbl_h\nvbl_h:\n\taddq.l #1,vbl_count\n\trts\n");

static volatile long *low(long a)
{
	volatile long *p;
	__asm__("" : "=a"(p) : "0"(a));
	return p;
}

static volatile long *slot;

static void sup_hook(void)
{
	volatile long *q = (volatile long *)*low(0x456L);
	short i, n = (short)((*low(0x454L)) >> 16);	/* nvbls, the word at $454 (the long there: it and $456's high word) */
	for (i = 1; i < n && !slot; i++)
		if (q[i] == 0) {
			q[i] = (long)vbl_h;
			slot = &q[i];
		}
}

static void sup_unhook(void)
{
	if (slot)
		*slot = 0;
}

static void say(const char *a, long v)
{
	static char line[40];
	char t[12];
	int n = 0, k;
	strcpy(line, "INT ");
	strcat(line, a);
	if (v >= 0) {
		strcat(line, " ");
		do
			t[n++] = (char)('0' + v % 10);
		while ((v /= 10) > 0);
		k = (int)strlen(line);
		while (n)
			line[k++] = t[--n];
		line[k] = 0;
	}
	strcat(line, "\n");
	nf_debug(line);
}

int main(void)
{
	short m[8];
	AEVENT e;
	appl_init();
	say("start", -1);
	Supexec(sup_hook);
	if (!slot) {
		say("no free vblqueue slot", -1);
		appl_exit();
		return 1;
	}
	e.which = 0;
	evnt_multi_(MU_TIMER, 0, 0, 0, 1500, m, &e);
	say("count", vbl_count);
	Supexec(sup_unhook);
	say("done", -1);
	appl_exit();
	return 0;
}
