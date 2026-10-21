/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_heap.c - the heap in atari/libc.c (malloc, free, calloc, realloc,
 * heap_add) on the build machine, with a GEMDOS of the test's own:
 *
 *   fixed        an accessory's heap: fill it, empty it, it is one block again
 *   grow         a program's heap: 512 KB from GEMDOS, then doubling
 *   limited      GEMDOS with little memory: smaller blocks, then "no memory"
 *   tos1         no Mxalloc (TOS 1.x): Malloc instead
 *   st1          a 1 MB ST: the first block a quarter of what is free
 *   maxblocks    never more than 40 blocks from GEMDOS
 *   exact        GEMDOS has just enough for the one request
 *   bins         the free lists' order by size
 *   dv1, dv2     the block before the one cut from: freed, grown into it
 *   random N S   N mixed calls (seed S), every block's bytes checked, the
 *                heap checked as it goes; no two blocks overlap
 *
 * Each run is one case: the heap's state is the process's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "tos.h"
#undef malloc
#undef free
#undef calloc
#undef realloc

#define WORD sizeof(size_t)

static int failed;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failed++;
	}
}

/* ---- GEMDOS ---- */

static char *pool, *pool_at, *pool_end;
static long cap = -1;		/* the most one call gets; -1 for any */
static int tos1;		/* no Mxalloc */
static long grants[64], ngrants, gemdos_calls;

/* GEMDOS's memory: low, as on an Atari (under 2 GB, so a block's address
   is positive as a long: the heap takes a negative one for an error) */
static void pool_new(long bytes)
{
	void *want = (void *)0x10000000UL;
	pool = mmap(want, (size_t)bytes + 64, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (pool == MAP_FAILED || (unsigned long)pool + bytes >= 0x80000000UL) {
		printf("FAIL no memory under 2 GB for GEMDOS\n");
		exit(1);
	}
	pool_at = pool + 2;	/* GEMDOS's blocks are even, not more */
	pool_end = pool + bytes;
}

static void *take(long n)
{
	char *p = pool_at;
	gemdos_calls++;
	if (n <= 0 || (cap >= 0 && n > cap) || !pool || p + n > pool_end)
		return 0;
	pool_at = (char *)(((unsigned long)p + n + 7) & ~7UL) + 2;
	if (ngrants < 64)
		grants[ngrants] = n;
	ngrants++;
	return p;
}

static long tt_free;		/* TT RAM free, for Mxalloc(-1, 1); 0: none */

static long st_left(void)
{
	return pool ? (long)(pool_end - pool_at) : 0;
}

void *Mxalloc(long n, int mode)
{
	if (tos1)
		return (void *)-32L;
	if (n == -1)
		return (void *)(mode == 1 ? tt_free : st_left());
	return take(n);
}

void *Malloc(long n)
{
	if (n == -1)
		return (void *)st_left();
	return take(n);
}

/* ---- the blocks' bytes ---- */

typedef struct {
	unsigned char *p;
	size_t n;
	unsigned id;
} LIVE;

static unsigned long rnd_state = 1;

static unsigned rnd(void)
{
	rnd_state = rnd_state * 1103515245UL + 12345UL;
	return (unsigned)(rnd_state >> 16) & 0x7FFF;
}

static unsigned rnd32(void)
{
	return rnd() << 15 | rnd();
}

static unsigned char pat(unsigned id, size_t i)
{
	return (unsigned char)(id * 131u + (unsigned)i * 7u + (unsigned)(i >> 8));
}

/* the bytes checked: all of a small block, the ends of a big one */
static int checked(size_t n, size_t i)
{
	return n <= 4096 || i < 2048 || i >= n - 2048;
}

static void fill(const LIVE *l, size_t from)
{
	size_t i;
	for (i = from; i < l->n; i++)
		if (checked(l->n, i))
			l->p[i] = pat(l->id, i);
		else
			i = l->n - 2048 - 1;
}

static int intact(const LIVE *l, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		if (!checked(l->n, i)) {
			i = l->n - 2048 - 1;
			continue;
		}
		if (l->p[i] != pat(l->id, i))
			return 0;
	}
	return 1;
}

static size_t usable(const void *p)
{
	return (((const size_t *)p)[-1] & ~(size_t)3) - WORD;
}

static int by_addr(const void *a, const void *b)
{
	const LIVE *x = a, *y = b;
	return x->p < y->p ? -1 : x->p > y->p;
}

/* every live block in use, big enough and apart from the others */
static int apart(LIVE *live, int n)
{
	LIVE *s = malloc(sizeof(LIVE) * (size_t)(n ? n : 1));
	int i, k = 0, ok = 1;
	for (i = 0; i < n; i++)
		if (live[i].p)
			s[k++] = live[i];
	qsort(s, (size_t)k, sizeof(LIVE), by_addr);
	for (i = 0; i < k && ok; i++) {
		size_t h = ((size_t *)s[i].p)[-1];
		if (!(h & 1) || usable(s[i].p) < s[i].n)
			ok = 0;
		else if (i + 1 < k && s[i].p + usable(s[i].p) + WORD > s[i + 1].p)
			ok = 0;
	}
	free(s);
	return ok;
}

static size_t some_size(void)
{
	unsigned r = rnd() % 1000;
	if (r < 600)
		return rnd() % 65;
	if (r < 850)
		return 65 + rnd() % 448;
	if (r < 970)
		return 513 + rnd() % 7680;
	if (r < 995)
		return 8192 + rnd32() % 57344;
	return 65536 + rnd32() % 344064;
}

/* ---- the cases ---- */

static void case_fixed(void)
{
	static char arena[300000 + 16];
	LIVE *live = malloc(sizeof(LIVE) * 20000);
	long f0, nfree, f;
	int n = 0, i;
	unsigned char *a, *b, *c, *x, *y, *z;

	heap_add(arena + 2, 300000);
	f0 = heap_check(&nfree);
	check(f0 > 300000 - 32 && nfree == 1, "fixed: one free block of nearly all of it");
	while (n < 20000) {
		LIVE l;
		l.n = rnd() % 2000;
		if (!(l.p = heap_malloc(l.n)))
			break;
		l.id = (unsigned)n;
		fill(&l, 0);
		live[n++] = l;
	}
	check(n > 200 && n < 20000, "fixed: full, then malloc says no");
	check(heap_check(0) >= 0, "fixed: full heap checks out");
	check(apart(live, n), "fixed: full heap's blocks apart");
	for (i = 0; i < n; i++)
		check(intact(&live[i], live[i].n), "fixed: bytes kept");
	for (i = n - 1; i > 0; i--) {		/* free in any order */
		int j = (int)(rnd32() % (unsigned)(i + 1));
		LIVE t = live[i];
		live[i] = live[j];
		live[j] = t;
	}
	for (i = 0; i < n; i++) {
		heap_free(live[i].p);
		if (i % 97 == 0)
			check(heap_check(0) >= 0, "fixed: checks out while emptying");
	}
	f = heap_check(&nfree);
	check(f == f0 && nfree == 1, "fixed: empty again, one block again");
	a = heap_malloc((size_t)f0 - WORD);
	check(a != 0, "fixed: the whole of it in one malloc");
	check(heap_malloc(1) == 0, "fixed: then nothing more");
	heap_free(a);
	check(heap_malloc((size_t)f0 - WORD + 1) == 0, "fixed: a byte more than all: no");
	check(gemdos_calls == 0, "fixed: never asks GEMDOS");

	/* realloc grows into the free block after it, and keeps the bytes */
	a = heap_malloc(100);
	b = heap_malloc(100);
	c = heap_malloc(100);
	if (b < a) {			/* a: the lower of two side by side */
		x = a;
		a = b;
		b = x;
	}
	check(b == a + usable(a) + WORD, "malloc: one block after another");
	memset(a, 'a', 100);
	heap_free(b);
	check(heap_realloc(a, 150) == a, "realloc: grows in place");
	check(a[0] == 'a' && a[99] == 'a', "realloc: in place keeps the bytes");
	check(heap_realloc(a, 120) == a, "realloc: smaller stays");
	x = heap_realloc(a, 5000);
	check(x && x != a && x[0] == 'a' && x[99] == 'a', "realloc: moves when it must, bytes kept");
	check(heap_realloc(x, (size_t)1 << 31) == 0 && x[0] == 'a', "realloc: too big: no, block kept");
	{
		long before = heap_use;
		check(heap_realloc(x, 101) == x && x[0] == 'a' && x[99] == 'a' && usable(x) < 200,
		      "realloc: much smaller: the end given back in place, bytes kept");
		check(before - heap_use > 4800 && heap_check(0) >= 0, "  heap_use drops, the heap checks out");
		y = heap_malloc(4000);
		check(y && heap_check(0) >= 0, "  the end is used again");
		heap_free(y);
	}
	y = heap_calloc(100, 3);
	for (i = 0; i < 300 && y && !y[i]; i++)
		;
	check(y && i == 300, "calloc: zeroed");
	check(heap_calloc(0x10000, 0x10000) == 0, "calloc: overflow: no");
	check(heap_calloc((size_t)-1, 2) == 0, "calloc: overflow: no (2)");
	check(heap_malloc(((size_t)1 << 30) + 1) == 0, "malloc: over a gigabyte: no");
	z = heap_malloc(0);
	check(z != 0, "malloc(0): a block");
	heap_free(0);
	/* a block freed twice between blocks in use: left alone */
	a = heap_malloc(40);
	b = heap_malloc(40);
	z = heap_malloc(40);
	heap_free(b);
	heap_free(b);
	check(heap_check(0) >= 0, "free twice: heap still checks out");
	heap_free(a);
	heap_free(z);
	heap_free(c);
	heap_free(x);
	heap_free(y);
	check(heap_check(&nfree) >= 0, "fixed: checks out at the end");
	free(live);
}

static void case_grow(void)
{
	long sum = 0, nfree;
	int i, n = 0;
	void **v = malloc(sizeof(void *) * 40000);
	pool_new(128L << 20);
	for (i = 0; i < 40000; i++) {
		if (!(v[i] = heap_malloc(1000)))
			break;
		n++;
	}
	check(n == 40000, "grow: 40 MB in 1000-byte blocks");
	check(ngrants >= 1 && grants[0] == 512L * 1024, "grow: first 512 KB");
	for (i = 1; i < ngrants && i < 64; i++) {
		long before = 0;
		int k;
		for (k = 0; k < i; k++)
			before += grants[k];
		check(grants[i] == before, "grow: then as much as all before");
	}
	check(ngrants <= 8, "grow: few GEMDOS blocks");
	for (i = 0; i < ngrants; i++)
		sum += grants[i];
	check(heap_tt + heap_st == sum, "grow: heap_tt + heap_st add up");
	check(heap_check(0) >= 0, "grow: checks out");
	for (i = 0; i < n; i++)
		heap_free(v[i]);
	check(heap_check(&nfree) >= 0 && nfree == ngrants, "grow: emptied: one free block an area");
	free(v);
}

short _is_acc;

/* an accessory's malloc before its heap_add: 16 KB steps, not 512 KB */
static void case_accearly(void)
{
	static char arena[100000 + 16];
	void *p, *q;
	pool_new(4L << 20);
	_is_acc = 1;
	p = heap_malloc(8000);
	check(p != 0 && ngrants == 1 && grants[0] == 16384, "accearly: 8000 bytes take one 16 KB block");
	check(heap_early == 16384 && heap_tt + heap_st == 16384, "accearly: counted in heap_early and the heap's total");
	q = heap_malloc(20000);
	check(q != 0 && ngrants == 2 && grants[1] == 32768, "accearly: 20000 more take 32 KB, not 512");
	heap_add(arena + 2, 100000);
	q = heap_malloc(50000);
	check(q != 0 && ngrants == 2, "accearly: after heap_add, from the heap, no more GEMDOS");
	check(heap_early == 49152, "accearly: heap_early stays as it was");
	check(heap_check(0) >= 0, "accearly: checks out");
	_is_acc = 0;
}

/* ECal's refresh in Falcon mode (20 Oct 2026): an answer's buffer grows
   by doubling and goes, while the events read from it stay, small; over
   many answers the small ones must not end up all through the heap */
static void case_frag(void)
{
	static char arena[4L * 1024 * 1024 + 16];
	unsigned r;
	int k, failed_big = 0;
	long worst = 1L << 30;
	heap_add(arena + 2, 4L * 1024 * 1024);
	for (r = 0; r < 1500; r++) {
		size_t n = 8192, top = 16384u << (r % 4);
		char *answer = heap_malloc(12000 + r % 5000);	/* the gzip'd answer */
		char *big = heap_malloc(n), *nb;
		for (; big && n < top; n *= 2) {
			/* inflate's output doubles; events are parsed meanwhile */
			for (k = 0; k < 3; k++)
				if (!heap_malloc(40 + (r * 37 + k * 101 + n) % 260))
					failed_big++;
			nb = heap_realloc(big, n * 2);
			if (!nb)
				break;
			big = nb;
		}
		if (!answer || !big || n < top)
			failed_big++;
		heap_free(answer);
		heap_free(big);
		if (r % 50 == 49) {
			heap_check(0);
			if (heap_check_big < worst)
				worst = heap_check_big;
		}
	}
	check(failed_big == 0, "frag: every answer's buffer grew to its size, every event kept");
	check(heap_check(0) >= 0, "frag: checks out");
	check(worst >= 1024L * 1024, "frag: the largest free block never under 1 MB");
	printf("frag: %ld KB free, largest %ld KB, worst %ld KB\n", heap_check(0) / 1024, heap_check_big / 1024, worst / 1024);
}

/* a trace from an Atari, replayed (fixtures/: ECal's accessory, Falcon
   mode, three calendars of 500 events; whomper/ECal's conv.py made it
   from a probe build's console). Lines: "A size" heap_add, "M id n"
   malloc (id 0: it failed there), "F id" free, "R oldid newid n"
   realloc (newid 0: failed there), "X" where the app failed there. Every
   call that worked there must work here, and at each X the largest free
   block must be at least 128 KB (it was 39 KB, 20 Oct 2026) */
static void case_replay(const char *path)
{
	FILE *f = fopen(path, "r");
	unsigned char **ptr = calloc(400000, sizeof(*ptr));
	char line[100];
	long nops = 0, fails = 0, xs = 0, worst = 1L << 30, nfree, fr;
	if (!f || !ptr) {
		check(0, "replay: the trace");
		return;
	}
	pool_new(64L << 20);
	while (fgets(line, sizeof(line), f)) {
		long a, b, n;
		unsigned char *p;
		nops++;
		if (line[0] == 'A' && sscanf(line + 2, "%ld", &n) == 1) {
			heap_add(pool + 2, (size_t)n);
		} else if (line[0] == 'M' && sscanf(line + 2, "%ld %ld", &a, &n) == 3 - 1) {
			p = heap_malloc((size_t)n);
			if (!p && a)
				fails++;
			if (a && a < 400000)
				ptr[a] = p;
			else if (p)
				heap_free(p);
		} else if (line[0] == 'F' && sscanf(line + 2, "%ld", &a) == 1 && a < 400000) {
			heap_free(ptr[a]);
			ptr[a] = 0;
		} else if (line[0] == 'R' && sscanf(line + 2, "%ld %ld %ld", &a, &b, &n) == 3 &&
			   a < 400000 && b < 400000) {
			p = heap_realloc(a ? ptr[a] : 0, (size_t)n);
			if (!p && b)
				fails++;
			if (p) {
				if (a)
					ptr[a] = 0;
				if (b)
					ptr[b] = p;
				else
					heap_free(p);
			}
		} else if (line[0] == 'X') {
			fr = heap_check(&nfree);
			xs++;
			if (heap_check_big < worst)
				worst = heap_check_big;
			printf("replay: at X (op %ld): free %ld in %ld, largest %ld, use %ld\n", nops, fr, nfree,
			       heap_check_big, heap_use);
		}
	}
	fclose(f);
	check(fails == 0, "replay: every call that worked on the Atari works");
	check(xs > 0 && worst >= 128L * 1024, "replay: at the Atari's failure points, a free block of 128 KB or more");
	check(heap_check(0) >= 0, "replay: checks out");
	printf("replay: %ld calls, peak %ld\n", nops, heap_peak);
	free(ptr);
}

static void case_limited(void)
{
	int n = 0, i;
	void **v = malloc(sizeof(void *) * 10000);
	pool_new(3L << 20);
	cap = 1L << 20;
	while (n < 10000 && (v[n] = heap_malloc(1000)))
		n++;
	check(n > 2000 && n < 3200, "limited: most of 3 MB, then no");
	for (i = 0; i < ngrants && i < 64; i++)
		check(grants[i] <= cap, "limited: no block over what GEMDOS gives");
	check(heap_check(0) >= 0, "limited: checks out");
	for (i = 0; i < n; i += 2)
		heap_free(v[i]);
	check(heap_malloc(500) != 0, "limited: memory back after free");
	check(heap_check(0) >= 0, "limited: checks out at the end");
	free(v);
}

static void case_tos1(void)
{
	void *p;
	pool_new(4L << 20);
	tos1 = 1;
	p = heap_malloc(100);
	check(p != 0 && ngrants == 1, "tos1: Malloc instead of Mxalloc");
	heap_free(p);
	check(heap_check(0) >= 0, "tos1: checks out");
}

/* a 1 MB ST: no TT RAM, 700 KB free: the first block a quarter of it,
   so a job's buffer still finds most of the rest; then it grows */
static void case_st1(void)
{
	int n = 0;
	pool_new(700L * 1024);
	check(heap_malloc(1000) != 0 && ngrants == 1 && grants[0] <= 700L * 1024 / 4,
	      "st1: the first block a quarter of what is free");
	check(st_left() > 450L * 1024, "st1: most of the memory left for the rest");
	while (n < 400 && heap_malloc(1000))	/* 400 KB: more than the first block */
		n++;
	check(n == 400 && ngrants >= 2, "st1: and the heap grows when it needs to");
	check(heap_check(0) >= 0, "st1: checks out");
}

static void case_maxblocks(void)
{
	long sum = 0;
	int n = 0, i;
	pool_new(64L << 20);
	cap = 64L * 1024;
	while (n < 100000 && heap_malloc(1000))
		n++;
	check(ngrants == 40, "maxblocks: 40 GEMDOS blocks, then no more");
	for (i = 0; i < ngrants; i++)
		sum += grants[i];
	/* (each 64 KB or less: halved down from as much as all before) */
	check((long)n * 1000 > sum / 10 * 9 && (long)n * 1000 < sum,
	      "maxblocks: and the blocks are used");
	check(heap_check(0) >= 0, "maxblocks: checks out");
}

/* the lists by size: in order (so a block from a later list is always
   big enough), one list per size below 512 */
static void case_bins(void)
{
	size_t sz;
	unsigned prev = 0, b;
	int k, ok = 1;
	for (sz = 4 * WORD; sz < ((size_t)8 << 20) && ok; sz += WORD) {
		b = heap_bin(sz);
		if (b < prev || (sz < 512 && b != sz / WORD))
			ok = 0;
		prev = b;
	}
	check(ok, "bins: in order up to 8 MB, one per size below 512");
	for (k = 23; k < 32 && ok; k++) {
		size_t p2 = (size_t)1 << k;
		if (heap_bin(p2 - WORD) > heap_bin(p2) || heap_bin(p2) > heap_bin(p2 + p2 / 2))
			ok = 0;
	}
	check(ok, "bins: in order up to 2 GB");
	check(heap_bin((size_t)1 << 31) >= heap_bin(((size_t)1 << 31) - WORD) &&
	      heap_bin((size_t)-1 & ~(size_t)3) == heap_bin((size_t)1 << 31), "bins: 2 GB and up in the last");
}

/* a block just before the one cut from, freed (1) or grown into all of it
   (2): three blocks, everything else taken, the middle one freed and cut
   from */
static void case_dv(int which)
{
	static char arena[4096 + 16];
	unsigned char *p1, *p2, *p3, *q, *r, *all;
	long f, nfree;
	heap_add(arena, 4096);
	p1 = heap_malloc(200);
	p2 = heap_malloc(200);
	p3 = heap_malloc(200);
	check(p3 < p2 && p2 < p1, "dv: cut from the end, one below the other");
	f = heap_check(&nfree);
	all = heap_malloc((size_t)f - WORD);
	check(all && heap_check(0) == 0, "dv: all the rest taken");
	heap_free(p2);
	q = heap_malloc(20);
	check(q > p2 && q < p1, "dv: the freed block cut from");
	if (which == 1) {
		heap_free(p3);
		check(heap_check(&nfree) >= 0 && nfree == 1, "dv: the block before it freed: joined");
	} else {
		size_t room = (size_t)(q - p3) - WORD;	/* up to q's block */
		r = heap_realloc(p3, room);
		check(r == p3 && heap_check(&nfree) == 0 && nfree == 0, "dv: grown into all of it");
		r = heap_malloc(8);
		check(r == 0, "dv: nothing left after that");
	}
}

/* GEMDOS has just enough: one block, and it holds the request whatever
   its address (even, never more) */
static void case_exact(void)
{
	void *p;
	pool_new(4L << 20);
	cap = 200000 + 64;
	p = heap_malloc(200000);
	check(p && ngrants == 1, "exact: just what is needed, in one GEMDOS block");
	check(heap_check(0) >= 0, "exact: checks out");
}

static void case_random(long ops, unsigned long seed)
{
	enum { SLOTS = 12000 };
	LIVE *live = calloc(SLOTS, sizeof(LIVE));
	long i, nfree;
	unsigned id = 1;
	int k, bad = 0;
	rnd_state = seed;
	pool_new(160L << 20);
	for (i = 0; i < ops && !bad; i++) {
		unsigned r = rnd() % 100;
		LIVE *l = &live[rnd32() % SLOTS];
		if (r < 55) {				/* malloc, or calloc */
			if (l->p)
				continue;
			l->n = some_size();
			l->id = id++;
			if (r < 5) {
				size_t j;
				l->p = heap_calloc(l->n, 1);
				for (j = 0; l->p && j < l->n; j++)
					if (l->p[j]) {
						bad = 1;
						printf("FAIL random: calloc not zeroed\n");
						break;
					}
			} else
				l->p = heap_malloc(l->n);
			if (!l->p) {
				printf("FAIL random: no memory at call %ld\n", i);
				bad = 1;
				break;
			}
			fill(l, 0);
		} else if (r < 90) {			/* free */
			if (!l->p)
				continue;
			if (!intact(l, l->n)) {
				printf("FAIL random: bytes changed (call %ld)\n", i);
				bad = 1;
			}
			heap_free(l->p);
			l->p = 0;
		} else {				/* realloc */
			size_t n = some_size();
			unsigned char *p;
			if (!l->p)
				continue;
			if (!intact(l, l->n)) {
				printf("FAIL random: bytes changed before realloc (call %ld)\n", i);
				bad = 1;
			}
			if (!(p = heap_realloc(l->p, n))) {
				printf("FAIL random: realloc: no memory at call %ld\n", i);
				bad = 1;
				break;
			}
			l->p = p;
			if (n < l->n) {
				if (!intact(l, n)) {
					printf("FAIL random: realloc lost bytes (call %ld)\n", i);
					bad = 1;
				}
				l->n = n;
			} else {
				size_t old = l->n;
				LIVE t = *l;
				t.n = old;
				if (!intact(&t, old)) {
					printf("FAIL random: realloc lost bytes (call %ld)\n", i);
					bad = 1;
				}
				l->n = n;
			}
			l->id = id++;
			fill(l, 0);
		}
		if (i % 25000 == 0 && (heap_check(0) < 0 || !apart(live, SLOTS))) {
			printf("FAIL random: heap wrong at call %ld\n", i);
			bad = 1;
		}
	}
	if (bad)
		failed++;
	check(heap_check(0) >= 0 && apart(live, SLOTS), "random: checks out at the end");
	{
		/* heap_use is the blocks in use, whole: their heads say their sizes */
		long sum = 0;
		for (k = 0; k < SLOTS; k++)
			if (live[k].p)
				sum += (long)(((size_t *)live[k].p)[-1] & ~(size_t)3);
		check(heap_use == sum && heap_peak >= heap_use, "random: heap_use is the blocks in use");
	}
	for (k = 0; k < SLOTS; k++)
		if (live[k].p) {
			check(intact(&live[k], live[k].n), "random: bytes kept to the end");
			heap_free(live[k].p);
		}
	check(heap_check(&nfree) >= 0 && nfree == ngrants, "random: emptied: one free block an area");
	check(heap_use == 0 && heap_peak > 0, "random: emptied: heap_use 0, heap_peak kept");
	printf("random: %ld calls, %ld GEMDOS blocks, %ld KB taken\n", ops, ngrants,
	       (heap_tt + heap_st) / 1024);
	free(live);
}

int main(int argc, char **argv)
{
	const char *c = argc > 1 ? argv[1] : "";
	if (!strcmp(c, "fixed"))
		case_fixed();
	else if (!strcmp(c, "grow"))
		case_grow();
	else if (!strcmp(c, "replay") && argc > 2)
		case_replay(argv[2]);
	else if (!strcmp(c, "frag"))
		case_frag();
	else if (!strcmp(c, "accearly"))
		case_accearly();
	else if (!strcmp(c, "limited"))
		case_limited();
	else if (!strcmp(c, "tos1"))
		case_tos1();
	else if (!strcmp(c, "st1"))
		case_st1();
	else if (!strcmp(c, "maxblocks"))
		case_maxblocks();
	else if (!strcmp(c, "exact"))
		case_exact();
	else if (!strcmp(c, "bins"))
		case_bins();
	else if (!strcmp(c, "dv1"))
		case_dv(1);
	else if (!strcmp(c, "dv2"))
		case_dv(2);
	else if (!strcmp(c, "random") && argc > 3)
		case_random(atol(argv[2]), strtoul(argv[3], 0, 10));
	else {
		fprintf(stderr, "test_heap fixed|grow|limited|tos1|maxblocks|exact|bins|dv1|dv2|random N SEED\n");
		return 2;
	}
	printf("%s %s\n", failed ? "FAIL" : "ok  ", c);
	return failed != 0;
}
