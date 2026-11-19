/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_scrpens.c - ui/scrpens.c on the build machine: the pen to register
 * map against TOS's table (2, 4 and 16 colours; 256's first 16 and its
 * pens 1 and 255), each map a permutation, and scr_pens against a stand-in
 * VDI whose pen p answers a colour of its own: each lands at its pen's
 * register; mono white and black without a VDI call; past 8 planes none.
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "scrpens.h"

static int calls, fails;

/* pen p: red p*3 (0-1000 scale: p*3*1000/255 rounds back), green 1000 - .., blue 500 */
void vq_color(short h, short index, short *rgb)
{
	(void)h;
	calls++;
	rgb[0] = (short)((index % 85) * 3 * 1000L / 255 + 1);
	rgb[1] = (short)(1000 - (index % 85) * 3 * 1000L / 255);
	rgb[2] = 500;
}

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
	/* TOS's VDI pen -> hardware register (the VDI manual's table) */
	static const int tos16[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
	static const int tos4[4] = { 0, 3, 1, 2 };
	unsigned char rgb[256][3], seen[256];
	int planes, p, n;

	for (p = 0; p < 2; p++)
		CHECK(scr_pen_reg(p, 1) == p, "mono pen %d", p);
	for (p = 0; p < 4; p++)
		CHECK(scr_pen_reg(p, 2) == tos4[p], "4 colours: pen %d -> %d", p, scr_pen_reg(p, 2));
	for (p = 0; p < 16; p++)
		CHECK(scr_pen_reg(p, 4) == tos16[p], "16 colours: pen %d -> %d", p, scr_pen_reg(p, 4));
	for (p = 0; p < 16; p++)
		CHECK(scr_pen_reg(p, 8) == (p == 1 ? 255 : tos16[p]), "256 colours: pen %d -> %d", p, scr_pen_reg(p, 8));
	CHECK(scr_pen_reg(255, 8) == 15, "256 colours: pen 255 -> %d", scr_pen_reg(255, 8));
	for (p = 16; p < 255; p++)
		CHECK(scr_pen_reg(p, 8) == p, "256 colours: pen %d -> %d", p, scr_pen_reg(p, 8));
	/* each a permutation of the registers */
	for (planes = 1; planes <= 8; planes *= 2) {
		memset(seen, 0, sizeof seen);
		for (p = 0; p < 1 << planes; p++)
			seen[scr_pen_reg(p, planes)]++;
		for (p = 0; p < 1 << planes; p++)
			CHECK(seen[p] == 1, "%d planes: register %d named %d times", planes, p, seen[p]);
	}
	/* the colours by register */
	for (planes = 2; planes <= 8; planes *= 2) {
		calls = 0;
		memset(rgb, 0xAA, sizeof rgb);
		n = scr_pens(1, planes, rgb);
		CHECK(n == 1 << planes, "%d planes: %d registers", planes, n);
		CHECK(calls == n, "%d planes: %d VDI calls", planes, calls);
		for (p = 0; p < n; p++) {
			int r = scr_pen_reg(p, planes), k = p % 85;

			CHECK(rgb[r][0] == k * 3 && rgb[r][1] == 255 - k * 3 && rgb[r][2] == 127,
			      "%d planes: pen %d at register %d is %d %d %d", planes, p, r, rgb[r][0], rgb[r][1], rgb[r][2]);
		}
		for (p = n; p < 256; p++)
			CHECK(!rgb[p][0] && !rgb[p][1] && !rgb[p][2], "%d planes: register %d past the count not cleared", planes, p);
	}
	calls = 0;
	n = scr_pens(1, 1, rgb);
	CHECK(n == 2 && !calls, "mono: %d registers, %d VDI calls", n, calls);
	CHECK(rgb[0][0] == 255 && rgb[0][1] == 255 && rgb[0][2] == 255 && !rgb[1][0] && !rgb[1][1] && !rgb[1][2],
	      "mono: white then black");
	memset(rgb, 0xAA, sizeof rgb);
	CHECK(scr_pens(1, 16, rgb) == 0 && rgb[0][0] == 0xAA, "16 planes: none, rgb untouched");
	printf("%s\n", fails ? "FAILED" : "ok   scrpens: 2, 4, 16 and 256 colours by TOS's table, mono, past 8 planes");
	return fails != 0;
}
