/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_bell.c - ui/bell.c on the build machine. Each kind of machine is
 * set up by its cookies, its sound registers (an array here) and what
 * Locksnd and Dosound answer; the test checks what the bell does, what it
 * leaves alone when someone else holds the sound, and what it puts back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "bell.h"

const signed char bell_snd[64] = { 0, 1, 2, 3 };
const long bell_len = 64;

static unsigned char regs[0x40];
volatile unsigned char *bell_regs = regs;
unsigned long bell_st_top = ~0UL;	/* all of the host's memory "ST RAM" */

static long c_snd, c_mch;
static long lock_answer, dosound_now;
static int locked, unlocks, devconnects, dosounds, core_calls;
static char last[64];
static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

long get_cookie(long id)
{
	return id == 0x5F534E44L ? c_snd : id == 0x5F4D4348L ? c_mch : 0;
}

long Supexec(void (*f)(void))
{
	f();
	return 0;
}

void nf_debug(const char *s)
{
	snprintf(last, sizeof(last), "%s", s);
	if (*last && last[strlen(last) - 1] == '\n')
		last[strlen(last) - 1] = 0;	/* the line end: checked apart */
}

static char core_buf[64];
void *acc_core(long n, int mode)
{
	(void)n;
	(void)mode;
	core_calls++;
	return core_buf;
}

long acc_free(void *p)
{
	(void)p;
	return 0;
}

long bell_xbios(short fn, short a, short b, long c, long d)
{
	(void)c;
	(void)d;
	switch (fn) {
	case 128:
		if (lock_answer == 1)
			locked = 1;
		return lock_answer;
	case 129:
		locked = 0;
		unlocks++;
		return 0;
	case 139:
		devconnects++;
		regs[0x30] = 0x11;	/* the matrix as Devconnect leaves it */
		regs[0x35] = 0;
		return 0;
	case 28:
		return b == 7 ? 0xC0 | 0x3F : 0;	/* R7: ports out, all off */
	case 32:
		if (a == -1 && b == -1)
			return dosound_now;
		dosounds++;
		return 0;
	}
	(void)a;
	return 0;
}

static void machine(long snd, long mch)
{
	memset(regs, 0, sizeof(regs));
	c_snd = snd;
	c_mch = mch;
	lock_answer = 1;
	dosound_now = 0;
	locked = unlocks = devconnects = dosounds = 0;
	bell_init();
}

int main(void)
{
	/* an ST with TOS 1.04: no cookies at all; the YM */
	machine(0, 0);
	bell_ring(1);
	CHECK(!strcmp(last, "BELL ym") && dosounds == 1, "TOS 1.04 ST: %s, %d", last, dosounds);
	dosound_now = 0x1234;
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: busy") && dosounds == 1, "YM still playing: %s", last);

	/* an ST with TOS 1.06+ (_MCH 0, _SND 1) */
	machine(1, 1);
	bell_ring(1);
	CHECK(!strcmp(last, "BELL ym"), "ST: %s", last);

	/* off */
	bell_ring(0);
	CHECK(!strcmp(last, "BELL silent: off"), "off: %s", last);

	/* an STE: DMA, the mode put back once it has played */
	machine(3, 0x10000);
	regs[0x21] = 0x03;		/* someone's stereo 50 kHz */
	bell_ring(1);
	CHECK(!strcmp(last, "BELL dma") && regs[0x01] == 1 && regs[0x21] == 0x81, "STE: %s %x %x", last,
	      regs[0x01], regs[0x21]);
	bell_poll();
	CHECK(regs[0x21] == 0x81, "STE: the mode kept while it plays");
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: busy"), "STE: its own still ringing: %s", last);
	regs[0x01] = 0;			/* the hardware: played */
	bell_poll();
	CHECK(regs[0x21] == 0x03, "STE: the mode put back: %x", regs[0x21]);

	/* an STE while someone else plays: silent, nothing touched */
	machine(3, 0x10000);
	regs[0x01] = 3;
	regs[0x21] = 0x02;
	regs[0x03] = 0x12;
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: busy") && regs[0x21] == 0x02 && regs[0x03] == 0x12 && regs[0x01] == 3,
	      "STE busy: %s, left alone", last);

	/* a Falcon (_SND 0x3F): locked, the matrix saved and put back, unlocked */
	machine(0x3F, 0x30000);
	regs[0x20] = 0x00;
	regs[0x21] = 0x42;
	regs[0x30] = 0x99;
	regs[0x35] = 0x03;
	bell_ring(1);
	CHECK(!strcmp(last, "BELL dma") && locked && devconnects == 1 && regs[0x01] == 1,
	      "Falcon: %s, locked %d", last, locked);
	regs[0x01] = 0;
	bell_poll();
	CHECK(!locked && unlocks == 1 && regs[0x21] == 0x42 && regs[0x30] == 0x99 && regs[0x35] == 0x03,
	      "Falcon: put back and unlocked: %x %x %x, unlocks %d", regs[0x21], regs[0x30], regs[0x35], unlocks);
	bell_poll();
	CHECK(unlocks == 1, "Falcon: unlocked once");

	/* a program ending while it rings: stopped, put back, unlocked */
	machine(0x3F, 0x30000);
	regs[0x30] = 0x55;
	bell_ring(1);
	bell_free();
	CHECK(regs[0x01] == 0 && regs[0x30] == 0x55 && !locked, "Falcon: freed while ringing: %x %x %d",
	      regs[0x01], regs[0x30], locked);

	/* a Falcon whose sound another program holds (EPlay): silent */
	machine(0x3F, 0x30000);
	lock_answer = -128;		/* ESNDLOCKED */
	regs[0x30] = 0x77;
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: busy") && devconnects == 0 && regs[0x30] == 0x77 && !unlocks,
	      "Falcon locked by another: %s", last);

	/* locked, but the DMA already playing: silent, unlocked again */
	machine(0x3F, 0x30000);
	regs[0x01] = 1;
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: busy") && !locked && unlocks == 1 && devconnects == 0,
	      "Falcon DMA busy: %s", last);

	/* a machine with neither (a FireBee: its _SND says) */
	machine(0x20, 0x50000);
	bell_ring(1);
	CHECK(!strcmp(last, "BELL silent: none"), "none: %s", last);

	/* the image in TT RAM: a copy in ST RAM through acc_core */
	bell_st_top = (unsigned long)bell_snd;	/* the image just above */
	core_calls = 0;
	machine(3, 0x20000);
	CHECK(core_calls == 1, "TT RAM image: acc_core asked once: %d", core_calls);
	bell_free();
	bell_st_top = ~0UL;

	printf("bell: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
