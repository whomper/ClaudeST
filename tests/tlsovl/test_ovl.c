/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_ovl.c - src/ovl.c on the build machine. With TLS.OVL: loaded at
 * wherever malloc put it, every long its fixups name moved by that much
 * and nothing else changed, the BSS zeroed, TLS_OVL's table at the start.
 * Then fixup tables made by hand: the first offset split across pieces,
 * the 254-byte steps, and damaged ones refused (an odd step, a fixup past
 * the data, a file cut short).
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

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: test_ovl TLS.OVL\n");
		return 2;
	}
	real(argv[1]);
	made();
	damaged(argv[1]);
	printf("%s\n", fails ? "FAILED" : "all passed");
	return fails != 0;
}
