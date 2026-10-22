/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * CRASHT.PRG - checks the shared crash catcher (atari/crash.c) in an
 * emulator. C:\CRASHT.INF says what to do:
 *   b  a bus error in the program        a  an address error (68000)
 *   i  an illegal instruction            z  a division by zero
 *   k  a bus error in supervisor mode in the program's own code (a
 *      Supexec of its): caught as in user mode
 *   f  line F (an opcode for a coprocessor that isn't there), where vector
 *      11 has TOS's own handler; elsewhere (TOS 1.0x's own) "not ours"
 *   e  no crash: a software FPU emulator on vector 11 before the catcher
 *      runs the program's line F instruction (skips it, d0 = 42); the
 *      catcher must leave line F to it
 *   w  a wild jump: user mode, an illegal instruction outside the
 *      program, with another program's handler on vector 4 before the
 *      catcher's (as a driver's on a DFB1): caught, as the program is the
 *      process running
 *   r  ten times in one boot: CRASHT.PRG started again (Pexec, its
 *      command line "k") and caught in supervisor mode; before each and
 *      after the last, TOS's savptr ($4A2) and the supervisor stack in a
 *      Supexec, which must not drift (an abandoned Supexec's state put
 *      back)
 *   s  a bus error in supervisor mode outside the program: the bombs,
 *      which TOS records; the run after the next reset finds the record
 *      and, since it wasn't in the program, doesn't count it as its own
 *      (crash_previous)
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

/* code copied into a Malloc'ed block, outside the program */
static void *outside(const unsigned short *code, long n)
{
	void *m = Malloc(n);
	if ((long)m <= 0) {
		nf_debug("CRASHT no memory\n");
		return 0;
	}
	memcpy(m, code, n);
	return m;
}
/* s: tst.l $FFFFFA40 (nothing there); rts */
static const unsigned short sup_out[] = { 0x4AB9, 0xFFFF, 0xFA40, 0x4E75 };
/* w: illegal */
static const unsigned short wild[] = { 0x4AFC };
/* w: another program's handler on vector 4, before the catcher: on to
   the one before it (move.l old(pc),-(sp); rts; old) */
static const unsigned short pass4[] = { 0x2F3A, 0x0004, 0x4E75, 0x0000, 0x0000 };
static unsigned short *pass4_block;
static volatile long *volatile v4 = (volatile long *)0x10L;

/* r: savptr and the supervisor stack in user mode: Super(0) goes to
   supervisor mode and gives the supervisor stack it had (a Supexec runs on
   the caller's own stack, so its sp is no measure of it) */
static void probe_say(int i)
{
	char s[80];
	long savptr, ssp = trap1_wl(0x20, 0);		/* Super(0) */
	__asm__ volatile("move.l 0x4a2.w,%0" : "=d"(savptr));
	trap1_wl(0x20, ssp);				/* Super(ssp): back */
	snprintf(s, sizeof(s), "CRASHT probe %d savptr %08lX ssp %08lX\n", i, savptr, ssp);
	nf_debug(s);
}

/* v: the exception vectors as TOS left them */
static long vecs[12];
static void sup_vectors(void)
{
	int i;
	for (i = 2; i <= 11; i++)
		vecs[i] = *(volatile long *)(long)(i * 4);
}

/* e: an FPU emulator's handler: addq.l #2,2(sp) (past the instruction);
   moveq #42,d0; rte */
static const unsigned short fpu_emu[] = { 0x54AF, 0x0002, 0x702A, 0x4E73 };
static unsigned short *emu_block;
static long old11, now11;
static volatile long *volatile v11 = (volatile long *)0x2CL;

static void emu_hook(void)
{
	old11 = *v11;
	*v11 = (long)emu_block;
}

static void emu_unhook(void)
{
	*v11 = old11;
}

static unsigned short tos_version;
static void sup_v11(void)
{
	const unsigned char *h;
	now11 = *v11;
	__asm__ volatile("move.l 0x4f2.w,%0" : "=a"(h));	/* _sysbase */
	tos_version = *(const unsigned short *)(h + 2);
}

static void pass4_hook(void)		/* supervisor mode */
{
	*(long *)(pass4_block + 3) = *v4;
	*v4 = (long)pass4_block;
}

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
	{
		/* started by 'r': the kind from the command line */
		const unsigned char *cmd = (const unsigned char *)_BasePage + 0x80;
		if (cmd[0] > 0)
			kind = (char)cmd[1];
	}
	if (kind == 'v') {		/* what TOS puts on vectors 2 to 11 */
		char t[200];
		int i, n = 0;
		Supexec(sup_vectors);
		for (i = 2; i <= 11; i++)
			n += snprintf(t + n, sizeof(t) - n, " %d:%08lX", i, vecs[i]);
		nf_debug("CRASHT vectors");
		nf_debug(t);
		snprintf(t, sizeof(t), " _CPU %ld _FPU %08lX\nCRASHT vectors done\n", get_cookie(0x5F435055L),
			 get_cookie(0x5F465055L));
		nf_debug(t);
		return 0;
	}
	if (kind == 'r') {
		int i;
		for (i = 0; i < 10; i++) {
			probe_say(i);
			trap1_wwlll(0x4b, 0, (long)"C:\\CRASHT.PRG", (long)"\001k", 0);	/* Pexec */
		}
		probe_say(10);
		nf_debug("CRASHT repeat done\n");
		return 0;
	}

	if (kind == 'p') {		/* STinG is there before the program */
		sting_block = Malloc(sizeof(sting_code));
		if ((long)sting_block <= 0) {
			nf_debug("CRASHT no memory\n");
			return 1;
		}
		memcpy(sting_block, sting_code, sizeof(sting_code));
		Supexec(sting_hook);
	}
	if (kind == 'e') {		/* an FPU emulator is there first */
		if (!(emu_block = outside(fpu_emu, sizeof fpu_emu)))
			return 1;
		Supexec(emu_hook);
	}
	if (kind == 'w') {		/* a driver's handler is there first */
		if (!(pass4_block = outside(pass4, sizeof pass4)))
			return 1;
		Supexec(pass4_hook);
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
	case 'k':
		Supexec(sup_bus);
		break;
	case 'f': {
		extern void crash_e11(void);
		Supexec(sup_v11);
		if (now11 != (long)crash_e11) {
			char t[80];
			snprintf(t, sizeof(t), "CRASHT line F not ours: TOS %04X, vector 11 %08lX\n",
				 (unsigned)tos_version, now11);
			nf_debug(t);
			crash_remove();
			return 0;
		}
		__asm__ volatile(".short 0xFE00");	/* coprocessor 7: none */
		break;
	}
	case 'e': {
		long r;
		__asm__ volatile("moveq #0,%%d0\n\t.short 0xFE00\n\tmove.l %%d0,%0" : "=d"(r) : : "d0");
		crash_remove();
		Supexec(emu_unhook);
		nf_debug(r == 42 ? "CRASHT emulator ok\n" : "CRASHT emulator wrong\n");
		return 0;
	}
	case 'w': {
		void (*f)(void) = (void (*)(void))outside(wild, sizeof wild);
		if (f)
			f();
		break;
	}
	case 's': {
		void *f = outside(sup_out, sizeof sup_out);
		put("C:\\STAGE1.TXT", "s\r\n", 3);
		if (f)
			Supexec(f);
		break;
	}
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
