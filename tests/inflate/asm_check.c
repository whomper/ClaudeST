/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * asm_check.c - inflate_piece's hot loop in 68000 assembly
 * (atari/inflate_fast.S, INFLATE_ASM) against its C, byte for byte.
 *
 *   asm_check gen CORPUS [FILE...]   the streams (zlib's, built -DGEN -lz;
 *                                    FILEs: raw DEFLATE streams as well)
 *   asm_check run CORPUS OUT         each stream through inflate_piece in
 *                                    pieces of set sizes: every byte and
 *                                    every answer into OUT
 *
 * make asmcheck runs it with the C here and with the assembly built for
 * m68k Linux under qemu-m68k, and compares the two OUTs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inflate.h"
#ifdef GEN
#include <zlib.h>
#endif

static unsigned int seed;		/* 32 bits on both: the same pieces */
static unsigned rnd(unsigned n)
{
	seed = seed * 1103515245U + 12345U;
	return (unsigned)((seed >> 16) % n);
}

static void put32(FILE *f, unsigned long v)
{
	putc((int)(v >> 24) & 255, f);
	putc((int)(v >> 16) & 255, f);
	putc((int)(v >> 8) & 255, f);
	putc((int)v & 255, f);
}

static long get32(FILE *f)
{
	long v = 0;
	int i, c;
	for (i = 0; i < 4; i++) {
		if ((c = getc(f)) == EOF)
			return -1;
		v = v << 8 | c;
	}
	return v;
}

#ifdef GEN
static long deflate_raw(const unsigned char *in, long n, unsigned char *out, long cap, int level, int strat, int mixed)
{
	z_stream z;
	memset(&z, 0, sizeof(z));
	deflateInit2(&z, level, Z_DEFLATED, -15, 9, strat);
	z.next_out = out;
	z.avail_out = (uInt)cap;
	if (mixed == 2) {
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
	} else if (mixed) {
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

static void record(FILE *f, const unsigned char *p, long n)
{
	put32(f, (unsigned long)n);
	fwrite(p, 1, (size_t)n, f);
}

static int gen(const char *path, char **files, int nfiles)
{
	static unsigned char data[300000], comp[400000];
	static const int strats[] = { Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED };
	FILE *f = fopen(path, "wb");
	int kind, level, st, mixed, t, i;
	seed = 7;
	if (!f)
		return 1;
	for (kind = 0; kind < 4; kind++) {
		long n = kind == 3 ? 70000 + rnd(200000) : 1 + rnd(120000), j;
		for (j = 0; j < n; j++)
			data[j] = kind == 0 ? (unsigned char)"the quick brown fox jumps over the lazy dog. "[j % 45] ^ (rnd(50) ? 0 : 32)
				: kind == 1 ? (unsigned char)rnd(256)
				: kind == 2 ? (unsigned char)(j / 1000)
				: (unsigned char)(rnd(4) ? data[j > 40000 ? j - 33000 + rnd(3) : 0] : rnd(256));
		for (level = 0; level <= 9; level += 3)
			for (st = 0; st < 5; st++)
				for (mixed = 0; mixed < 3; mixed++) {
					long c = deflate_raw(data, n, comp, sizeof(comp), level, strats[st], mixed);
					record(f, comp, c);
					record(f, comp, c / 2);		/* cut short */
				}
	}
	for (t = 0; t < 500; t++) {			/* random bytes */
		long c = 1 + rnd(2000);
		for (i = 0; i < c; i++)
			comp[i] = (unsigned char)rnd(256);
		record(f, comp, c);
	}
	for (i = 0; i < nfiles; i++) {
		FILE *g = fopen(files[i], "rb");
		static unsigned char big[4000000];
		long n;
		if (!g)
			return 1;
		n = (long)fread(big, 1, sizeof(big), g);
		fclose(g);
		record(f, big, n);
	}
	fclose(f);
	return 0;
}
#endif

typedef struct {
	const unsigned char *p;
	long n, at;
} SRC;

static int get(void *u, unsigned char *buf, long n)
{
	SRC *s = u;
	long k = s->n - s->at;
	if (k > n)
		k = n;
	if (k > 1)
		k = 1 + (long)rnd((unsigned)k);		/* pieces of any size, the same on both */
	memcpy(buf, s->p + s->at, (size_t)k);
	s->at += k;
	return (int)k;
}

static int run(const char *path, const char *outpath)
{
	FILE *f = fopen(path, "rb"), *o = fopen(outpath, "wb");
	unsigned char *comp = malloc(4000000);
	long n, streams = 0;
	if (!f || !o || !comp)
		return 1;
	seed = 11;
	while ((n = get32(f)) >= 0) {
		SRC src;
		INFLATE *z;
		const unsigned char *p;
		long r, total = 0;
		if (fread(comp, 1, (size_t)n, f) != (size_t)n)
			return 1;
		src.p = comp;
		src.n = n;
		src.at = 0;
		z = inflate_open_piece(get, &src);
		do {
			r = inflate_piece(z, &p, 1 + rnd(40000), 0);
			put32(o, (unsigned long)r);
			if (r > 0) {
				fwrite(p, 1, (size_t)r, o);
				total += r;
			}
		} while (r > 0 && total < 50000000L);
		inflate_close(z);
		streams++;
	}
	fclose(o);
	fclose(f);
	printf("asm_check: %ld streams\n", streams);
	return 0;
}

int main(int argc, char **argv)
{
#ifdef GEN
	if (argc >= 3 && !strcmp(argv[1], "gen"))
		return gen(argv[2], argv + 3, argc - 3);
#endif
	if (argc == 4 && !strcmp(argv[1], "run"))
		return run(argv[2], argv[3]);
	fprintf(stderr, "asm_check gen CORPUS [FILE...] | run CORPUS OUT\n");
	return 2;
}
