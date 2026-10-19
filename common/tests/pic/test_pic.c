/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_pic.c - the pictures (src/pic.c) on the build machine, for run.py:
 *
 *   test_pic FILE W H OUT     decoded at W x H (0 0: its own size) into
 *                             OUT: "w h how scale\n", then R G B A bytes
 *                             ("-": nothing written, for counting the
 *                             decoder's instructions under QEMU)
 *   test_pic -d FILE W H OUT  the same, then dithered to the 16 colours
 *                             of the Falcon's standard palette: OUT gets
 *                             each pixel's pen colour (-D: to 256, a 6x6x6
 *                             cube, greys and the 16)
 *   test_pic -r FILE...       robustness: each file cut short at many
 *                             lengths and with bytes changed, decoded at
 *                             three sizes (AddressSanitizer and UBSan
 *                             watch); prints how many decoded
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"

static unsigned char *load(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *d;
	if (f == NULL)
		return NULL;
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc((size_t) *n + 1);
	if (d != NULL && fread(d, 1, (size_t) *n, f) != (size_t) *n) {
		free(d);
		d = NULL;
	}
	fclose(f);
	return d;
}

static const unsigned char vga16[16][3] = {
	{ 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
	{ 0, 255, 255 }, { 170, 170, 170 }, { 85, 85, 85 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 170, 0 },
	{ 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 0, 0, 0 }
};

/* -D's 256 colours */
static unsigned char pal256[256][3];

static int one(const char *file, int w, int h, const char *out, int dither)
{
	long n, i;
	int why;
	unsigned char *d = load(file, &n), *pens = NULL;
	PIC *p;
	FILE *f;
	if (d == NULL) {
		fprintf(stderr, "%s: can't read\n", file);
		return 2;
	}
	p = pic_decode(d, n, w, h, 16L << 20, &why);
	free(d);
	if (p == NULL) {
		printf("%s: %s\n", file, pic_why_text(why));
		return 1;
	}
	if (dither) {
		static PIC_PAL pal;
		int k;
		if (dither == 2) {
			memcpy(pal256, vga16, sizeof vga16);
			for (k = 0; k < 216; k++) {
				pal256[16 + k][0] = (unsigned char) (k / 36 * 51);
				pal256[16 + k][1] = (unsigned char) (k / 6 % 6 * 51);
				pal256[16 + k][2] = (unsigned char) (k % 6 * 51);
			}
			for (k = 232; k < 256; k++)
				pal256[k][0] = pal256[k][1] = pal256[k][2] = (unsigned char) ((k - 232) * 11);
			pic_pal_init(&pal, 256, (const unsigned char (*)[3]) pal256);
		} else {
			pic_pal_init(&pal, 16, vga16);
		}
		pens = malloc((size_t) p->w * p->h);
		pic_dither(p, &pal, pens);
	}
	if (strcmp(out, "-") == 0) {
		free(pens);
		pic_free(p);
		return 0;
	}
	f = fopen(out, "wb");
	fprintf(f, "%d %d %d %d\n", p->w, p->h, p->how, p->scale);
	for (i = 0; i < (long) p->w * p->h; i++) {
		unsigned v = p->px[i];
		unsigned char px[4];
		if (pens != NULL) {
			memcpy(px, dither == 2 ? pal256[pens[i]] : vga16[pens[i]], 3);
		} else {
			/* 5-6-5 back to 8 bits a component, the top bits repeated */
			px[0] = (unsigned char) (((v >> 8) & 0xF8) | (v >> 13));
			px[1] = (unsigned char) (((v >> 3) & 0xFC) | ((v >> 9) & 3));
			px[2] = (unsigned char) (((v << 3) & 0xF8) | ((v >> 2) & 7));
		}
		px[3] = p->alpha != NULL ? p->alpha[i] : 255;
		fwrite(px, 1, 4, f);
	}
	fclose(f);
	free(pens);
	pic_free(p);
	return 0;
}

static int robust(int argc, char **argv)
{
	static const int sizes[3][2] = { { 0, 0 }, { 13, 7 }, { 300, 200 } };
	long tried = 0, decoded = 0;
	int a;
	for (a = 0; a < argc; a++) {
		long n, cut, k;
		unsigned char *d = load(argv[a], &n);
		if (d == NULL)
			continue;
		for (cut = 0; cut <= n; cut += cut < 64 ? 1 : n / 40 + 1) {
			for (k = 0; k < 3; k++) {
				PIC *p = pic_decode(d, cut, sizes[k][0], sizes[k][1], 4L << 20, NULL);
				tried++;
				if (p != NULL)
					decoded++;
				pic_free(p);
			}
		}
		/* bytes changed: a few at a time, from a fixed seed */
		srand(1);
		for (k = 0; k < 60; k++) {
			unsigned char *e = malloc((size_t) n);
			int j;
			PIC *p;
			memcpy(e, d, (size_t) n);
			for (j = 0; j < 4; j++)
				e[rand() % n] = (unsigned char) rand();
			p = pic_decode(e, n, sizes[k % 3][0], sizes[k % 3][1], 4L << 20, NULL);
			tried++;
			if (p != NULL)
				decoded++;
			pic_free(p);
			free(e);
		}
		free(d);
	}
	printf("robust: %ld tries, %ld decoded\n", tried, decoded);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "-r") == 0)
		return robust(argc - 2, argv + 2);
	if (argc == 6 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "-D") == 0))
		return one(argv[2], atoi(argv[3]), atoi(argv[4]), argv[5], argv[1][1] == 'D' ? 2 : 1);
	if (argc == 5)
		return one(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4], 0);
	fprintf(stderr, "test_pic FILE W H OUT | -d|-D FILE W H OUT | -r FILE...\n");
	return 2;
}
