/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * TERMTEST.PRG: which way of ending runs a terminate handler (etv_term)
 * under FreeMiNT, for run.py (the crash catcher's, atari/crash.c; the
 * hub, 9 Nov 2026). C:\termt.inf holds one letter (a program under MiNT
 * can't be started with arguments here); each handler and the program
 * say what they do as NatFeats lines ("TRM ..."):
 *
 *   d  the handler put at $408 in supervisor mode (what crash.c did), then Pterm
 *   s  the handler put through Setexc(0x102), then Pterm
 *   t  Setexc(0x102), then the process killed by SIGTERM (Pkill on itself)
 *   k  Setexc(0x102), then killed by SIGKILL
 *   b  Setexc(0x102), then a bus error in the program (MiNT sends SIGBUS)
 *   S T K B  as s t k b, but with an older handler (term 2) on the vector, crash_install over it (crash_term:
 *      atari/crash.c) and the app's (term 1) over that, as EPlay's; a chain is "term 1 ran", then "term 2 ran"
 *      (through crash_term, the only way to it)
 *   c  crash_install (the catcher) and crash_on_term's handler, then Pterm
 *   C  the same, then SIGTERM
 *   K  the same, then SIGKILL
 *   B  the same, then a bus error
 *
 * Every handler says "TRM term N ran" (N its number); "TRM chain" when the
 * second of two ran after the first (the chain), "TRM start MODE" at the
 * start, "TRM before kill" just before the program ends itself.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "crash.h"

static void say(const char *a)
{
	static char line[80];
	strcpy(line, "TRM ");
	strcat(line, a);
	strcat(line, "\n");
	nf_debug(line);
}

long old1, old2;

__attribute__((used)) static void h1c(void)
{
	say("term 1 ran");
}

__attribute__((used)) static void h2c(void)
{
	say("term 2 ran");
}

/* a terminate handler is called as a subroutine: all registers kept, then on to the one before it */
#define TERM_H(name, cfn, oldv) \
	__asm__(".text\n\t.globl " #name "\n" #name ":\n\t" \
		"movem.l %d0-%d7/%a0-%a6,-(%sp)\n\tjsr " #cfn "\n\tmovem.l (%sp)+,%d0-%d7/%a0-%a6\n\t" \
		"tst.l " #oldv "\n\tbeq.s 1f\n\tmove.l " #oldv ",-(%sp)\n1:\trts\n")
void term1(void);
void term2(void);
TERM_H(term1, h1c, old1);
TERM_H(term2, h2c, old2);

static volatile long *low(long a)
{
	volatile long *p;
	__asm__("" : "=a"(p) : "0"(a));
	return p;
}

static void put408(void)
{
	old1 = *low(0x408L);
	*low(0x408L) = (long)term1;
}

static long Pkill(short pid, short sig)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w #0x111,-(%%sp)\n\ttrap #1\n\taddq.l #6,%%sp"
		: "=r"(r) : "0"(0), "r"(pid), "r"(sig) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

static long Setexc_(short n, long v)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w #5,-(%%sp)\n\ttrap #13\n\taddq.l #8,%%sp"
		: "=r"(r) : "0"(0), "r"(n), "r"(v) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

int main(void)
{
	static char buf[4], m[2];
	long h, n;
	char mode = 's';
	appl_init();
	h = Fopen("c:\\termt.inf", 0);
	if (h >= 0) {
		n = Fread((short)h, 1, buf);
		Fclose((short)h);
		if (n == 1)
			mode = buf[0];
	}
	m[0] = mode;
	m[1] = 0;
	{
		static char s[16];
		strcpy(s, "start ");
		strcat(s, m);
		say(s);
	}
	if (mode >= 'A' && mode <= 'Z') {
		/* an older handler (term 2), the catcher on top of it, the app's own (term 1) on top of that: the
		   order of a chain is term 1, the catcher, term 2 */
		old2 = 0;
		Setexc_(0x102, (long)term2);
		crash_install("c:\\", "TERMTEST", "0");
		old1 = Setexc_(0x102, -1L);
		Setexc_(0x102, (long)term1);
		say(old1 == (long)crash_term ? "chain: the catcher is under the app's handler" :
		    "chain: the catcher is NOT under the app's handler");
		mode = (char)(mode + 'a' - 'A');
	} else if (mode == 'd') {
		Supexec(put408);
	} else {
		old1 = Setexc_(0x102, -1L);
		Setexc_(0x102, (long)term1);
	}
	say("before end");
	if (mode == 't')
		Pkill((short)trap1_w(0x10b), 15);	/* SIGTERM on itself */
	if (mode == 'k')
		Pkill((short)trap1_w(0x10b), 9);	/* SIGKILL */
	say("after the kill");
	if (mode == 'b') {
		*low(0x10L) = 0;
	}
	appl_exit();
	return 0;
}
