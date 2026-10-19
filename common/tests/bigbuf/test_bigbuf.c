/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_bigbuf.c - atari/bigbuf.c on the build machine, with a GEMDOS of
 * the test's own: TT RAM first, ST RAM without it, Malloc on TOS 1.x, the
 * spares left, exactly what a small job needs, nothing when too little.
 */
#include <stdio.h>
#include "tos.h"
#include "bigbuf.h"

static long tt_free, st_free, last_n;
static int tos1, last_mode, freed;
static char mem[16];		/* the address handed out: never written */

void *Mxalloc(long n, int mode)
{
	if (tos1)
		return (void *)-32L;
	if (n == -1)
		return (void *)(mode == 1 ? tt_free : st_free);
	if (n > (mode == 1 ? tt_free : st_free))
		return 0;
	last_n = n;
	last_mode = mode;
	return mem;
}

void *Malloc(long n)
{
	if (n == -1)
		return (void *)st_free;
	if (n > st_free)
		return 0;
	last_n = n;
	last_mode = 9;
	return mem;
}

long Mfree(void *p)
{
	freed += p == mem;
	return 0;
}

static long frb;

long get_cookie(long id)
{
	return id == 0x5F465242L ? frb : 0;
}

static int failed;

static void check(const char *what, long tt, long st, int old, long want,
		  long got_want, int tt_want, int mode_want)
{
	long got;
	int t;
	void *p;
	tt_free = tt;
	st_free = st;
	tos1 = old;
	last_n = 0;
	last_mode = -1;
	p = big_alloc(want, &got, &t);
	if (got != got_want || t != tt_want || (p != 0) != (got_want > 0) ||
	    (p && (last_mode != mode_want || last_n != got))) {
		printf("FAIL %s: got %ld tt %d mode %d (want %ld tt %d mode %d)\n", what, got, t,
		       last_mode, got_want, tt_want, mode_want);
		failed++;
	}
	big_free(p);
}

int main(void)
{
	const long K = 1024;
	/* a Falcon with 128 MB Fast RAM: TT RAM, all the job asks for */
	check("fast ram, 1 MB job", 128 * K * K, 3 * K * K, 0, K * K, K * K, 1, 1);
	check("fast ram, size unknown", 1000 * K, 3 * K * K, 0, -1, 1000 * K - 64 * K, 1, 1);
	/* a Falcon without: ST RAM, 32 KB left */
	check("st only, size unknown", 0, 2 * K * K, 0, -1, 2 * K * K - 32 * K, 0, 0);
	check("st only, 100 KB job", 0, 2 * K * K, 0, 100 * K, 100 * K, 0, 0);
	/* too little TT RAM: ST RAM */
	check("tt ram nearly full", 70 * K, 2 * K * K, 0, 200 * K, 200 * K, 0, 0);
	/* a small job fits in what TT RAM is left over its spare */
	check("small job in tt ram", 70 * K, 2 * K * K, 0, 4 * K, 4 * K, 1, 1);
	/* a 1 MB ST under TOS 1.04: Malloc, the spare left */
	check("tos 1.x", 0, 100 * K, 1, -1, 68 * K, 0, 9);
	check("tos 1.x, 1 MB job", 0, 100 * K, 1, K * K, 68 * K, 0, 9);
	/* too little for a buffer: nothing (the job's own small buffer) */
	check("too little", 0, 36 * K, 0, -1, 0, 0, 0);
	check("too little, tos 1.x", 0, 36 * K, 1, 100 * K, 0, 0, 0);
	/* odd sizes go up to even */
	check("odd size", 0, 2 * K * K, 0, 9001, 9002, 0, 0);
	frb = 0;
	if (big_direct())
		printf("FAIL big_direct without _FRB\n"), failed++;
	frb = 0x123456;
	if (!big_direct())
		printf("FAIL big_direct with _FRB\n"), failed++;
	if (freed != 9)
		printf("FAIL big_free: %d freed, want 9\n", freed), failed++;
	if (!failed)
		printf("bigbuf: all ok\n");
	return failed != 0;
}
