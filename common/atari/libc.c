/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * libc.c - the subset of the C library the AERA programs use on the Atari:
 * strings, a heap on top of GEMDOS Malloc/Mxalloc, number parsing,
 * qsort and a small vsnprintf. On the host build the system libc is
 * used instead, so the mail core is the same code on both.
 */
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "tos.h"

#ifndef HEAP_TEST	/* tests/heap builds only the heap, on the build machine */

/* ---------------- memory and strings ---------------- */

int mem_unaligned_ok;		/* a 68020 or later: longs at any address */

void *memcpy(void *d, const void *s, size_t n)
{
	u8 *dp = d;
	const u8 *sp = s;
	/* long copies when both are even, or always on a 68020 and later
	   (the 68000 faults on an odd address; text is usually odd on one side) */
	if (mem_unaligned_ok || !(((u32)dp | (u32)sp) & 1)) {
		while (n >= 4) {
			*(u32 *)dp = *(const u32 *)sp;
			dp += 4;
			sp += 4;
			n -= 4;
		}
	}
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	u8 *dp = d;
	const u8 *sp = s;
	if (dp <= sp || dp >= sp + n)
		return memcpy(d, s, n);
	dp += n;
	sp += n;
	while (n--)
		*--dp = *--sp;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	u8 *dp = d;
	/* long words from an even address: a 68000 can */
	if ((u32)dp & 1 && n) {
		*dp++ = (u8)c;
		n--;
	}
	if (n >= 8) {
		u32 v = (u8)c;
		v |= v << 8;
		v |= v << 16;
		while (n >= 4) {
			*(u32 *)dp = v;
			dp += 4;
			n -= 4;
		}
	}
	while (n--)
		*dp++ = (u8)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const u8 *ap = a, *bp = b;
	for (; n; n--, ap++, bp++)
		if (*ap != *bp)
			return *ap - *bp;
	return 0;
}

void *memchr(const void *s, int c, size_t n)
{
	const u8 *p = s;
	for (; n; n--, p++)
		if (*p == (u8)c)
			return (void *)p;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
	char *r = d;
	while (n && *s) {
		*d++ = *s++;
		n--;
	}
	while (n--)
		*d++ = 0;
	return r;
}

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strncat(char *d, const char *s, size_t n)
{
	char *p = d + strlen(d);
	while (n-- && *s)
		*p++ = *s++;
	*p = 0;
	return d;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (u8)*a - (u8)*b;
		if (!*a)
			return 0;
	}
	return 0;
}

int strcasecmp(const char *a, const char *b)
{
	int ca, cb;
	do {
		ca = tolower((u8)*a++);
		cb = tolower((u8)*b++);
	} while (ca && ca == cb);
	return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	int ca, cb;
	for (; n; n--) {
		ca = tolower((u8)*a++);
		cb = tolower((u8)*b++);
		if (ca != cb)
			return ca - cb;
		if (!ca)
			return 0;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return 0;
	}
}

char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++) {
		if (*s == (char)c)
			r = s;
		if (!*s)
			return (char *)r;
	}
}

char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	if (!l)
		return (char *)h;
	for (; *h; h++)
		if (*h == *n && !strncmp(h, n, l))
			return (char *)h;
	return 0;
}

size_t strspn(const char *s, const char *a)
{
	const char *p = s;
	while (*p && strchr(a, *p))
		p++;
	return p - s;
}

size_t strcspn(const char *s, const char *r)
{
	const char *p = s;
	while (*p && !strchr(r, *p))
		p++;
	return p - s;
}

char *strpbrk(const char *s, const char *a)
{
	for (; *s; s++)
		if (strchr(a, *s))
			return (char *)s;
	return 0;
}

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

#endif

/* TLS.OVL (common/tlsovl) takes the strings and vsnprintf from here, and
   the memory from the program that loads it: no heap of its own */
#ifndef LIBC_NO_HEAP

/* ---------------- heap ----------------
 * Blocks carry their size at both ends (boundary tags), so free() finds
 * and joins its neighbours at once, and free blocks are kept in lists by
 * size, so malloc() looks in one or two lists, never the whole heap. (The
 * K&R list before it walked the free blocks on every free(): 14% of
 * ECal's calendar reading.) A block is cut from the end of a free one,
 * which stays where it is; and one free block, dv, is kept out of the
 * lists to cut from, so a run of mallocs and frees costs a few moves
 * each. On an 8 MHz ST in Hatari (tests/heap) a calendar's worth of
 * blocks took a fifth of K&R's time, and a fixed heap (an accessory's)
 * lasted many times as long before it was too cut up to use.
 *
 * A block starts with a word: its size in bytes, a multiple of ALIGN; bit
 * 0 is set while it is in use, bit 1 while the block before it is. A free
 * block also has its size in its last word and two list links after the
 * first, so an allocated block costs one word (K&R's cost two). Each area
 * of memory ends in a word marked in use, the fence, so nothing is ever
 * joined across the end of an area.
 *
 * The memory comes from GEMDOS: Mxalloc mode 3 prefers TT/Fast RAM on a
 * TT or accelerated Falcon; plain TOS 1.x lacks Mxalloc and gets Malloc.
 */

typedef struct blk {
	size_t head;			/* size, INUSE, PINUSE */
	struct blk *next, *prev;	/* a free block's list links */
} BLK;

#define INUSE	1UL
#define PINUSE	2UL
#define FLAGS	3UL
#define WORD	sizeof(size_t)
#define ALIGN	WORD				/* 4 on the Atari */
#define MINBLK	(2 * WORD + 2 * sizeof(BLK *))	/* head, links, foot */
#define MAXREQ	((size_t)1 << 30)
#define SIZE(b)	((b)->head & ~FLAGS)
#define AT(b, n) ((BLK *)((char *)(b) + (n)))
#define FOOT(b, s) (*(size_t *)((char *)(b) + (s) - WORD))

/* one list per size below SMALL; above it four per power of two, each
   list holding blocks within a quarter of each other */
#define SMALL	512
#define BIG	4096		/* and up: cut from a free block of its own, not dv */
#define SMALL_LG 9
#define NSMALL	(SMALL / ALIGN)
#define NBINS	(NSMALL + (30 - SMALL_LG + 1) * 4)
#define NMAP	((NBINS + 31) / 32)

static BLK *bins[NBINS];
static u32 binmap[NMAP];		/* which lists have blocks */
/* the free block new ones are cut from, in no list: cutting from it and
   giving back to it cost a few moves (one after another, as a program
   reads a file, is the common case) */
static BLK *dv;

/* an accessory can't take memory from GEMDOS once it runs: under plain
   TOS it would belong to whatever program is in front, and go when that
   one quits. So it takes one block at start-up and lives in it. */
static int heap_fixed;

static unsigned bin_of(size_t s)
{
	size_t x = s;
	unsigned lg = 0;
	if (s < SMALL)
		return (unsigned)(s / ALIGN);
	if (s >> 30 >> 1)
		return NBINS - 1;	/* 2 GB and up */
	/* the top bit, in shifts the 68000 does quickly (by a register it
	   takes two cycles a bit) */
	if (x >> 16) {
		x >>= 16;
		lg = 16;
	}
	if (x >> 8) {
		x >>= 8;
		lg += 8;
	}
	if (x >> 4) {
		x >>= 4;
		lg += 4;
	}
	if (x >> 2) {
		x >>= 2;
		lg += 2;
	}
	lg += (unsigned)(x >> 1);
	return NSMALL + (lg - SMALL_LG) * 4 + (unsigned)((s >> (lg - 2)) & 3);
}

/* the first list from i on that has a block, or -1 */
static int bin_next(unsigned i)
{
	unsigned w = i >> 5;
	u32 m;
	if (i >= NBINS)
		return -1;
	m = binmap[w] & (~(u32)0 << (i & 31));
	while (!m) {
		if (++w >= NMAP)
			return -1;
		m = binmap[w];
	}
	i = w << 5;
	if (!(m & 0xFFFF)) {
		m >>= 16;
		i += 16;
	}
	if (!(m & 0xFF)) {
		m >>= 8;
		i += 8;
	}
	if (!(m & 0xF)) {
		m >>= 4;
		i += 4;
	}
	if (!(m & 3)) {
		m >>= 2;
		i += 2;
	}
	return (int)(i + !(m & 1));
}

#define INLINE static inline __attribute__((always_inline))

INLINE void bin_put(BLK *b, unsigned i)
{
	b->prev = 0;
	if ((b->next = bins[i]))
		b->next->prev = b;
	bins[i] = b;
	binmap[i >> 5] |= (u32)1 << (i & 31);
}

/* b out of list i */
INLINE void bin_take(BLK *b, unsigned i)
{
	if (b->next)
		b->next->prev = b->prev;
	if (b->prev)
		b->prev->next = b->next;
	else if (!(bins[i] = b->next))
		binmap[i >> 5] &= ~((u32)1 << (i & 31));
}

/* nb in o's place in list i */
INLINE void bin_swap(BLK *o, BLK *nb, unsigned i)
{
	BLK *nx = o->next, *pv = o->prev;
	nb->next = nx;
	nb->prev = pv;
	if (nx)
		nx->prev = nb;
	if (pv)
		pv->next = nb;
	else
		bins[i] = nb;
}

/* b, free and in list i, is now s bytes: it says so at both ends, and
   changes lists only when its size belongs in another */
INLINE void resize(BLK *b, unsigned i, size_t s)
{
	unsigned j = bin_of(s);
	b->head = s | PINUSE;
	FOOT(b, s) = s;
	if (j != i) {
		bin_take(b, i);
		bin_put(b, j);
	}
}

/* b, of s bytes, becomes a free block (the one before it is in use) */
static void make_free(BLK *b, size_t s)
{
	b->head = s | PINUSE;
	FOOT(b, s) = s;
	AT(b, s)->head &= ~PINUSE;
	bin_put(b, bin_of(s));
}

#ifdef HEAP_TEST
static char *area_lo[64], *area_hi[64];
static int areas;
#endif

/* a new area: one free block, and the fence */
static int add_area(void *p, size_t bytes)
{
	char *lo = (char *)(((unsigned long)p + ALIGN - 1) & ~(unsigned long)(ALIGN - 1));
	char *hi = (char *)(((unsigned long)p + bytes) & ~(unsigned long)(ALIGN - 1));
	if (hi < lo + MINBLK + WORD)
		return 0;
	hi -= WORD;
	((BLK *)hi)->head = INUSE;
	make_free((BLK *)lo, (size_t)(hi - lo));
#ifdef HEAP_TEST
	area_lo[areas] = lo;
	area_hi[areas++] = hi;
#endif
	return 1;
}

void heap_add(void *p, size_t bytes)
{
	add_area(p, bytes);
	heap_fixed = 1;
}

#define MAX_BLOCKS 40		/* TOS's pool is shared with every program and folder */

long heap_tt, heap_st;		/* bytes the heap took from TT/Fast and ST RAM (About) */
long heap_early;		/* of those, an accessory's before its heap_add (its log says so) */

extern short _is_acc;		/* crt0.S (tests/heap: its own) */

/* more memory from GEMDOS for a block of need bytes (kept out of malloc,
   whose registers it would cost) */
static __attribute__((noinline)) int more_core(size_t need)
{
	static short blocks;		/* GEMDOS blocks taken, a pool entry each */
	static size_t taken;		/* bytes taken from GEMDOS so far */
	size_t least = need + WORD + 2 * ALIGN, want;	/* the block, the fence, alignment */
	void *h = 0;
	if (heap_fixed)
		return 0;
	if (_is_acc) {
		/* an accessory before its heap_add (acc_heap): what it asks for
		   is taken as it is, in 16 KB steps, never the 512 KB first
		   block, which it would hold beside its heap for good (ECal's
		   PEOPLE cache before acc_heap held 512 KB, 20 Oct 2026). Its
		   log can name it: heap_early */
		want = (least + 16383) & ~(size_t)16383;
		h = Mxalloc(want, 3);
		if ((long)h == -32 || (long)h <= 0)
			h = Malloc(want);
		if ((long)h <= 0)
			return 0;
		blocks++;
		taken += want;
		heap_early += want;
		if ((unsigned long)h >= 0x01000000UL)
			heap_tt += want;
		else
			heap_st += want;
		return add_area(h, want);
	}
	/* Every GEMDOS block takes an entry in TOS's small internal pool, and
	   when that runs out TOS halts the machine ("OUT OF INTERNAL MEMORY").
	   So few blocks: 512 KB at first, then each as big as all before it,
	   so the heap doubles and 16 MB takes six. Less if that much isn't
	   free; at worst exactly what is needed. */
	want = least < 512L * 1024 ? 512L * 1024 : least;
	if (!taken) {
		/* the first block: with no TT RAM, at most a quarter of what is
		   free, so a 1 MB ST keeps room for a job's buffer (bigbuf.c)
		   and for GEM; the heap still doubles from there as it needs */
		long tt = (long)Mxalloc(-1L, 1), st;
		if (tt == -32 || tt <= 0) {
			st = tt == -32 ? (long)Malloc(-1L) : (long)Mxalloc(-1L, 0);
			if (st > 0 && (size_t)(st / 4) < want)
				want = (size_t)(st / 4) > least ? (size_t)(st / 4) & ~(size_t)1 : least;
		}
	}
	if (want < taken)
		want = taken;
	if (blocks >= MAX_BLOCKS)
		return 0;		/* "out of memory" beats a halted machine */
	while (!h) {
		h = Mxalloc(want, 3);
		if ((long)h == -32 || (long)h <= 0)
			h = Malloc(want);
		if ((long)h <= 0) {
			h = 0;
			if (want == least)
				return 0;
			want = want / 2 > least ? want / 2 : least;
		}
	}
	taken += want;
	blocks++;
	if ((unsigned long)h >= 0x01000000UL)	/* above the ST's 16 MB */
		heap_tt += want;
	else
		heap_st += want;
	return add_area(h, want);
}

static size_t block_for(size_t n)
{
	size_t need = (n + WORD + ALIGN - 1) & ~(ALIGN - 1);
	return need < MINBLK ? MINBLK : need;
}

static void *malloc_more(size_t need);

/* the bytes of the blocks in use, and the most since heap_peak was last
   set: an accessory keeps what its data needs, not a flat share */
long heap_use, heap_peak;

/* the common cases first, in few registers: a small block of a size that
   is free, or one cut from the end of dv */
static inline void *malloc0(size_t n)
{
	size_t need, s;
	BLK *b;
	if (n >= SMALL - WORD - ALIGN)
		return n > MAXREQ ? 0 : malloc_more(block_for(n));
	need = block_for(n);
	if ((b = bins[need / ALIGN])) {
		bin_take(b, need / ALIGN);
		b->head |= INUSE;
		AT(b, need)->head |= PINUSE;
		return (char *)b + WORD;
	}
	if (dv && (s = SIZE(dv)) >= need + MINBLK) {
		b = AT(dv, s - need);
		dv->head = (s - need) | PINUSE;
		FOOT(dv, s - need) = s - need;
		b->head = need | INUSE;
		AT(b, need)->head |= PINUSE;
		return (char *)b + WORD;
	}
	return malloc_more(need);
}

void *malloc(size_t n)
{
	void *p = malloc0(n);
	if (p && (heap_use += (long)SIZE((BLK *)((char *)p - WORD))) > heap_peak)
		heap_peak = heap_use;
	return p;
}

/* the rest: the lists, dv, more from GEMDOS */
static __attribute__((noinline)) void *malloc_more(size_t need)
{
	size_t s;
	BLK *b;
	int i;
	for (;;) {
		i = (int)bin_of(need);
		if (need < SMALL) {
			if ((b = bins[i]))	/* one of its size */
				break;
		} else {
			/* its own list: sizes near it, the first that fits */
			for (b = bins[i]; b && SIZE(b) < need; b = b->next)
				;
			if (b)
				break;
		}
		if (dv && SIZE(dv) >= need && need < BIG)
			goto cut;
		/* any block in a later list is big enough. A big block is cut
		   from its end and dv stays where the small ones are: made dv,
		   the small blocks that stay were cut from beside each big
		   buffer, and once those went no big piece was left (ECal's
		   refresh in Falcon mode: 3.6 MB free, none of it 40 KB) */
		if ((i = bin_next((unsigned)i + 1)) >= 0) {
			b = bins[i];
			if (SIZE(b) - need < MINBLK || need >= BIG)
				break;
			bin_take(b, (unsigned)i);	/* it is cut from now on */
			if (dv)
				bin_put(dv, bin_of(SIZE(dv)));
			dv = b;
			goto cut;
		}
		if (dv && SIZE(dv) >= need)
			goto cut;		/* a big one, nothing else fits */
		if (!more_core(need))
			return 0;
	}
	/* b, in list i */
	s = SIZE(b);
	if (s - need >= MINBLK) {
		/* its end: the rest stays where it is, in its list as a rule */
		BLK *a = AT(b, s - need);
		resize(b, (unsigned)i, s - need);
		a->head = need | INUSE;		/* (the one before it is free) */
		AT(a, need)->head |= PINUSE;
		return (char *)a + WORD;
	}
	bin_take(b, (unsigned)i);
	b->head |= INUSE;
	AT(b, s)->head |= PINUSE;
	return (char *)b + WORD;
cut:
	s = SIZE(dv);
	if (s - need >= MINBLK) {
		BLK *a = AT(dv, s - need);
		dv->head = (s - need) | PINUSE;
		FOOT(dv, s - need) = s - need;
		a->head = need | INUSE;
		AT(a, need)->head |= PINUSE;
		return (char *)a + WORD;
	}
	b = dv;
	dv = 0;
	b->head |= INUSE;
	AT(b, s)->head |= PINUSE;
	return (char *)b + WORD;
}

static void free_more(BLK *b);

/* the common case first, in few registers: back into dv, just before it */
void free(void *ptr)
{
	BLK *b, *n;
	size_t s, ps;
	if (!ptr)
		return;
	b = (BLK *)((char *)ptr - WORD);
	if (!(b->head & INUSE))
		return;			/* freed twice: left alone */
	b->head &= ~INUSE;		/* (so it shows, even once joined) */
	s = SIZE(b);
	heap_use -= (long)s;
	n = AT(b, s);
	if (!(b->head & PINUSE) && (n->head & INUSE) && dv &&
	    (BLK *)((char *)b - (ps = *((size_t *)b - 1))) == dv) {
		s += ps;
		dv->head = s | PINUSE;
		FOOT(dv, s) = s;
		n->head &= ~PINUSE;
		return;
	}
	free_more(b);
}

/* the rest: b (no longer in use) joined with free neighbours */
static __attribute__((noinline)) void free_more(BLK *b)
{
	BLK *n;
	size_t s, ns = 0;
	s = SIZE(b);
	n = AT(b, s);
	if (!(n->head & INUSE))
		ns = SIZE(n);		/* the one after is free: joined */
	if (!(b->head & PINUSE)) {
		/* the one before is free: it grows where it is */
		size_t ps = *((size_t *)b - 1);
		BLK *p = (BLK *)((char *)b - ps);
		if (ns) {
			if (n == dv) {
				bin_take(p, bin_of(ps));
				dv = p;
			} else
				bin_take(n, bin_of(ns));
			n = AT(n, ns);
		}
		s += ps + ns;
		if (p == dv) {
			p->head = s | PINUSE;
			FOOT(p, s) = s;
		} else
			resize(p, bin_of(ps), s);
		n->head &= ~PINUSE;
		return;
	}
	if (ns) {			/* b takes the place of the one after */
		unsigned ni, j;
		s += ns;
		b->head = s | PINUSE;
		FOOT(b, s) = s;
		if (n == dv)
			dv = b;
		else if ((j = bin_of(s)) == (ni = bin_of(ns)))
			bin_swap(n, b, ni);
		else {
			bin_take(n, ni);
			bin_put(b, j);
		}
		return;
	}
	make_free(b, s);
}

void *calloc(size_t n, size_t m)
{
	void *p;
	if ((n | m) >= 0x10000 && m && n > (size_t)-1 / m)
		return 0;
	if ((p = malloc(n * m)))
		memset(p, 0, n * m);
	return p;
}

void *realloc(void *ptr, size_t n)
{
	BLK *b, *nx;
	size_t s, t, need;
	void *np;
	if (!ptr)
		return malloc(n);
	if (n > MAXREQ)
		return 0;
	b = (BLK *)((char *)ptr - WORD);
	s = SIZE(b);
	need = block_for(n);
	if (s >= need) {
		/* it fits; a block asked for much less gives its end back, in
		   place (a converter's result, cut to what it used: ECal kept
		   three times each title's length) */
		if (s - need >= MINBLK) {
			BLK *t = AT(b, need);
			b->head = need | (b->head & FLAGS);
			t->head = (s - need) | PINUSE;	/* freed, as free() would */
			heap_use -= (long)(s - need);
			free_more(t);
		}
		return ptr;
	}
	nx = AT(b, s);
	if (!(nx->head & INUSE) && (t = s + SIZE(nx)) >= need) {
		/* grow into the free block after it */
		int was_dv = nx == dv;
		if (was_dv)
			dv = 0;
		else
			bin_take(nx, bin_of(SIZE(nx)));
		if ((heap_use += (long)((t - need >= MINBLK ? need : t) - s)) > heap_peak)
			heap_peak = heap_use;
		if (t - need >= MINBLK) {
			b->head = need | (b->head & FLAGS);
			make_free(AT(b, need), t - need);
			if (was_dv) {		/* (the rest is still the one cut from) */
				dv = AT(b, need);
				bin_take(dv, bin_of(t - need));
			}
		} else {
			b->head = t | (b->head & FLAGS);
			AT(b, t)->head |= PINUSE;
		}
		return ptr;
	}
	if (!(np = malloc(n)))
		return 0;
	memcpy(np, ptr, s - WORD);
	free(ptr);
	return np;
}

#ifdef HEAP_TEST
unsigned heap_bin(size_t s)
{
	return bin_of(s);
}

/* walks every area: sizes and flags agree, no two free blocks side by
   side, every free block in its list and every list's blocks free and of
   its size. The free bytes, and the free blocks in *nfree; -1 if wrong. */
long heap_check_big;		/* heap_check: the largest free block */

long heap_check(long *nfree)
{
	long bytes = 0, blocks = 0, listed = 0;
	heap_check_big = 0;
	int a, dv_seen = 0;
	unsigned i;
	for (a = 0; a < areas; a++) {
		BLK *b = (BLK *)area_lo[a];
		int prev_used = 1;
		while ((char *)b < area_hi[a]) {
			size_t s = SIZE(b);
			if (s < MINBLK || s % ALIGN || (char *)b + s > area_hi[a])
				return -1;
			if (!!(b->head & PINUSE) != prev_used)
				return -1;
			if (b == dv)
				dv_seen = 1;
			if (!(b->head & INUSE)) {
				if (!prev_used || FOOT(b, s) != s)
					return -1;
				bytes += (long)s;
				blocks++;
				if ((long)s > heap_check_big)
					heap_check_big = (long)s;
			}
			prev_used = !!(b->head & INUSE);
			b = AT(b, s);
		}
		if ((char *)b != area_hi[a] || SIZE(b) || !(b->head & INUSE) ||
		    !!(b->head & PINUSE) != prev_used)
			return -1;
	}
	for (i = 0; i < NBINS; i++) {
		BLK *b, *p = 0;
		if (!bins[i] != !(binmap[i >> 5] & ((u32)1 << (i & 31))))
			return -1;
		for (b = bins[i]; b; p = b, b = b->next) {
			if ((b->head & INUSE) || bin_of(SIZE(b)) != i || b->prev != p || b == dv)
				return -1;
			listed++;
		}
	}
	if (dv && ((dv->head & INUSE) || !dv_seen))
		return -1;
	if (listed + (dv != 0) != blocks)
		return -1;
	if (nfree)
		*nfree = blocks;
	return bytes;
}
#endif

#endif	/* LIBC_NO_HEAP */

#ifndef HEAP_TEST

/* ---------------- numbers ---------------- */

unsigned long strtoul(const char *s, char **end, int base_)
{
	unsigned long v = 0;
	while (isspace((u8)*s))
		s++;
	if (*s == '+')
		s++;
	if ((base_ == 0 || base_ == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base_ = 16;
	} else if (base_ == 0) {
		base_ = 10;
	}
	for (;; s++) {
		int d;
		if (isdigit((u8)*s))
			d = *s - '0';
		else if (isalpha((u8)*s))
			d = tolower((u8)*s) - 'a' + 10;
		else
			break;
		if (d >= base_)
			break;
		v = v * base_ + d;
	}
	if (end)
		*end = (char *)s;
	return v;
}

long strtol(const char *s, char **end, int base_)
{
	int neg = 0;
	while (isspace((u8)*s))
		s++;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	return neg ? -(long)strtoul(s, end, base_) : (long)strtoul(s, end, base_);
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* heap sort: no recursion, no extra memory, and n log n even for the
   list of thousands of contacts sorted again after every change (the
   shell sort it replaces went quadratic there). Pointers (the usual
   case) are swapped as longs */
/* two elements swapped: by longs or shorts where both are on even
   addresses and the size allows, else a byte at a time (ECal's reminder
   index of 43 elements took 60 ms on an ST, byte by byte) */
static void swap_el(char *x, char *y, size_t size)
{
	if (size == sizeof(long) && !(((unsigned long)x | (unsigned long)y) & 1)) {
		long t = *(long *)x;
		*(long *)x = *(long *)y;
		*(long *)y = t;
	} else if (!((size | (unsigned long)x | (unsigned long)y) & 1)) {
		long *lx = (long *)x, *ly = (long *)y, t;
		for (; size >= sizeof(long); size -= sizeof(long)) {
			t = *lx;
			*lx++ = *ly;
			*ly++ = t;
		}
		for (x = (char *)lx, y = (char *)ly; size; size -= sizeof(short)) {
			short u = *(short *)x;	/* the shorts left */
			*(short *)x = *(short *)y;
			*(short *)y = u;
			x += sizeof(short);
			y += sizeof(short);
		}
	} else {
		while (size--) {
			char t = *x;
			*x++ = *y;
			*y++ = t;
		}
	}
}

static void sift(char *b, size_t root, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1;
		if (child >= n)
			return;
		if (child + 1 < n && cmp(b + child * size, b + (child + 1) * size) < 0)
			child++;
		if (cmp(b + root * size, b + child * size) >= 0)
			return;
		swap_el(b + root * size, b + child * size, size);
		root = child;
	}
}

/* the same for arrays of pointers, indexed without a multiply (a 68000
   multiplies 32-bit numbers in software) */
static void sift_ptr(void **a, size_t root, size_t n, int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1;
		void *t;
		if (child >= n)
			return;
		if (child + 1 < n && cmp(&a[child], &a[child + 1]) < 0)
			child++;
		if (cmp(&a[root], &a[child]) >= 0)
			return;
		t = a[root];
		a[root] = a[child];
		a[child] = t;
		root = child;
	}
}

void qsort(void *base_, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	char *b = base_;
	size_t i;
	if (n < 2)
		return;
	if (size == sizeof(void *) && !((unsigned long)b & 1)) {
		void **a = base_, *t;
		for (i = n / 2; i-- > 0;)
			sift_ptr(a, i, n, cmp);
		for (i = n - 1; i > 0; i--) {
			t = a[0];
			a[0] = a[i];
			a[i] = t;
			sift_ptr(a, 0, i, cmp);
		}
		return;
	}
	for (i = n / 2; i-- > 0;)
		sift(b, i, n, size, cmp);
	for (i = n - 1; i > 0; i--) {
		swap_el(b, b + i * size, size);
		sift(b, 0, i, size, cmp);
	}
}

void exit(int code)
{
	trap1_ww(0x4c, (short)code);
	for (;;)
		;
}

/* ---------------- vsnprintf ---------------- */

typedef struct {
	char *p;
	size_t left;
	int count;
} OUT;

static void put(OUT *o, char c)
{
	if (o->left > 1) {
		*o->p++ = c;
		o->left--;
	}
	o->count++;
}

static void put_num(OUT *o, unsigned long v, int base_, int upper, int neg,
		    int width, int zero, int left)
{
	char tmp[12];
	int n = 0, len;
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	do {
		tmp[n++] = dig[v % base_];
		v /= base_;
	} while (v);
	len = n + neg;
	if (neg && zero)
		put(o, '-');
	if (!left)
		while (width-- > len)
			put(o, zero ? '0' : ' ');
	if (neg && !zero)
		put(o, '-');
	while (n)
		put(o, tmp[--n]);
	if (left)
		while (width-- > len)
			put(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	OUT o;
	o.p = buf;
	o.left = size;
	o.count = 0;

	for (; *fmt; fmt++) {
		int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		fmt++;
		for (;; fmt++) {
			if (*fmt == '-')
				left = 1;
			else if (*fmt == '0')
				zero = 1;
			else
				break;
		}
		if (*fmt == '*') {
			width = va_arg(ap, int);
			fmt++;
		} else {
			while (isdigit((u8)*fmt))
				width = width * 10 + *fmt++ - '0';
		}
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			} else {
				while (isdigit((u8)*fmt))
					prec = prec * 10 + *fmt++ - '0';
			}
		}
		while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') {
			lng = *fmt != 'h';
			fmt++;
		}
		(void)lng;	/* int and long are both 32 bits */
		switch (*fmt) {
		case 'd':
		case 'i': {
			long v = va_arg(ap, long);
			put_num(&o, v < 0 ? -(unsigned long)v : (unsigned long)v, 10, 0, v < 0, width, zero, left);
			break;
		}
		case 'u':
			put_num(&o, va_arg(ap, unsigned long), 10, 0, 0, width, zero, left);
			break;
		case 'x':
		case 'X':
			put_num(&o, va_arg(ap, unsigned long), 16, *fmt == 'X', 0, width, zero, left);
			break;
		case 'p':
			put(&o, '0');
			put(&o, 'x');
			put_num(&o, (unsigned long)va_arg(ap, void *), 16, 0, 0, 8, 1, 0);
			break;
		case 'c':
			put(&o, (char)va_arg(ap, int));
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len;
			if (!s)
				s = "(null)";
			for (len = 0; s[len] && (prec < 0 || len < prec); len++)
				;
			if (!left)
				while (width-- > len)
					put(&o, ' ');
			while (len--)
				put(&o, *s++), width--;
			if (left)
				while (width-- > 0)
					put(&o, ' ');
			break;
		}
		case '%':
			put(&o, '%');
			break;
		case 0:
			fmt--;
			break;
		default:
			put(&o, '%');
			put(&o, *fmt);
		}
	}
	if (size)
		*o.p = 0;
	return o.count;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vsnprintf(buf, 0x7fffffff, fmt, ap);
	va_end(ap);
	return r;
}

#endif
