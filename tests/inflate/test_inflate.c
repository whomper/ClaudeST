/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_inflate.c - src/inflate.c's streaming decoder (inflate_open/read)
 * against inflate_raw and the original bytes: text, random and repetitive
 * data compressed by zlib at every level and strategy, with stored blocks
 * after compressed ones (deflateParams mid-stream), read through random
 * input and output piece sizes; and damaged and cut-short data refused.
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "inflate.h"

static unsigned long seed = 7;
static unsigned rnd(unsigned n)
{
	seed = seed * 1103515245UL + 12345UL;
	return (unsigned)((seed >> 16) % n);
}

typedef struct {
	const unsigned char *p;
	long n, at, most;
} SRC;

static int get(void *u, unsigned char *buf, long n)
{
	SRC *s = u;
	long k = s->n - s->at;
	if (n > s->most)
		n = s->most;
	if (k > n)
		k = n;
	if (k > 1)
		k = 1 + (long)rnd((unsigned)k);	/* pieces of any size */
	memcpy(buf, s->p + s->at, k);
	s->at += k;
	return (int)k;
}

static long deflate_raw(const unsigned char *in, long n, unsigned char *out, long cap, int level, int strat, int mixed)
{
	z_stream z;
	memset(&z, 0, sizeof(z));
	deflateInit2(&z, level, Z_DEFLATED, -15, 9, strat);
	z.next_out = out;
	z.avail_out = (uInt)cap;
	if (mixed == 2) {			/* sync flushes: an empty stored block after each piece */
		long at = 0;
		z.next_in = (unsigned char *)in;
		while (at < n) {
			long k = 1 + (long)rnd(5000);
			if (k > n - at)
				k = n - at;
			z.avail_in = (uInt)k;
			deflate(&z, Z_SYNC_FLUSH);
			at += k;
		}
	} else if (mixed) {			/* a third stored, then compressed, then stored */
		long third = n / 3;
		z.next_in = (unsigned char *)in;
		z.avail_in = (uInt)third;
		deflateParams(&z, 0, Z_DEFAULT_STRATEGY);
		deflate(&z, Z_BLOCK);
		deflateParams(&z, 9, Z_DEFAULT_STRATEGY);
		z.avail_in += (uInt)third;
		deflate(&z, Z_BLOCK);
		deflateParams(&z, 0, Z_DEFAULT_STRATEGY);
		z.avail_in += (uInt)(n - 2 * third);
	} else {
		z.next_in = (unsigned char *)in;
		z.avail_in = (uInt)n;
	}
	deflate(&z, Z_FINISH);
	deflateEnd(&z);
	return (long)(cap - z.avail_out);
}

static long fails, checks;
static void check(int ok, const char *what)
{
	checks++;
	if (!ok && fails++ < 10)
		printf("FAIL %s\n", what);
}

int main(void)
{
	static unsigned char data[300000], comp[400000], back[300000];
	static const int strats[] = { Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED };
	int kind, level, st, mixed;
	for (kind = 0; kind < 4; kind++) {
		long n = kind == 3 ? 70000 + rnd(200000) : 1 + rnd(120000), i;
		for (i = 0; i < n; i++)
			data[i] = kind == 0 ? (unsigned char)"the quick brown fox jumps over the lazy dog. "[i % 45] ^ (rnd(50) ? 0 : 32)
				: kind == 1 ? (unsigned char)rnd(256)
				: kind == 2 ? (unsigned char)(i / 1000)
				: (unsigned char)(rnd(4) ? data[i > 40000 ? i - 33000 + rnd(3) : 0] : rnd(256));
		for (level = 0; level <= 9; level += 3)
			for (st = 0; st < 5; st++)
				for (mixed = 0; mixed < 3; mixed++) {
					long c = deflate_raw(data, n, comp, sizeof(comp), level, strats[st], mixed), got = 0, r;
					SBUF ref;
					SRC src = { comp, c, 0, 1 + rnd(6000) };
					INFLATE *z = inflate_open(get, &src);
					char what[96];
					snprintf(what, sizeof(what), "kind %d level %d strategy %d mixed %d, %ld bytes", kind, level, strats[st], mixed, n);
					memset(&ref, 0, sizeof(ref));
					check(inflate_raw(comp, c, &ref, 1L << 30) == 1 && ref.len == n && !memcmp(ref.s, data, n), what);
					free(ref.s);
					while ((r = inflate_read(z, back + got, 1 + rnd(40000))) > 0)
						got += r;
					check(r == 0 && got == n && !memcmp(back, data, n), what);
					inflate_close(z);
					/* cut short: an error, never an end */
					if (c > 10) {
						SRC cut = { comp, c / 2, 0, 4096 };
						z = inflate_open(get, &cut);
						got = 0;
						while ((r = inflate_read(z, back + got, 30000)) > 0)
							got += r;
						check(r < 0, "cut short");
						/* what came before the cut, as it was */
						check(got <= n && !memcmp(back, data, got) && (c < 2000 || got > 0), "cut short: the start");
						inflate_close(z);
					}
				}
	}
	/* damaged: random bytes refused or ended, never a crash (ASan) */
	{
		int t;
		for (t = 0; t < 2000; t++) {
			long c = 1 + rnd(2000), i, got = 0, r;
			SRC src;
			INFLATE *z;
			for (i = 0; i < c; i++)
				comp[i] = (unsigned char)rnd(256);
			src.p = comp;
			src.n = c;
			src.at = 0;
			src.most = 512;
			z = inflate_open(get, &src);
			while ((r = inflate_read(z, back, 4096)) > 0 && (got += r) < 2000000)
				;
			inflate_close(z);
		}
		checks++;
	}
	printf("inflate: %ld checks, %s\n", checks, fails ? "FAILED" : "all passed");
	return fails != 0;
}
