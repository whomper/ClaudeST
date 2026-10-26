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
 *   test_pic -X FILE...       (test_pic_dsp) each picture dithered on the
 *                             DSP and on the 68030, to 4, 16 and 256
 *                             colours, at its size and at 61 x 37: the
 *                             same pens to the bit; prints how many
 *   test_pic -r FILE...       robustness: each file cut short at many
 *                             lengths and with bytes changed, decoded at
 *                             three sizes (AddressSanitizer and UBSan
 *                             watch); prints how many decoded
 *
 * Built with WITH_DSP_SIM (test_pic_dsp), libjpeg-turbo's JPEGs go to
 * dsp/jpeg.a56 on Hatari's DSP core (dsp_sim.c); PIC_DSP_CYCLES=1 prints
 * its cycles and the words each way to stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"

#ifdef WITH_DSP_SIM
const PIC_DSP *sim_dsp(void);
extern unsigned long long sim_cycles, sim_pc[1536];
extern unsigned long sim_in, sim_out;
#endif

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

/* -m's two pens: black and white, the order a monochrome screen has */
static const unsigned char bw[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };

/* -D's 256 colours */
static unsigned char pal256[256][3];

static void make256(void)
{
	int k;
	memcpy(pal256, vga16, sizeof vga16);
	for (k = 0; k < 216; k++) {
		pal256[16 + k][0] = (unsigned char) (k / 36 * 51);
		pal256[16 + k][1] = (unsigned char) (k / 6 % 6 * 51);
		pal256[16 + k][2] = (unsigned char) (k % 6 * 51);
	}
	for (k = 232; k < 256; k++)
		pal256[k][0] = pal256[k][1] = pal256[k][2] = (unsigned char) ((k - 232) * 11);
}

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
		if (dither == 3) {
			pic_pal_init(&pal, 2, bw);
		} else if (dither == 2) {
			make256();
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
			memcpy(px, dither == 3 ? bw[pens[i]] : dither == 2 ? pal256[pens[i]] : vga16[pens[i]], 3);
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

/* pic_poll for -s: counts its calls; 0 at call stop_at (0: never) */
static long polls, stop_at;

static int count_poll(void)
{
	return ++polls != stop_at;
}

/* in slices: each picture decoded and dithered with turns taken comes
   out as without them; turns come every 16 rows or fewer (a picture of
   48 rows or more has some), and a turn that says stop stops the decode
   (PIC_STOPPED) and the dither */
static int slices(int argc, char **argv)
{
	static PIC_PAL pal;
	long tried = 0, bad = 0, stopped = 0;
	int a, k;
#ifdef WITH_DSP_SIM
	pic_dsp_dither = 0;	/* the 68030's (the DSP's takes no turns: -X compares it) */
#endif
	make256();
	for (a = 0; a < argc; a++) {
		long n;
		unsigned char *d = load(argv[a], &n);
		if (d == NULL)
			continue;
		for (k = 0; k < 2; k++) {
			int why, w = k ? 61 : 0, h = k ? 37 : 0;
			PIC *p, *q;
			size_t np;
			unsigned char *x, *y;
			long dp;
			pic_poll = NULL;
			p = pic_decode(d, n, w, h, 4L << 20, &why);
			if (p == NULL)
				continue;
			polls = 0;
			stop_at = 0;
			pic_poll = count_poll;
			q = pic_decode(d, n, w, h, 4L << 20, &why);
			tried++;
			np = (size_t) p->w * p->h;
			if (q == NULL || q->w != p->w || q->h != p->h || memcmp(p->px, q->px, np * 2) != 0 ||
			    (p->alpha == NULL) != (q->alpha == NULL) ||
			    (p->alpha != NULL && memcmp(p->alpha, q->alpha, np) != 0)) {
				printf("FAIL %s %dx%d: not the same with turns\n", argv[a], w, h);
				bad++;
			}
			pic_free(q);
			pic_pal_init(&pal, 256, (const unsigned char (*)[3]) pal256);
			x = malloc(np);
			y = malloc(np);
			pic_poll = NULL;
			pic_dither(p, &pal, x);
			polls = 0;
			pic_poll = count_poll;
			if (!pic_dither(p, &pal, y) || memcmp(x, y, np) != 0) {
				printf("FAIL %s %dx%d: dithered not the same with turns\n", argv[a], p->w, p->h);
				bad++;
			}
			dp = polls;
			if (p->h >= 48 && dp == 0) {
				printf("FAIL %s %dx%d: no turn while dithering\n", argv[a], p->w, p->h);
				bad++;
			}
			if (p->h >= 48) {
				/* a turn that says stop */
				polls = 0;
				stop_at = 2;
				q = pic_decode(d, n, w, h, 4L << 20, &why);
				if (q != NULL || why != PIC_STOPPED) {
					printf("FAIL %s %dx%d: not stopped (%s)\n", argv[a], w, h,
					       q != NULL ? "decoded" : pic_why_text(why));
					bad++;
				} else {
					stopped++;
				}
				pic_free(q);
				polls = 0;
				if (pic_dither(p, &pal, y) != 0 || polls != 2) {
					printf("FAIL %s %dx%d: dither not stopped\n", argv[a], p->w, p->h);
					bad++;
				}
			}
			pic_poll = NULL;
			free(x);
			free(y);
			pic_free(p);
		}
		free(d);
	}
	pic_pal_free(&pal);
#ifdef WITH_DSP_SIM
	pic_dsp_dither = 1;
#endif
	printf("in slices: %ld pictures, %ld stopped, %ld failed\n", tried, stopped, bad);
	return bad != 0;
}

#ifdef WITH_DSP_SIM
static int dsame(int argc, char **argv)
{
	static const unsigned char four[4][3] = { { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 0 } };
	static const int sizes[2][2] = { { 0, 0 }, { 61, 37 } };
	static PIC_PAL pals[3];
	long tried = 0, differ = 0;
	int a, s, k;
	make256();
	pic_pal_init(&pals[0], 4, four);
	pic_pal_init(&pals[1], 16, vga16);
	pic_pal_init(&pals[2], 256, (const unsigned char (*)[3]) pal256);
	for (a = 0; a < argc; a++) {
		long n;
		unsigned char *d = load(argv[a], &n);
		if (d == NULL)
			continue;
		for (s = 0; s < 2; s++) {
			int why;
			PIC *p = pic_decode(d, n, sizes[s][0], sizes[s][1], 4L << 20, &why);
			if (p == NULL)
				continue;
			for (k = 0; k < 3; k++) {
				size_t np = (size_t) p->w * p->h;
				unsigned char *x = malloc(np), *y = malloc(np);
				pic_dsp_dither = 1;
				pic_dither(p, &pals[k], x);
				pic_dsp_dither = 0;
				pic_dither(p, &pals[k], y);
				pic_dsp_dither = 1;
				tried++;
				if (memcmp(x, y, np) != 0) {
					size_t i;
					for (i = 0; i < np && x[i] == y[i]; i++)
						;
					differ++;
					printf("FAIL %s %dx%d %d colours: pixel %ld,%ld: %d, the 68030's %d\n", argv[a], p->w,
					       p->h, pals[k].n, (long) (i % p->w), (long) (i / p->w), x[i], y[i]);
				}
				free(x);
				free(y);
			}
			pic_free(p);
		}
		free(d);
	}
	printf("dithered on the DSP: %ld, %ld differ (%s)\n", tried, differ, pic_dsp_state());
	return differ != 0;
}
#endif

static int run(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "-r") == 0)
		return robust(argc - 2, argv + 2);
	if (argc >= 2 && strcmp(argv[1], "-s") == 0)
		return slices(argc - 2, argv + 2);
#ifdef WITH_DSP_SIM
	if (argc >= 2 && strcmp(argv[1], "-X") == 0)
		return dsame(argc - 2, argv + 2);
#endif
	if (argc == 6 && (strcmp(argv[1], "-d") == 0 || strcmp(argv[1], "-D") == 0 || strcmp(argv[1], "-m") == 0))
		return one(argv[2], atoi(argv[3]), atoi(argv[4]), argv[5], argv[1][1] == 'D' ? 2 : argv[1][1] == 'm' ? 3 : 1);
	if (argc == 5)
		return one(argv[1], atoi(argv[2]), atoi(argv[3]), argv[4], 0);
	fprintf(stderr, "test_pic FILE W H OUT | -d|-D|-m FILE W H OUT | -r FILE... | -s FILE...\n");
	return 2;
}

int main(int argc, char **argv)
{
	int r;
#ifdef WITH_DSP_SIM
	pic_dsp = sim_dsp();
	if (getenv("PIC_DSP_SHRINK"))		/* 0: the scaler shrinks, as before */
		pic_dsp_shrink = atoi(getenv("PIC_DSP_SHRINK"));
#endif
	r = run(argc, argv);
#ifdef WITH_DSP_SIM
	if (getenv("PIC_DSP_CYCLES"))
		fprintf(stderr, "dsp: %s, %llu cycles, %lu words in, %lu out\n", pic_dsp_state(), sim_cycles, sim_in,
			sim_out);
	if (getenv("PIC_DSP_PROFILE")) {
		/* each address's cycles, for tools to name from a56's labels */
		FILE *f = fopen(getenv("PIC_DSP_PROFILE"), "w");
		int a;
		for (a = 0; f != NULL && a < 1536; a++)
			if (sim_pc[a])
				fprintf(f, "%04x %llu\n", a < 512 ? a : 0x7000 + a - 512, sim_pc[a]);
		if (f != NULL)
			fclose(f);
	}
#endif
	return r;
}
