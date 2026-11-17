/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_crash.c - crash.S's entries as ColdFire code under qemu-m68k -cpu
 * cfv4e, entered through a ColdFire frame (crash_fake.S):
 *   chain: a fault crash_take leaves alone goes on to the handler before
 *          ours with the frame and every register as they were;
 *   take:  one it takes gives the program's registers to crash_regs and
 *          the user stack to crash_usp, drops the frame, and leaves for
 *          crash_landing in user mode through a ColdFire frame (format 4,
 *          the vector, the new SR) on the stack the fault came in on;
 *          the privileged move usp and rte, SIGILL in user mode, are
 *          stood in for and the frame looked at as rte would take it;
 *   line F: one outside the program's code goes on at once, one inside
 *          it to crash_take: its PC read from a ColdFire frame.
 *   test_crash chain|take|linef */
#include "rt.h"
#include "gemdos.h"

typedef unsigned long u32;
long fake_exc(void (*entry)(void), long sr, long pc, long vec);
void old_handler(void);
extern u32 seen_regs[15], seen_sp, frame_at, saved_sp;
extern long crash_slot[12];
void crash_e4(void), crash_e11(void);

/* what crash.c gives crash.S */
short crash_busy, crash_cpu = 40, crash_fvec, crash_newsr;
u32 crash_regs[15], crash_usp, crash_stack_top = 0x00c0ffee & ~3UL, crash_text_lo = 0x1000, crash_text_hi = 0x2000;
static long take_answer, take_vec;
static unsigned short *take_frame;
void crash_landing(void) {}
void crash_restore(void) {}
long crash_take(unsigned short *f, long vec)
{
	take_frame = f;
	take_vec = vec;
	if (take_answer) {
		crash_fvec = (short)(vec * 4);
		crash_newsr = f[1] & 0x0700;
	}
	return take_answer;
}

static int fails;
static void check(const char *what, int ok)
{
	if (!ok) {
		fails++;
		out("crash: FAIL "); out(what); out("\n");
	}
}

static u32 usp_set;
static void sigill(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned short op = *(unsigned short *)uc->gregs[16];
	unsigned short *fr = (unsigned short *)uc->gregs[15];
	(void)sig, (void)info;
	if (op == 0x4e68) {			/* move.l %usp,%a0 */
		uc->gregs[8] = 0x0badcafe;
		uc->gregs[16] += 2;
		return;
	}
	if (op == 0x4e60) {			/* move.l %a0,%usp */
		usp_set = uc->gregs[8];
		uc->gregs[16] += 2;
		return;
	}
	if (op == 0x4e73) {			/* rte: the frame it would take */
		int i;
		check("crash_take was given the frame", take_frame == (unsigned short *)frame_at && take_vec == 4);
		check("format 4 and the vector", fr[0] == (0x4000 | 4 * 4));
		check("the new SR: user mode, the interrupt level kept", fr[1] == 0x0300);
		check("into crash_landing", *(u32 *)(fr + 2) == (u32)crash_landing);
		check("on the stack the fault came in on", (u32)fr + 8 == frame_at + 8);
		check("crash_landing's stack as the user stack", usp_set == crash_stack_top);
		check("the user stack noted", crash_usp == 0x0badcafe);
		for (i = 0; i < 8; i++)
			check("d0-d7 noted", crash_regs[i] == 0xd0d0d0d0UL + 0x01010101UL * (u32)i);
		for (i = 0; i < 7; i++)
			check("a0-a6 noted", crash_regs[8 + i] == 0xa0a0a0a0UL + 0x01010101UL * (u32)i);
		out(fails ? "crash: take failed\n" : "crash: a fault taken leaves for crash_landing through a ColdFire frame\n");
		sys_exit(fails != 0);
	}
	out("crash: SIGILL at "); out_hex(uc->gregs[16]); out("\n");
	sys_exit(3);
}

int main(int argc, char **argv)
{
	int i;
	sig_install(4, sigill);
	if (argc > 1 && !strcmp(argv[1], "take")) {
		take_answer = 8;
		fake_exc(crash_e4, 0x2300, 0x1234, 4);
		out("crash: FAIL: came back\n");
		return 1;
	}
	/* (each handler-before slot, through crash_slot: in the text, which
	   this test links writable) */
	*(long *)crash_slot[4] = (long)old_handler;
	*(long *)crash_slot[11] = (long)old_handler;
	if (argc > 1 && !strcmp(argv[1], "linef")) {
		/* line F outside the program's code (0x1000-0x2000): on at once */
		check("line F outside: on to the handler before", fake_exc(crash_e11, 0x0000, 0x9000, 11) == 1);
		check("line F: the frame as it was", seen_sp == frame_at);
		check("line F: crash_take not asked", take_frame == 0);
		/* and one inside it (its PC read right): crash_take asked */
		check("line F inside: crash_take's, then on", fake_exc(crash_e11, 0x0000, 0x1800, 11) == 1);
		check("line F inside: crash_take asked", take_vec == 11 && take_frame == (unsigned short *)frame_at);
	} else {
		check("on to the handler before", fake_exc(crash_e4, 0x0000, 0x9000, 4) == 1);
		check("crash_take asked", take_vec == 4 && take_frame == (unsigned short *)frame_at);
		check("the frame as it was, on top", seen_sp == frame_at);
		for (i = 0; i < 8; i++)
			check("d0-d7 as they were", seen_regs[i] == 0xd0d0d0d0UL + 0x01010101UL * (u32)i);
		for (i = 0; i < 7; i++)
			check("a0-a6 as they were", seen_regs[8 + i] == 0xa0a0a0a0UL + 0x01010101UL * (u32)i);
	}
	out(fails ? "crash: chain failed\n" : argc > 1 ? "crash: line F outside the program goes on, its PC from a ColdFire frame\n" :
	    "crash: a fault not ours goes on to the handler before with everything as it was\n");
	return fails != 0;
}
