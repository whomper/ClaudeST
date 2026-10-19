/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspec.c - the Falcon's DSP56001 for src/ecdsp.c: loads dsp/ec.a56
 * through the DSP's bootstrap and trades command words with it through
 * the host port, as dsprsa.c does for RSA. The DSP is locked with the
 * XBIOS while a job runs; a busy or missing DSP means BearSSL does it.
 * A job is one key exchange or signature: thousands of commands, up to
 * about a second.
 */
#include "tos.h"
#include "dspec.h"
#include "plat.h"
#include "../dsp/ec_prog.h"

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

/* in supervisor mode. Each word waits at most a second: a product takes
   the DSP well under a millisecond */
static void put(unsigned long w)
{
	unsigned long t0;
	if (ecdsp_failed)
		return;
	if (!(HISR & 2)) {
		t0 = HZ200;
		while (!(HISR & 2))
			if (HZ200 - t0 > 200) {
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
			if (HZ200 - t0 > 200) {
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
	long ssp;
	int i;
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
	ssp = trap1_wl(0x20, 0L);
	for (i = 0; i < 512; i++)		/* the bootstrap loads 512 words and starts them */
		put(dsp_ec_prog[i]);
	if (!ecdsp_failed)
		fn(ctx);
	trap1_wl(0x20, ssp);			/* back to user mode */
	if (lock == 0)
		trap14_w(105);			/* Dsp_Unlock */
	return 1;
}

const ECDSP_PORT dspec_port = { run, put, get };
