/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * An accessory's memory (ui/accmem.c) on the build machine, with GEMDOS
 * standing in: ST RAM and TT RAM free, what each call is given.
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "accmem.h"

#define KB 1024L
static long st_free, tt_free, given, given_mode, added;
static int failed, checks;
static char area[1];
long heap_use, heap_peak, heap_early;

void *Malloc(long n)
{
	if (n == -1)
		return (void *)st_free;
	if (n > st_free)
		return 0;
	st_free -= n;
	given = n;
	given_mode = 0;
	return area;
}

void *Mxalloc(long n, int mode)
{
	long *f = mode == 1 ? &tt_free : &st_free;
	if (n == -1)
		return (void *)*f;
	if (n > *f)
		return 0;
	*f -= n;
	given = n;
	given_mode = mode;
	return area;
}

static int n_accs = 1, found;		/* the *.ACC in the boot root */
static char path_asked[16];

void *Fgetdta(void)
{
	return area;
}
long Fsetdta(void *p)
{
	(void)p;
	return 0;
}
long Fsfirst(const char *path, int attr)
{
	(void)attr;
	strncpy(path_asked, path, sizeof(path_asked) - 1);
	found = 0;
	return found < n_accs ? (found++, 0) : -33;
}
long Fsnext(void)
{
	return found < n_accs ? (found++, 0) : -49;
}
short boot_drive(void)
{
	return 2;
}

void heap_add(void *p, size_t bytes)
{
	(void)p;
	added = (long)bytes;
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static long take(long st, long tt, long first, long tt_first, long heapkb, long memkb)
{
	st_free = st;
	tt_free = tt;
	given = added = acc_heap_bytes = 0;
	return acc_heap_take(first, tt_first, heapkb, memkb);
}

int main(void)
{
	char line[160];
	long a, b;
	int i;

	/* the 4 MB Falcon with NVDI: 3660 KB free; ECal, then EContact */
	a = take(3660 * KB, 0, 1536 * KB, 4096 * KB, 0, 0);
	check(a == 915 * KB && added == a && !acc_heap_tt, "first start, 3.6 MB free: a quarter (915 KB), not 1.5 MB");
	b = take(st_free, 0, 2048 * KB, 0, 0, 0);
	check(b > 0 && st_free >= 2048 * KB, "  the second accessory a quarter of the rest: over 2 MB left for EMail");
	printf("     ECal %ld KB, EContact %ld KB, %ld KB left\n", a / KB, b / KB, st_free / KB);

	a = take(3660 * KB, 0, 1536 * KB, 0, 0, 450);
	check(a == 450 * KB, "measured (MEMKB=450): 450 KB");
	a = take(3660 * KB, 0, 1536 * KB, 0, 0, 3000);
	check(a == 1830 * KB, "  but never more than half");
	a = take(3660 * KB, 0, 1536 * KB, 0, 1200, 450);
	check(a == 1200 * KB, "HEAPKB= wins over MEMKB=");

	/* a 4 MB Falcon with NVDI's fonts: 1552 KB free when the accessories
	   load; EContact, then ECal (EMail had 0 KB beside its program) */
	a = take(1552 * KB, 0, 2048 * KB, 0, 0, 0);
	check(a == 388 * KB, "4 MB Falcon with NVDI, 1552 KB free: a quarter, 388 KB");
	b = take(st_free, 0, 1536 * KB, 0, 0, 0);
	check(b == 291 * KB, "  the second a quarter of the rest, 291 KB: 873 KB left for the program");

	/* the same, all four accessories in C:\ (ECal, EContact, EHypView,
	   EMail check): each first boot a share of half of what is free,
	   an eighth, and half and more left for the program (EUpdate found
	   0 KB with four quarters, 27 Oct 2026) */
	n_accs = 4;
	st_free = 1552 * KB;
	for (i = 0, b = 0; i < 4; i++) {
		a = take(st_free, 0, 2048 * KB, 0, 0, 0);
		if (i == 0)
			check(a == 194 * KB && !strcmp(path_asked, "C:\\*.ACC"), "four accessories in C:\\: an eighth, 194 KB");
		b += a;
	}
	check(st_free > 900 * KB, "  all four: over 900 KB left for the program");
	a = take(1552 * KB, 0, 2048 * KB, 0, 300, 0);
	check(a == 300 * KB, "  measured (MEMKB=300): 300 KB, however many there are");
	n_accs = 2;
	st_free = 1552 * KB;
	a = take(1552 * KB, 0, 2048 * KB, 0, 0, 0);
	check(a == 388 * KB, "  two accessories: a quarter, as before");
	n_accs = 1;

	/* a 1 MB ST with STinG: 400 KB free; one accessory */
	a = take(400 * KB, 0, 2048 * KB, 0, 0, 0);
	check(a == 200 * KB, "1 MB ST, 400 KB free: half (under 2 MB free, as before)");
	a = take(150 * KB, 0, 256 * KB, 0, 0, 0);
	check(a == 118 * KB, "150 KB free: all but 32 KB (half is under 96 KB)");
	acc_heap_leave = 96 * KB;
	a = take(160 * KB, 0, 1024 * KB, 0, 0, 0);
	check(a == 64 * KB && st_free == 96 * KB, "160 KB free, 96 KB to leave: 64 KB, 96 KB for a program beside it");
	a = take(130 * KB, 0, 1024 * KB, 0, 0, 0);
	check(a == 64 * KB, "  130 KB free: still 64 KB, the least a heap is");
	a = take(90 * KB, 0, 1024 * KB, 0, 0, 0);
	check(a == 58 * KB, "  90 KB free: all but 32 KB");
	a = take(300 * KB, 0, 1024 * KB, 0, 0, 0);
	check(a == 150 * KB, "300 KB free, 96 KB to leave: half, as ever");
	acc_heap_leave = 32 * KB;
	a = take(30 * KB, 0, 256 * KB, 0, 0, 0);
	check(a == 0 && !added, "30 KB free: nothing taken");

	/* TT RAM: the heap goes there */
	a = take(3000 * KB, 16384 * KB, 1536 * KB, 4096 * KB, 0, 0);
	check(a == 4096 * KB && acc_heap_tt && given_mode == 1, "TT RAM, 16 MB: tt_first (4 MB) there");
	check(st_free == 3000 * KB, "  ST RAM untouched");
	a = take(3000 * KB, 16384 * KB, 1536 * KB, 0, 0, 300);
	check(a == 300 * KB && acc_heap_tt, "  MEMKB= in TT RAM too, when the app passes it");

	/* what to write */
	heap_peak = 196 * KB;
	check(acc_mem_kb(196 * KB, 256 * KB) == 468, "peak 196 KB + 256 KB margin: 468 KB");
	check(acc_mem_kb(100 * KB, 0) == 16 + (196 * KB + 196 * KB / 10) / KB, "  need under the peak: the peak, and a tenth");
	check(acc_mem_moved(468, 0), "no MEMKB= yet: written");
	check(!acc_mem_moved(460, 468), "468 to 460: not written (under an eighth)");
	check(acc_mem_moved(400, 468), "468 to 400: written");
	check(acc_mem_moved(500, 468), "more than before: written");
	acc_heap_bytes = 256 * KB;
	check(acc_mem_full_kb() == 1024, "memory full with 256 KB: 1024 KB next start (ECal's ST, 3 x 500: 850 KB needed)");

	heap_use = 52 * KB;
	heap_peak = 95 * KB;
	heap_early = 16 * KB;
	acc_heap_tt = 0;
	acc_mem_line(line, sizeof(line), 351);
	check(!strcmp(line, "memory: 52 KB in use, 95 KB at the most, of 256 KB, and 16 KB before it; next start 351 KB"),
	      "the log line");
	printf("     %s\n", line);

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
