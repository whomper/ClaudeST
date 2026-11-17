/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspec.c - the Falcon's DSP56001 for src/ecdsp.c: loads dsp/ec.a56
 * through the DSP's bootstrap and trades command words with it through
 * the host port, as dsprsa.c does for RSA. The DSP is locked with the
 * XBIOS while a job runs; a busy or missing DSP means BearSSL does it.
 * A job is one key exchange or signature: thousands of commands, up to
 * about a second on a stock Falcon. Every 40 ms of it the 68030 goes back
 * to user mode for a turn of pf_idle (the guidelines' Speed: EUpdate's
 * handshakes held a stock Falcon 1.3 to 1.7 s), the DSP kept locked and
 * waiting where it was: another program's job meanwhile finds it busy
 * and does its own with BearSSL. The turns come by the clock, not by the
 * commands, and the commands don't depend on the secret.
 */
#include "tos.h"
#include "dspec.h"
#include "plat.h"
#include "../dsp/ec_prog.h"
#include "../src/trail.h"
#ifdef TLS_TRAIL
#include <stdio.h>
void conn_log(const char *who, const char *dir, const char *text, long n);
static unsigned long tr_a, tr_b, tr_c, tr_d, tr_first;	/* HZ200 at the job's steps */
#endif

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200   (*(volatile unsigned long *)0x4baL)
#define HISR    (*(volatile unsigned char *)0xffffa202L)
#define HTXH    (*(volatile unsigned char *)0xffffa205L)
#define HTXM    (*(volatile unsigned char *)0xffffa206L)
#define HTXL    (*(volatile unsigned char *)0xffffa207L)

long get_cookie(long id);

int dspec_present(void)
{
	return (get_cookie(0x5F534E44L) & 8) != 0;	/* _SND: a DSP56001 */
}

static void pause_ms(unsigned long ms)
{
	unsigned long t0 = pf_ms();
	while (pf_ms() - t0 < ms)
		;
}

static long run_ssp;			/* Super(0)'s, while a job runs */
static unsigned long turn_at;		/* _hz_200 at the last turn */
static int turns_on;			/* the program loaded, the job's commands going */
unsigned short dspec_turns;
unsigned long dspec_turn_ms;
extern unsigned long tls_turn_ms;	/* tls.c: when the last turn came back (0: not known) */

/* a turn for GEM: back to user mode on the same stack, pf_idle, and
   supervisor mode again. Super(ssp) from supervisor mode leaves the user
   stack pointer where the job's Super(0) left it, above this call's
   frames, which pf_idle's would then overwrite: so USP is set to the
   stack as it is first, and the stack pointer put back after each trap
   whatever TOS did with it. Super(0) then takes the user stack as it is */
/* Asked at every word sent, so the clock is _hz_200 itself, read here in
   supervisor mode: pf_ms() is tos_ms(), a Supexec, an XBIOS trap through
   every driver hooked on it (NVDI's, the network's) for each of the 4,800
   words of an X25519, which on Erez's stock Falcon took 2.1 s where the
   DSP's own work is 227 ms (tests/ec's sim). 8 steps are 40 ms (26 where
   a driver runs the counter at 312 Hz: a few turns more). pf_ms() only at
   a turn, to time it for the log */
static int turn(void)
{
	unsigned long t0;
	if (!turns_on || !pf_idle || HZ200 - turn_at < 8)
		return 0;
	t0 = pf_ms();
	__asm__ volatile(
		"move.l %1,%%a3\n\t"
		"move.l %%sp,%%a4\n\t"
		"move.l %%a4,%%usp\n\t"
		"move.l %0,-(%%sp)\n\t"
		"move.w #0x20,-(%%sp)\n\t"
		"trap #1\n\t"			/* Super(run_ssp): user mode */
		"move.l %%a4,%%sp\n\t"
		"jsr (%%a3)\n\t"			/* pf_idle */
		"clr.l -(%%sp)\n\t"
		"move.w #0x20,-(%%sp)\n\t"
		"trap #1\n\t"			/* Super(0): supervisor on this stack */
		"move.l %%a4,%%sp\n\t"
		"move.l %%d0,%0"
		: "+m"(run_ssp)
		: "m"(pf_idle)
		: "d0", "d1", "d2", "a0", "a1", "a2", "a3", "a4", "memory", "cc");
	turn_at = HZ200;
	dspec_turns++;
	dspec_turn_ms += pf_ms() - t0;
	return 1;
}

/* in supervisor mode. Each word waits at most a second of the DSP's
   time: a product takes it well under a millisecond, a whole ladder
   (RUN) a quarter of a second. GEM has its turns meanwhile, the DSP
   going on with its work, and a turn's time isn't counted */
static void put(unsigned long w)
{
	unsigned long t0;
	if (ecdsp_failed)
		return;
#ifdef TLS_TRAIL
	if (turns_on && !tr_first)
		tr_first = HZ200;
#endif
	turn();
	if (!(HISR & 2)) {
		t0 = HZ200;
		while (!(HISR & 2))
			if (turn())
				t0 = HZ200;
			else if (HZ200 - t0 > 200) {
				ecdsp_failed = 1;
				return;
			}
	}
	HTXH = (unsigned char)(w >> 16);
	HTXM = (unsigned char)(w >> 8);
	HTXL = (unsigned char)w;
}

static unsigned long get(void)
{
	unsigned long w, t0;
	if (ecdsp_failed)
		return 0;
	if (!(HISR & 1)) {
		t0 = HZ200;
		while (!(HISR & 1))
			if (turn())
				t0 = HZ200;
			else if (HZ200 - t0 > 200) {
				ecdsp_failed = 1;
				return 0;
			}
	}
	w = (unsigned long)HTXH << 16;
	w |= (unsigned long)HTXM << 8;
	w |= HTXL;
	return w;
}

static int run(void (*fn)(void *), void *ctx)
{
	int i;
	unsigned long ago = tls_turn_ms ? pf_ms() - tls_turn_ms : 1000, hz_end;
	int lock = (int)trap14_w(104);		/* Dsp_Lock */
	if (lock == -1)
		return 0;			/* another program has it */
	/* reset: bit 4 of the sound chip's port A, through the XBIOS (see
	   dsprsa.c) */
	trap14_ww(30, 0x10);			/* Ongibit: hold the DSP in reset */
	pause_ms(5);
	trap14_ww(29, ~0x10);			/* Offgibit: it starts its bootstrap */
	pause_ms(5);
	/* supervisor mode, for the host port: Super(0) rather than Supexec,
	   so the job (about 1 KB of stack) runs on this program's stack, not
	   on whatever system stack Supexec is given */
	run_ssp = trap1_wl(0x20, 0L);
	/* the 8 steps of a turn are counted from the last turn of whatever
	   kind (tls.c's stamp), not from here: the job's first turn comes
	   when 40 ms have passed since it, 0 steps from now if they have */
	turn_at = HZ200 - (ago >= 40 ? 8 : ago / 5);
#ifdef TLS_TRAIL
	tr_a = HZ200;
	tr_first = 0;
#endif
	dspec_turns = 0;
	dspec_turn_ms = 0;
	for (i = 0; i < 512; i++)		/* the bootstrap loads 512 words and starts them */
		put(dsp_ec_prog[i]);
#ifdef TLS_TRAIL
	tr_b = HZ200;
#endif
	if (!ecdsp_failed) {
		turns_on = 1;
		fn(ctx);
		turns_on = 0;
	}
	hz_end = HZ200;
#ifdef TLS_TRAIL
	tr_d = hz_end;
#endif
	/* back to user mode on this stack: after a turn, the user stack
	   pointer TOS keeps is the turn's, deeper in the calls */
	__asm__ volatile(
		"move.l %%sp,%%a4\n\t"
		"move.l %%a4,%%usp\n\t"
		"move.l %0,-(%%sp)\n\t"
		"move.w #0x20,-(%%sp)\n\t"
		"trap #1\n\t"
		"move.l %%a4,%%sp"
		:
		: "m"(run_ssp)
		: "d0", "d1", "d2", "a0", "a1", "a2", "a4", "memory", "cc");
	if (lock == 0)
		trap14_w(105);			/* Dsp_Unlock */
	/* the last turn, as far back as the steps say (5 ms each at the
	   nominal 200 Hz: where a driver runs the counter faster this is
	   later than it was, a turn sooner) */
	tls_turn_ms = pf_ms() - (hz_end - turn_at) * 5;
#ifdef TLS_TRAIL
	{
		char m[100];
		int n = snprintf(m, sizeof(m), "ec steps: ago %lu ms, load %lu, to first put %lu, fn %lu (ticks)", ago,
				 tr_b - tr_a, tr_first ? tr_first - tr_b : 0UL, tr_d - tr_b);
		conn_log("TRAIL", " -- ", m, n);
	}
#endif
	return 1;
}

const ECDSP_PORT dspec_port = { run, put, get, dsp_ec_prog_ext, DSP_EC_PROG_EXT_AT, DSP_EC_PROG_EXT_N };
