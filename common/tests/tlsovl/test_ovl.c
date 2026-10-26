/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_ovl.c - src/ovl.c on the build machine. With TLS.OVL: loaded at
 * wherever malloc put it, every long its fixups name moved by that much
 * and nothing else changed, the BSS zeroed, TLS_OVL's table at the start.
 * Then fixup tables made by hand: the first offset split across pieces,
 * the 254-byte steps, and damaged ones refused (an odd step, a fixup past
 * the data, a file cut short). Then ovl_load_tail on files made here:
 * the bytes after the fixups (EWEBJS.OVL's imports) handed out whole and
 * in order, wherever the table ends in the pieces read, a tail refused
 * making the file damaged, and why a load failed (for good or for now).
 *
 *     test_ovl TLS.OVL
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "plat.h"
#include "ovl.h"

void (*pf_idle)(void);
static int idles, fails;
static void idle(void) { idles++; }

static FILE *fh[4];

int pf_open(const char *path, int mode)
{
	int i;
	(void)mode;
	for (i = 0; i < 4; i++)
		if (!fh[i])
			return (fh[i] = fopen(path, "rb")) ? i : -1;
	return -1;
}
long pf_read(int h, void *buf, long n) { return (long)fread(buf, 1, (size_t)n, fh[h]); }
long pf_seek(int h, long off) { return fseek(fh[h], off, SEEK_SET) ? -1 : off; }
void pf_close(int h)
{
	if (h >= 0 && fh[h]) {
		fclose(fh[h]);
		fh[h] = 0;
	}
}

static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		fails++;
}

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static unsigned char *slurp(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *d;
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc((size_t)*n);
	if (fread(d, 1, (size_t)*n, f) != (size_t)*n) {
		free(d);
		d = 0;
	}
	fclose(f);
	return d;
}

static void real(const char *path)
{
	long n, taken, t, d, b, s, i, at, nfix = 0;
	unsigned char *file = slurp(path, &n), *img, *fixed, *fx;
	char err[120];
	if (!file) {
		check(0, "TLS.OVL read");
		return;
	}
	t = be32(file + 2), d = be32(file + 6), b = be32(file + 10), s = be32(file + 14);
	pf_idle = idle;
	img = ovl_load(path, &taken, err, sizeof(err));
	check(img != 0, img ? "TLS.OVL loaded" : err);
	if (!img)
		return;
	check(taken == t + d + b, "it holds text, data and BSS");
	check(idles == 1 && !fh[0], "pf_idle once, after the file was closed");
	/* the fixups again, here, to see that they and only they changed */
	fixed = calloc((size_t)(t + d), 1);
	fx = file + 28 + t + d + s;
	at = (long)be32(fx);
	if (at) {
		fixed[at] = 1;
		nfix++;
		for (fx += 4; *fx; fx++) {
			at += *fx == 1 ? 254 : *fx;
			if (*fx != 1) {
				fixed[at] = 1;
				nfix++;
			}
		}
	}
	for (i = 0; i < t + d; i++) {
		if (fixed[i]) {
			uint32_t want = be32(file + 28 + i) + (uint32_t)(uintptr_t)img;
			if (be32(img + i) != want)
				break;
			i += 3;
		} else if (img[i] != file[28 + i])
			break;
	}
	check(i >= t + d, "each fixup moved by where it landed, nothing else changed");
	for (i = 0; i < b && !img[t + d + i]; i++)
		;
	check(i == b, "the BSS zeroed");
	check(be32(img) == 0x544C534F && img[4] == 0 && img[5] == 1, "TLS_OVL's magic and major 1 at the start");
	check(nfix > 100, "it has fixups (the table's pointers)");
	printf("      text %ld, data %ld, bss %ld, %ld fixups, version %.7s\n", t, d, b, nfix, (char *)img + 8);
	free(fixed);
	free(img);
	free(file);
}

/* a fixup table fed in pieces of k bytes */
static int feed(unsigned char *img, long size, const unsigned char *tab, long n, long k)
{
	OVL_FIX f;
	long i;
	int r = 1;
	ovl_fix_start(&f, img, size);
	for (i = 0; i < n && r == 1; i += k)
		r = ovl_fix_feed(&f, tab + i, n - i < k ? n - i : k);
	return r;
}

static void made(void)
{
	unsigned char img[1024], tab[16];
	uint32_t base;
	int k, ok = 1;
	/* fixups at 2, 2 + 254 + 10 = 266 and 1000 (266 + 734: 2 x 254 + 226) */
	static const unsigned char good[] = { 0, 0, 0, 2, 1, 10, 1, 1, 226, 0 };
	for (k = 1; k <= 10; k++) {
		memset(img, 0, sizeof(img));
		img[5] = 7;
		ok &= feed(img, sizeof(img), good, sizeof(good), k) == 2;
		base = (uint32_t)(uintptr_t)img;
		ok &= be32(img + 2) == base + 7 && be32(img + 266) == base && be32(img + 1000) == base;
		ok &= be32(img + 6) == 0 && img[1] == 0;
	}
	check(ok, "a table fed a byte at a time and in bigger pieces");
	memcpy(tab, "\0\0\0\0", 4);
	check(feed(img, sizeof(img), tab, 4, 4) == 2, "no fixups (a first offset of 0)");
	memcpy(tab, "\0\0\0\2\3\0", 6);
	check(feed(img, sizeof(img), tab, 6, 6) == 0, "an odd step refused");
	memcpy(tab, "\0\0\x03\xfe\0", 5);
	check(feed(img, sizeof(img), tab, 5, 5) == 0, "a fixup past the image refused");
	memcpy(tab, "\0\0\0\3\0", 5);
	check(feed(img, sizeof(img), tab, 5, 5) == 0, "an odd first offset refused");
	memcpy(tab, "\0\0\0\2\2\2", 6);
	check(feed(img, sizeof(img), tab, 6, 6) == 1, "a table without its end wants more");
}

static void damaged(const char *path)
{
	long n, taken;
	unsigned char *file = slurp(path, &n);
	char err[120];
	FILE *f;
	void *p;
	p = ovl_load("build/none.ovl", &taken, err, sizeof(err));
	check(!p && strstr(err, "none.ovl is missing"), err);
	/* cut short in the text */
	f = fopen("build/short.ovl", "wb");
	fwrite(file, 1, (size_t)(n / 2), f);
	fclose(f);
	p = ovl_load("build/short.ovl", &taken, err, sizeof(err));
	check(!p && strstr(err, "damaged"), "a file cut short refused");
	/* cut short in the fixups */
	f = fopen("build/fixshort.ovl", "wb");
	fwrite(file, 1, (size_t)(n - 3), f);
	fclose(f);
	p = ovl_load("build/fixshort.ovl", &taken, err, sizeof(err));
	check(!p && strstr(err, "damaged"), "fixups cut short refused");
	/* not a program file */
	file[0] = 'X';
	f = fopen("build/bad.ovl", "wb");
	fwrite(file, 1, (size_t)n, f);
	fclose(f);
	p = ovl_load("build/bad.ovl", &taken, err, sizeof(err));
	check(!p && strstr(err, "damaged"), "a file that isn't one refused");
	free(file);
}

/* a program file: 64 bytes of text with a fixup at 4, 8 of BSS, the
   fixups' table (its first offset, steps of 254 (1) to make it ones bytes
   long, its end), then ntail bytes */
static void write_ovl(const char *path, long ones, const unsigned char *tl, long ntail)
{
	unsigned char hdr[28], text[64];
	FILE *f = fopen(path, "wb");
	long i;
	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 0x60;
	hdr[1] = 0x1A;
	hdr[5] = 64;		/* text */
	hdr[13] = 8;		/* BSS */
	memset(text, 0, sizeof(text));
	text[7] = 0x10;		/* the long at 4: 0x10, moved by where it lands */
	fwrite(hdr, 1, sizeof(hdr), f);
	fwrite(text, 1, sizeof(text), f);
	fwrite("\0\0\0\4", 1, 4, f);
	for (i = 0; i < ones; i++)
		fputc(1, f);
	fputc(0, f);
	fwrite(tl, 1, (size_t)ntail, f);
	fclose(f);
}

struct got {
	unsigned char buf[4096];
	long n;
	int ends, pieces, refuse;
	unsigned char *base;
};

static int tail(void *ud, unsigned char *base, const unsigned char *p, long n)
{
	struct got *g = ud;
	g->base = base;
	if (!n) {
		g->ends++;
		return 1;
	}
	if (g->n + n > (long)sizeof(g->buf))
		return 0;
	memcpy(g->buf + g->n, p, (size_t)n);
	g->n += n;
	g->pieces++;
	return g->refuse ? 0 : 1;
}

static void tails(void)
{
	unsigned char tl[1500];
	struct got g;
	long taken, i;
	char err[120];
	unsigned char *img;
	int later, ok;
	for (i = 0; i < (long)sizeof(tl); i++)
		tl[i] = (unsigned char)(i * 7 + 3);
	/* the table ends inside the first piece read (5 bytes of 512) */
	write_ovl("build/tail.ovl", 0, tl, sizeof(tl));
	memset(&g, 0, sizeof(g));
	img = ovl_load_tail("build/tail.ovl", &taken, err, sizeof(err), tail, &g, &later);
	check(img && g.n == (long)sizeof(tl) && !memcmp(g.buf, tl, sizeof(tl)) && g.ends == 1 && g.base == img,
	      "the bytes after the fixups handed out whole, in order, then the end");
	check(img && be32(img + 4) == 0x10 + (uint32_t)(uintptr_t)img && taken == 72 && !img[64] && !img[71],
	      "the image relocated and its BSS zeroed as by ovl_load");
	check(g.pieces > 1, "in pieces as the file is read");
	free(img);
	/* the table ends with the first piece (4 + 507 + 1 bytes) */
	write_ovl("build/tail2.ovl", 507, tl, sizeof(tl));
	memset(&g, 0, sizeof(g));
	img = ovl_load_tail("build/tail2.ovl", &taken, err, sizeof(err), tail, &g, &later);
	check(img && g.n == (long)sizeof(tl) && !memcmp(g.buf, tl, sizeof(tl)) && g.ends == 1,
	      "a table ending with a piece: the tail from the next");
	free(img);
	/* no tail at all */
	write_ovl("build/tail3.ovl", 0, tl, 0);
	memset(&g, 0, sizeof(g));
	img = ovl_load_tail("build/tail3.ovl", &taken, err, sizeof(err), tail, &g, &later);
	check(img && g.n == 0 && g.ends == 1, "no bytes after the table: only the end");
	free(img);
	/* ovl_load leaves a tail alone */
	img = ovl_load("build/tail.ovl", &taken, err, sizeof(err));
	check(img && be32(img + 4) == 0x10 + (uint32_t)(uintptr_t)img, "ovl_load loads a file with a tail");
	free(img);
	/* a tail refused: damaged, for good */
	memset(&g, 0, sizeof(g));
	g.refuse = 1;
	later = -1;
	img = ovl_load_tail("build/tail.ovl", &taken, err, sizeof(err), tail, &g, &later);
	check(!img && strstr(err, "damaged") && later == 0 && !fh[0], "a tail refused: damaged, for good, closed");
	later = -1;
	img = ovl_load_tail("build/none.ovl", &taken, err, sizeof(err), tail, &g, &later);
	check(!img && strstr(err, "missing") && later == 0, "a missing file: for good");
	/* more than any overlay: refused as damaged before any memory is asked */
	ok = 1;
	{
		FILE *f = fopen("build/huge.ovl", "wb");
		unsigned char hdr[28];
		memset(hdr, 0, sizeof(hdr));
		hdr[0] = 0x60;
		hdr[1] = 0x1A;
		hdr[2] = 0x7f;		/* 2 GB of text */
		fwrite(hdr, 1, sizeof(hdr), f);
		fclose(f);
		later = -1;
		img = ovl_load_tail("build/huge.ovl", &taken, err, sizeof(err), tail, &g, &later);
		ok = !img && strstr(err, "damaged") && later == 0;
	}
	check(ok, "a header bigger than any overlay: damaged");
	ovl_flush();		/* (nothing here: the Atari's caches) */
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: test_ovl TLS.OVL\n");
		return 2;
	}
	real(argv[1]);
	made();
	damaged(argv[1]);
	tails();
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
