/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * CRASHT.PRG - checks the shared crash catcher (atari/crash.c) in an
 * emulator. C:\CRASHT.INF says what to do:
 *   b  a bus error in the program        a  an address error (68000)
 *   i  an illegal instruction            z  a division by zero
 *   s  a bus error in supervisor mode (in the program's own code): the
 *      bombs, which TOS records; the run after the next reset finds the
 *      record and, since it was in the program, says so (crash_previous)
 *   p  no crash: STinG's way into supervisor mode. Code outside the
 *      program, with a handler of its own on vector 8 put there before
 *      the catcher, runs "or.w #0,sr" in user mode and catches the
 *      privilege violation; the catcher must pass it on (STinG's
 *      protect_exec, in TCP_open: every connection bombed in 1.31)
 * The catcher writes C:\CRASH.LOG; this program notes on the emulator's
 * console (NatFeats) what it saw, and crash_cleanup and crash_report
 * leave C:\CLEANUP.TXT and C:\REPORT.TXT. run.py drives it in Hatari.
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "crash.h"

static char kind;

static void put(const char *path, const char *s, long n)
{
	long h = Fcreate(path, 0);
	if (h >= 0) {
		Fwrite((short)h, n, s);
		Fclose((short)h);
	}
}

static void cleanup(const char *what)
{
	char s[100];
	snprintf(s, sizeof(s), "cleanup: %s\r\n", what);
	put("C:\\CLEANUP.TXT", s, strlen(s));
	nf_debug("CRASHT cleanup\n");
}

static void report(const char *text, long n)
{
	put("C:\\REPORT.TXT", text, n);
}

static void sup_bus(void)
{
	volatile long *p = (volatile long *)0xFFFFFA40L;	/* nothing there */
	(void)*p;
}

static volatile long sink;

/* ---- p: a resident program's privilege violation, as STinG's ----
   Copied into a Malloc'ed block, outside the program: handler, the old
   vector, and prot ("or.w #0,sr; rts"). The handler takes the fault
   when it comes from prot (skips the instruction, d0 = 42), else hands
   it on to the vector before it. */
static const unsigned short sting_code[] = {
	0x2F08,			/*  0 handler: move.l a0,-(sp)      */
	0x41FA, 0x001E,		/*  2 lea prot(pc),a0              */
	0xB1EF, 0x0006,		/*  6 cmpa.l 6(sp),a0 (the PC)     */
	0x660A,			/* 10 bne.s other                  */
	0x58AF, 0x0006,		/* 12 addq.l #4,6(sp): skip it     */
	0x702A,			/* 16 moveq #42,d0                 */
	0x205F,			/* 18 move.l (sp)+,a0              */
	0x4E73,			/* 20 rte                          */
	0x205F,			/* 22 other: move.l (sp)+,a0       */
	0x2F3A, 0x0004,		/* 24 move.l old(pc),-(sp)         */
	0x4E75,			/* 28 rts: to the old handler      */
	0x0000, 0x0000,		/* 30 old                          */
	0x007C, 0x0000,		/* 34 prot: or.w #0,sr             */
	0x4E75			/* 38 rts                          */
};
static unsigned short *sting_block;

static volatile long *volatile v8 = (volatile long *)0x20L;	/* vector 8 */

static void sting_hook(void)		/* supervisor mode */
{
	*(long *)(sting_block + 15) = *v8;	/* old */
	*v8 = (long)sting_block;
}

static void sting_unhook(void)
{
	*v8 = *(long *)(sting_block + 15);
}

int main(void)
{
	char inf[8];
	long h = Fopen("C:\\CRASHT.INF", 0);
	const char *prev;
	if (h < 0) {
		nf_debug("CRASHT no CRASHT.INF\n");
		return 1;
	}
	inf[0] = 0;
	Fread((short)h, 1, inf);
	Fclose((short)h);
	kind = inf[0];

	if (kind == 'p') {		/* STinG is there before the program */
		sting_block = Malloc(sizeof(sting_code));
		if ((long)sting_block <= 0) {
			nf_debug("CRASHT no memory\n");
			return 1;
		}
		memcpy(sting_block, sting_code, sizeof(sting_code));
		Supexec(sting_hook);
	}
	crash_install("C:\\", "CrashTest", "1.0");
	crash_cleanup = cleanup;
	crash_report = report;
	prev = crash_previous();
	if (Fopen("C:\\STAGE1.TXT", 0) >= 0) {		/* after the bombs and a reset */
		char s[100];
		snprintf(s, sizeof(s), "CRASHT previous %s\n", prev ? prev : "none");
		nf_debug(s);
		crash_remove();
		return 0;
	}
	crash_note("about to crash");
	nf_debug("CRASHT crashing\n");
	switch (kind) {
	case 'b': {
		static volatile long *volatile low;
		low = (volatile long *)0x10L;			/* user mode: bus error */
		*low = 0;
		break;
	}
	case 'a': {
		static const char b[4];
		sink = *(volatile const short *)(b + 1);	/* odd: address error on a 68000 */
		break;
	}
	case 'i': {
		static const unsigned short ill[] = { 0x4afc };
		((void (*)(void))ill)();
		break;
	}
	case 'z': {
		volatile short d = 0;
		long q;
		__asm__ volatile("move.l #100,%0\n\tdivs.w %1,%0" : "=&d"(q) : "d"(d));
		sink = q;
		break;
	}
	case 's':
		put("C:\\STAGE1.TXT", "s\r\n", 3);
		Supexec(sup_bus);
		break;
	case 'p': {
		long r = ((long (*)(void))(sting_block + 17))();
		crash_remove();
		Supexec(sting_unhook);
		nf_debug(r == 42 ? "CRASHT protect ok\n" : "CRASHT protect wrong\n");
		return 0;
	}
	}
	nf_debug("CRASHT no crash\n");
	crash_remove();
	return 2;
}
