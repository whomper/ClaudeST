/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bell.c - a soft bell (EMail's new mail first, decided 27 Nov 2026: a
 * soothing bell, not a harsh one, with a bit of reverb). The family's rules
 * for sound (the hub, 27 Nov 2026):
 *
 * - Never take sound from someone else (EPlay drives the DMA and the DSP
 *   from interrupts). With the Falcon's XBIOS sound calls (_SND bit 2) the
 *   bell rings only if Locksnd() succeeds; on an STE or TT (bit 1 alone)
 *   not while the DMA's play bit is already set; on the YM not while
 *   Dosound is still playing. Busy means silent: no waiting, no retry.
 * - Put everything back: what it changes (the DMA's mode; on the Falcon
 *   its matrix and prescale registers, read before and written back) goes
 *   back once the bell has ended, at bell_poll, which also unlocks. The
 *   volume is never touched: the sample itself peaks at about -12 dBFS.
 * - The YM chime goes only through Dosound and ends by silencing its
 *   voices; register 7's I/O bits (port A: the floppy's select) stay as
 *   they were.
 * - Never block: it starts the sound and returns.
 * - Nothing on a machine with neither (the FireBee: _SND says).
 * - One NatFeats line each time: "BELL dma", "BELL ym", or "BELL silent:"
 *   and why (off, busy, none).
 *
 * The sample (bell_snd, bell_len: 8-bit signed mono at 12517 Hz, the rate
 * the STE, TT and Falcon share) is made at build time by
 * common/tools/mkbell.py. DMA reads ST RAM only: where the image is in TT
 * RAM, bell_init copies it into a block of ST RAM taken through acc_core.
 */
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "bell.h"

extern const signed char bell_snd[];	/* mkbell.py */
extern const long bell_len;

#ifdef BELL_HOST
/* common/tests/bell: the calls, the registers and where ST RAM ends, as the
   test sets them */
long bell_xbios(short fn, short a, short b, long c, long d);
extern volatile unsigned char *bell_regs;
extern unsigned long bell_st_top;
#define ST_TOP bell_st_top
#else
#define ST_TOP 0x01000000UL
static long bell_xbios(short fn, short a, short b, long c, long d)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %5,-(%%sp)\n\tmove.l %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\t"
			 "move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\tlea 14(%%sp),%%sp"
			 : "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c), "r"(d) : TRAP_CLOBBER);
	return r;
}
static volatile unsigned char *const bell_regs = (volatile unsigned char *)0xFFFF8900L;
#endif

/* the XBIOS calls (each reads only the words it wants from the stack) */
#define Locksnd()		bell_xbios(128, 0, 0, 0, 0)
#define Unlocksnd()		bell_xbios(129, 0, 0, 0, 0)
/* Devconnect(DMAPLAY, DAC, the 25.175 MHz clock, prescale 0: the STE
   rates, 1: no handshake): the last two words in the one long */
#define Devconnect_dma_dac()	bell_xbios(139, 0, 8, 0, 0x00010000L)
#define Giaccess(d, r)		bell_xbios(28, (d), (r), 0, 0)
#define Dosound(p)		bell_xbios(32, (short)((long)(p) >> 16), (short)(long)(p), 0, 0)

enum { NONE, DMA, FALCON, YM };
static short how;
static short playing;		/* rung, its settings to put back */
static unsigned char saved[0x40];	/* the sound registers as they were */
static const signed char *dma_from;
static signed char *st_copy;
static char why[1];		/* set in supervisor mode: 'b' busy, 'e' ended */

/* the YM chime: register, value pairs; 0xFF n waits n 50 Hz ticks, 0xFF 0
   ends (Dosound's table). A and B a fifth apart (E5, B5), C the room around
   them: E5 a hair flat, a tenth of a second later, quieter, fading more
   slowly (about 2.2 s). R7 (index 15, and the one before the end) is
   written at ring time with the port bits as they are */
static const unsigned char ym_chime[] = {
	0, 190, 1, 0,		/* A: 125000 / 190 = 658 Hz */
	2, 127, 3, 0,		/* B: 984 Hz */
	4, 192, 5, 0,		/* C: 651 Hz */
	10, 0, 7, 0x38,
	8, 10, 9, 7, 0xFF, 5,
	8, 9, 9, 6, 10, 6, 0xFF, 6,
	8, 8, 9, 5, 0xFF, 7,
	8, 7, 9, 4, 10, 5, 0xFF, 8,
	8, 6, 9, 3, 0xFF, 9,
	8, 5, 9, 2, 10, 4, 0xFF, 10,
	8, 4, 9, 1, 0xFF, 11,
	8, 3, 9, 0, 10, 3, 0xFF, 12,
	8, 2, 0xFF, 10,
	8, 1, 10, 2, 0xFF, 10,
	8, 0, 0xFF, 10,
	10, 1, 0xFF, 12,
	8, 0, 9, 0, 10, 0,
	7, 0x3F,		/* the tones off; the port bits put in at ring time */
	0xFF, 0
};
#define YM_R7_ON  15			/* the value after "7," at the start */
#define YM_R7_OFF (sizeof(ym_chime) - 3)
static unsigned char ym_table[sizeof(ym_chime)];

static void note(const char *what)
{
	nf_debug(what);
}

void bell_init(void)
{
	long snd = get_cookie(0x5F534E44L);	/* _SND */
	how = NONE;
	/* TOS 1.00-1.04 sets no cookies (a jar there is a driver's): with no
	   _MCH either, an ST, which has the YM */
	if (!snd && !get_cookie(0x5F4D4348L))	/* _MCH */
		snd = 1;
	if (snd & 2) {
		dma_from = bell_snd;
		if ((unsigned long)bell_snd >= ST_TOP) {
			/* in TT RAM: a copy where DMA can read it */
			signed char *p = acc_core(bell_len, 0);
			if ((long)p > 0) {
				memcpy(p, bell_snd, bell_len);
				st_copy = p;
				dma_from = p;
			}
		}
		if ((unsigned long)dma_from < ST_TOP)
			how = (snd & 4) ? FALCON : DMA;
	}
	if (how == NONE && (snd & 1))
		how = YM;
}

/* ---- the STE and TT: the DMA registers, the mode put back after ---- */

static void dma_busy(void)
{
	why[0] = (bell_regs[0x01] & 1) ? 'b' : 0;
}

static void dma_go(void)
{
	unsigned long a = (unsigned long)dma_from, e = a + (unsigned long)bell_len;
	saved[0x21] = bell_regs[0x21];
	bell_regs[0x03] = (unsigned char)(a >> 16);
	bell_regs[0x05] = (unsigned char)(a >> 8);
	bell_regs[0x07] = (unsigned char)a;
	bell_regs[0x0F] = (unsigned char)(e >> 16);
	bell_regs[0x11] = (unsigned char)(e >> 8);
	bell_regs[0x13] = (unsigned char)e;
	bell_regs[0x21] = 0x81;		/* mono, 12517 Hz */
	bell_regs[0x01] = 1;		/* play once */
}

/* ---- the Falcon: locked, its matrix and prescale read and put back ---- */

/* the mode (0x20-0x21) and the matrix and prescale (0x30-0x37): what
   Devconnect and the mode write change. 0x22-0x25, the STE's microwire,
   are never written: a write there shifts it out */
static void falcon_save(void)
{
	short i;
	for (i = 0x20; i <= 0x37; i++)
		if (i < 0x22 || i >= 0x30)
			saved[i] = bell_regs[i];
}

static void falcon_restore(void)
{
	short i;
	for (i = 0x20; i <= 0x37; i++)
		if (i < 0x22 || i >= 0x30)
			bell_regs[i] = saved[i];
}

static void dma_restore(void)
{
	bell_regs[0x21] = saved[0x21];
}

static void dma_ended(void)
{
	why[0] = (bell_regs[0x01] & 1) ? 0 : 'e';
}

void bell_ring(short on)
{
	if (!on) {
		note("BELL silent: off\n");
		return;
	}
	if (playing) {
		note("BELL silent: busy\n");	/* its own, still ringing */
		return;
	}
	switch (how) {
	case FALCON:
		if (Locksnd() != 1) {
			note("BELL silent: busy\n");
			return;
		}
		Supexec(dma_busy);
		if (why[0]) {
			Unlocksnd();
			note("BELL silent: busy\n");
			return;
		}
		Supexec(falcon_save);
		Devconnect_dma_dac();
		Supexec(dma_go);		/* then the same registers as the STE's */
		playing = FALCON;
		note("BELL dma\n");
		return;
	case DMA:
		Supexec(dma_busy);
		if (why[0]) {
			note("BELL silent: busy\n");
			return;
		}
		Supexec(dma_go);
		playing = DMA;
		note("BELL dma\n");
		return;
	case YM:
		if (Dosound(-1L) > 0) {		/* a table still playing */
			note("BELL silent: busy\n");
			return;
		}
		{
			short r7 = (short)(Giaccess(0, 7) & 0xC0);	/* the port bits */
			memcpy(ym_table, ym_chime, sizeof(ym_chime));
			ym_table[YM_R7_ON] = (unsigned char)(r7 | 0x38);
			ym_table[YM_R7_OFF] = (unsigned char)(r7 | 0x3F);
		}
		Dosound(ym_table);
		note("BELL ym\n");
		return;
	default:
		note("BELL silent: none\n");
	}
}

void bell_poll(void)
{
	if (!playing)
		return;
	Supexec(dma_ended);
	if (!why[0])
		return;
	if (playing == FALCON) {
		Supexec(falcon_restore);
		Unlocksnd();
		note("BELL back, unlocked\n");
	} else {
		Supexec(dma_restore);
		note("BELL back\n");
	}
	playing = 0;
}

static void dma_stop(void)
{
	bell_regs[0x01] = 0;
}

void bell_free(void)
{
	if (playing) {
		/* a program ending while it rings: stopped, put back, unlocked */
		Supexec(dma_stop);
		bell_poll();
	}
	if (st_copy) {
		acc_free(st_copy);
		st_copy = 0;
		how = NONE;
	}
}
