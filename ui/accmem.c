/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * accmem.c - an accessory's memory (see accmem.h), from ECal's and
 * EContact's acc_heap and EContact's mem_note.
 */
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "accmem.h"
#include "gem.h"

long acc_heap_bytes, acc_heap_avail;
void *acc_heap_at;
short acc_heap_tt;
long acc_heap_leave = 32L * 1024;

/* the accessories in the boot drive's root, which TOS loads at boot: at
   least this one */
static int acc_count(void)
{
	/* both on even addresses: TOS writes words and longs into the DTA,
	   and GCC fills an initialised char array with long moves, which
	   at an odd offset (path at sp+39) are an address error on a 68000
	   (EMail 1.65's candidate on TOS 1.04, 27 Oct 2026) */
	long dta[11];
	char path[10] __attribute__((aligned(4))) = "C:\\*.ACC";
	void *was = Fgetdta();
	int n = 0;
	path[0] = (char)('A' + boot_drive());
	Fsetdta(dta);
	if (Fsfirst(path, 0) == 0)
		do
			n++;
		while (n < 64 && Fsnext() == 0);
	Fsetdta(was);
	return n < 1 ? 1 : n;
}

long acc_heap_take(long first, long tt_first, long heapkb, long memkb)
{
	long avail = (long)Malloc(-1L), alt = (long)Mxalloc(-1L, 1), want, cap;
	int known = heapkb >= 64 || memkb >= 32;
	void *p;
	if (alt == -32 || alt < 0)
		alt = 0;
	acc_heap_tt = alt >= 256L * 1024;
	if (acc_heap_tt) {
		avail = alt;		/* TT RAM, when there is some */
		if (tt_first > first)
			first = tt_first;
	}
	want = heapkb >= 64 ? heapkb * 1024 : memkb >= 32 ? memkb * 1024 : first;
	if (want < 64L * 1024)
		want = 64L * 1024;
	/* the rest left to the programs run after, and the other accessories:
	   not known yet, a quarter where 1 MB or more is free (on a 4 MB
	   Falcon with NVDI's fonts only about 1.5 MB is free when the
	   accessories load, and two halves left EMail none for NVDI), and
	   with more than two accessories to load, half of it shared out
	   among them: four first boots of a quarter each took two thirds of
	   that 1.5 MB, and EUpdate found 0 KB for its screen (27 Oct 2026) */
	if (!known && avail >= 1024L * 1024) {
		int n = acc_count();
		cap = avail / (n > 2 ? 2 * n : 4);
	} else {
		cap = avail / 2;
		/* a first boot (nothing measured yet) under 1 MB: half and up to
		   8 KB, as far as acc_heap_leave still leaves. Half of a 1 MB
		   Falcon's ~204 KB left EHypView's front page a picture short by a
		   kilobyte (20 Nov 2026); from the next boot MEMKB= is what was
		   measured */
		if (!known) {
			long more = avail - acc_heap_leave - cap;
			if (more > 8L * 1024)
				more = 8L * 1024;
			if (more > 0)
				cap += more;
		}
	}
	if (want > cap) {
		want = cap & ~1L;
		/* little memory (a 1 MB ST): half is under 96 KB; then all but
		   acc_heap_leave, and never under 64 KB nor more than all but
		   32 KB (what the desktop needs to start) */
		if (want < 96L * 1024) {
			want = (avail - acc_heap_leave) & ~1L;
			if (want < 64L * 1024)
				want = 64L * 1024;
			if (want > avail - 32L * 1024)
				want = (avail - 32L * 1024) & ~1L;
		}
	}
	if (want < 32L * 1024)
		return 0;
	/* owned by the accessory itself on plain TOS (libc's acc_core), not by
	   the process current now, which would hand it to the next program */
	p = acc_core(want, acc_heap_tt ? 1 : 3);
	if (acc_heap_tt && (long)p <= 0) {
		/* no luck in TT RAM after all: ST RAM, at its own share */
		long st = (long)Malloc(-1L) / 2;
		if (want > st)
			want = st & ~1L;
		acc_heap_tt = 0;
		p = 0;
	}
	if ((long)p == -32 || (long)p <= 0)
		p = acc_core(want, -1);
	if ((long)p <= 0)
		return 0;
	heap_add(p, (size_t)want);
	acc_heap_bytes = want;
	acc_heap_at = p;
	acc_heap_avail = avail;
	return want;
}

long acc_mem_kb(long need, long margin)
{
	long kb;
	if (heap_peak > need)
		need = heap_peak;
	if (margin < need / 10)
		margin = need / 10;
	kb = (need + margin) / 1024 + 16;
	return kb > 32000 ? 32000 : kb;
}

int acc_mem_moved(long kb, long memkb)
{
	return !memkb || kb > memkb || kb <= memkb - memkb / 8;
}

long acc_mem_full_kb(void)
{
	/* four times: a short heap costs the user a failed job each start, a
	   large one only until the next start measures it (MEMKB=) */
	long kb = acc_heap_bytes / 1024 * 4;
	return kb > 32000 ? 32000 : kb < 64 ? 64 : kb;
}

int acc_mem_line(char *buf, int size, long next_kb)
{
	char early[40] = "";
	if (heap_early)
		snprintf(early, sizeof(early), ", and %ld KB before it", heap_early / 1024);
	return snprintf(buf, (size_t)size, "memory: %ld KB in use, %ld KB at the most, of %ld KB%s%s; next start %ld KB",
			heap_use / 1024, heap_peak / 1024, acc_heap_bytes / 1024, acc_heap_tt ? " (TT RAM)" : "",
			early, next_kb);
}

/* under 2 MB of ST RAM (a 1 MB ST, a 520): the family's accessories don't
   run there (decided 26 Nov 2026). TOS loads every accessory at boot and
   it stays loaded, so on TOS 1.04 its image alone left 383 KB for the
   program in front (EContact 2.17), less than ClaudeST, EMail or EPDF
   needs to open its menu bar. The accessory can't unload itself: it takes
   no heap and says so from its Desk entry */
static long phystop_read(void)
{
	volatile long *p;
	/* the address hidden from GCC (as crash.c's low()): GCC 13 takes a
	   constant address for an object of size 0 and warns (-Warray-bounds) */
	__asm__("" : "=a"(p) : "0"(0x42EL));
	return *p;
}

int acc_small_machine(void)
{
	return Supexec(phystop_read) < 2L * 1024 * 1024;
}

void acc_small_alert(const char *name, const char *file)
{
	/* the AES's own alert, not the family's alert(): that saves the screen
	   behind it in memory malloc'd from a heap this accessory never took,
	   16 KB gone from the 1 MB machine from the first opening (EHypView's
	   small_acc.py, 26 Nov 2026). The words on the stack */
	char a[128];
	snprintf(a, sizeof a, "[1][%s needs|an Atari with 2 MB or more.|Remove %s|from the boot drive.][ OK ]",
		 name, file);
	form_alert(1, a);
}

/* an accessory that runs only with TT RAM (EWEB.ACC, decided 27 Nov 2026): 1 when there is none of
   it (Mxalloc mode 1 refused or empty). Where there is, TOS 3.06 and 4.04 load the accessory there
   when its PRGFLAGS asks (EWeb's probe: a 1.56 MB image in TT RAM, no ST RAM taken) */
int acc_no_ttram(void)
{
	long tt = (long)Mxalloc(-1L, 1);
	return tt == -32 || tt <= 0;
}

void acc_tt_alert(const char *name, const char *file)
{
	char a[128];
	snprintf(a, sizeof a, "[1][%s needs|an Atari with TT RAM.|Remove %s|from the boot drive.][ OK ]",
		 name, file);
	form_alert(1, a);
}
