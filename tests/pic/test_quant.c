/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_quant.c - quant.c against a 256-colour screen's palette: each
   picture at 64 x 64, dithered (pic.c) to NVDI's palette as it is, and
   with pens 192-255 the picture's own 64 (quant_565); the error after a
   3 x 3 blur (what the eye sees of a dither) for each, and PPMs to look
   at in OUT. Fails when the picture's own pens don't lower the error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pic.h"
#include "quant.h"

#define S 64
static const unsigned char tos16[16][3] = {
	{255,255,255},{0,0,0},{255,0,0},{0,255,0},{0,0,255},{0,255,255},{255,255,0},{255,0,255},
	{204,204,204},{136,136,136},{136,0,0},{0,136,0},{0,0,136},{0,136,136},{136,136,0},{136,0,136}};
static const int lv[6] = { 0, 48, 101, 154, 207, 255 };

static void nvdi(unsigned char rgb[256][3])
{
	int i, r, g, b;
	memcpy(rgb, tos16, sizeof tos16);
	i = 16;
	for (r = 0; r < 6; r++) for (g = 0; g < 6; g++) for (b = 0; b < 6; b++, i++) {
		rgb[i][0] = lv[r]; rgb[i][1] = lv[g]; rgb[i][2] = lv[b];
	}
	for (; i < 256; i++)
		rgb[i][0] = rgb[i][1] = rgb[i][2] = (unsigned char)((i - 231) * 10);
}

static void c8(unsigned short v, int c[3])
{
	c[0] = (v >> 11) * 255 / 31; c[1] = ((v >> 5) & 63) * 255 / 63; c[2] = (v & 31) * 255 / 31;
}

static double blur_err(const PIC *p, const unsigned char *out, unsigned char rgb[256][3])
{
	double e = 0;
	int x, y, k, dx, dy;
	for (y = 1; y < S - 1; y++) for (x = 1; x < S - 1; x++) for (k = 0; k < 3; k++) {
		double a = 0, b = 0;
		for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
			int c[3], i = (y + dy) * S + x + dx;
			c8(p->px[i], c);
			a += c[k];
			b += rgb[out[i]][k];
		}
		e += (a - b) / 9 * (a - b) / 9;
	}
	return e / ((S - 2) * (S - 2) * 3);
}

static void ppm(const char *dir, const char *name, const char *tag, const unsigned char *out,
		unsigned char rgb[256][3])
{
	char path[512];
	FILE *f;
	int i;
	snprintf(path, sizeof path, "%s/%s-%s.ppm", dir, name, tag);
	if (!(f = fopen(path, "wb")))
		return;
	fprintf(f, "P6 %d %d 255\n", S, S);
	for (i = 0; i < S * S; i++)
		fwrite(rgb[out[i]], 1, 3, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	int a, bad = 0;
	const char *dir = argc > 1 ? argv[1] : ".";
	for (a = 2; a < argc; a++) {
		static unsigned char d[8 << 20], out0[S * S], out1[S * S], rgb[256][3];
		static PIC_PAL pal;
		short own[64][3];
		const char *name = strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a];
		FILE *f = fopen(argv[a], "rb");
		long n;
		int why, k, got;
		double e0, e1;
		PIC *p;
		if (!f)
			continue;
		n = (long)fread(d, 1, sizeof d, f);
		fclose(f);
		if (!(p = pic_decode(d, n, S, S, 1L << 24, &why)))
			continue;
		nvdi(rgb);
		pic_pal_init(&pal, 256, (const unsigned char (*)[3])rgb);
		pic_pal_soft(&pal, 8);	/* EContact's share in 256 colours */
		pic_dither(p, &pal, out0);
		e0 = blur_err(p, out0, rgb);
		got = quant_565(p->px, (long)S * S, -1, 64, own);
		for (k = 0; k < got; k++) {
			rgb[192 + k][0] = (unsigned char)((own[k][0] * 255 + 500) / 1000);
			rgb[192 + k][1] = (unsigned char)((own[k][1] * 255 + 500) / 1000);
			rgb[192 + k][2] = (unsigned char)((own[k][2] * 255 + 500) / 1000);
		}
		pic_pal_init(&pal, 256, (const unsigned char (*)[3])rgb);
		pic_pal_soft(&pal, 8);
		pic_dither(p, &pal, out1);
		e1 = blur_err(p, out1, rgb);
		printf("%-28s %2d colours  error %7.1f -> %7.1f\n", name, got, e0, e1);
		nvdi(rgb);
		ppm(dir, name, "nvdi", out0, rgb);
		for (k = 0; k < got; k++) {
			rgb[192 + k][0] = (unsigned char)((own[k][0] * 255 + 500) / 1000);
			rgb[192 + k][1] = (unsigned char)((own[k][1] * 255 + 500) / 1000);
			rgb[192 + k][2] = (unsigned char)((own[k][2] * 255 + 500) / 1000);
		}
		ppm(dir, name, "own64", out1, rgb);
		if (e1 > e0)
			bad++;
		pic_free(p);
	}
	printf("%s\n", bad ? "FAIL: the picture's own colours were worse" : "quant: better on every picture");
	return bad != 0;
}
