/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * DITHBENCH.PRG: pic.c's dither on the CPU (pic_dither_row, the 68000's
 * and the 68030's: no DSP), timed for run.py, which runs it built from two
 * pic.c's (before and after a change) on one of the family's machines. A
 * 192 x 96 picture of colours across the cube, light and dark (each pixel
 * different from its neighbours, as a photo's), dithered to 2 pens
 * (black and white), 4 (ST medium's), 16 (ST low's) and 256 (a Falcon's:
 * the system's 16, a 6-level cube, greys), each 3 times, at the share
 * pic_pal_soft gives (16, then 12 and 8 where the build has it), on the
 * 200 Hz clock, the pens' sum after. Lines go to the emulator's console
 * as "DTB ...", the last "DTB done".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "plat.h"
#include "pic.h"
#include "util.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

unsigned long pf_ms(void) { return tos_ms(); }

#define W 192
#define H 96

static unsigned short px[W * H];
static unsigned char out[W];
static unsigned char rgb[256][3];
static PIC_PAL pal;
static char line[160];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

static void palette(int n)
{
	static const unsigned char st16[16][3] = {
		{ 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
		{ 0, 255, 255 }, { 170, 170, 170 }, { 85, 85, 85 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 170, 0 },
		{ 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 0, 0, 0 }
	};
	int k;
	if (n == 2) {
		memset(rgb, 0, sizeof(rgb));
		rgb[0][0] = rgb[0][1] = rgb[0][2] = 255;
	} else if (n == 4) {
		static const unsigned char med[4][3] = { { 255, 255, 255 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 0 } };
		memcpy(rgb, med, sizeof(med));
	} else {
		memcpy(rgb, st16, sizeof(st16));
		for (k = 16; k < n && k < 232; k++) {
			rgb[k][0] = (unsigned char)((k - 16) / 36 * 51);
			rgb[k][1] = (unsigned char)((k - 16) / 6 % 6 * 51);
			rgb[k][2] = (unsigned char)((k - 16) % 6 * 51);
		}
		for (; k < n; k++)
			rgb[k][0] = rgb[k][1] = rgb[k][2] = (unsigned char)((k - 232) * 11);
	}
	pic_pal_init(&pal, n, (const unsigned char (*)[3])rgb);
}

/* one dither of the picture: its time, the pens' sum to *sum */
static unsigned long once(unsigned long *sum)
{
	PIC_DITHER *dt = pic_dither_open(W, &pal);
	unsigned long t = tos_ms(), s = 0;
	int y, x;
	for (y = 0; y < H; y++) {
		pic_dither_row(dt, px + y * W, 0, out);
		for (x = 0; x < W; x++)
			s = s * 31 + out[x];
	}
	t = tos_ms() - t;
	pic_dither_close(dt);
	*sum = s;
	return t;
}

int main(void)
{
	static const int pens[4] = { 2, 4, 16, 256 };
	int x, y, i, j, k;
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			int r = (x * 31 / (W - 1) + (y & 3)) & 31, g = (y * 63 / (H - 1) + (x % 5)) & 63,
			    b = ((x + y) * 31 / (W + H - 2) + (x * y) % 3) & 31;
			px[y * W + x] = (unsigned short)(r << 11 | g << 5 | b);
		}
	snprintf(line, sizeof(line), "DTB %dx%d, %s", W, H, DTB_NAME);
	say(line);
	for (i = 0; i < 4; i++) {
#ifdef DTB_SOFT
		static const int shares[3] = { 16, 12, 8 };
		int nshares = 3;
#else
		static const int shares[1] = { 16 };
		int nshares = 1;
#endif
		for (j = 0; j < nshares; j++) {
			unsigned long t[3], sum = 0;
			palette(pens[i]);
#ifdef DTB_SOFT
			pic_pal_soft(&pal, shares[j]);
#endif
			for (k = 0; k < 3; k++)
				t[k] = once(&sum);
			snprintf(line, sizeof(line), "DTB %d pens share %d: %lu %lu %lu ms, sum %08lx", pens[i], shares[j],
				 t[0], t[1], t[2], sum);
			say(line);
		}
	}
	say("DTB done");
	return 0;
}
