/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* aboutpen.h's nearest pen against the plain search it replaces (every candidate's distance by
   32-bit multiplies, the first of equals): the same pen over many palettes, colours and
   lists, ties included (palettes of few levels) */
#include <stdio.h>
#include <stdlib.h>
#include "aboutpen.h"

static short ref_nearest(const short (*c)[3], const short *pens, short n, const short *want)
{
	long best = -1;
	short i, pen = 0;
	for (i = 0; i < n; i++) {
		long d = 0, e;
		short k;
		for (k = 0; k < 3; k++)
			e = c[i][k] - want[k], d += e * e;
		if (best < 0 || d < best)
			best = d, pen = pens ? pens[i] : i;
	}
	return pen;
}

int main(void)
{
	long checks = 0, bad = 0;
	int round, k;
	srand(20261111);
	for (k = 0; k <= 1733; k += 37) {		/* the square root */
		long d;
		for (d = (long)k * k - 1; d <= (long)k * k + 1; d++) {
			short r = about_isqrt(d < 0 ? 0 : d), j;
			long dd = d < 0 ? 0 : d;
			for (j = 0; j < 1; j++)
				if ((long)r * r > dd || (long)(r + 1) * (r + 1) <= dd)
					bad++, printf("isqrt(%ld) = %d\n", dd, r);
			checks++;
		}
	}
	for (round = 0; round < 20000; round++) {
		short c[256][3], pens[256], want[3], n = (short)(1 + rand() % 256), i;
		int levels = round % 4 == 0 ? 3 : round % 4 == 1 ? 16 : round % 4 == 2 ? 251 : 1001;
		for (i = 0; i < n; i++) {
			for (k = 0; k < 3; k++)
				c[i][k] = (short)(levels == 1001 ? rand() % 1001 : (rand() % levels) * (1000 / (levels > 1 ? levels - 1 : 1)));
			pens[i] = (short)(rand() % 256);
		}
		for (k = 0; k < 3; k++)
			want[k] = (short)(levels == 1001 ? rand() % 1001 : (rand() % levels) * (1000 / (levels - 1)));
		{
			short a = ref_nearest((const short (*)[3])c, 0, n, want), b = about_nearest_pen((const short (*)[3])c, 0, n, want);
			short a2 = ref_nearest((const short (*)[3])c, pens, n, want), b2 = about_nearest_pen((const short (*)[3])c, pens, n, want);
			checks += 2;
			if (a != b || a2 != b2)
				bad++, printf("round %d: %d/%d, %d/%d\n", round, a, b, a2, b2);
		}
	}
	printf("%ld checks, %ld bad\n", checks, bad);
	return bad != 0;
}
