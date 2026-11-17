/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aboutpen.h - the nearest pen of a palette to a colour, for the About box's
 * 256-colour icon (aboutbox.c; header-only so that no app's Makefile has a
 * file more, and so that tests/about checks it on the host).
 *
 * The distance is the sum of the squares of the channels' differences (a
 * channel 0 to 1000); the first of equals wins. It is the same answer as
 * working every candidate out, found with no 32-bit multiply (a 68000
 * build's __mulsi3 was 90 of the 190 ms NVDI's 256-colour palette cost
 * the box on a stock Falcon, 11 Nov 2026) and with most candidates
 * dropped at their first channel: once a candidate is the best so far,
 * any whose channel differs by more than the square root of its distance
 * can't be nearer.
 */
#ifndef AERA_ABOUTPEN_H
#define AERA_ABOUTPEN_H

/* v * v, 16 x 16: muls.w on a 68000 (GCC took (long)mid * mid, mid a
   short, for a 32-bit __mulsi3: 5 of the 256-colour box's 195 ms on a
   stock Falcon under NVDI, 16 Nov 2026) */
static inline long about_sq(short v)
{
#if defined(__mc68000__) && !defined(__mcoldfire__)
	long r = v;
	__asm__("muls.w %1,%0" : "+d"(r) : "d"(v));
	return r;
#else
	return (long)v * v;
#endif
}

/* the whole part of the square root of d (0 to 3,000,000), no multiply wider than 16 x 16 */
static inline short about_isqrt(long d)
{
	short lo = 0, hi = 1733, mid;
	while (lo < hi) {
		mid = (short)((lo + hi + 1) >> 1);
		if (about_sq(mid) <= d)
			lo = mid;
		else
			hi = (short)(mid - 1);
	}
	return lo;
}

/* the nearest of n colours c[] to want (a pen's rgb, 0 to 1000 a channel):
   its pen, pens[i] where the colours are a list, else i */
static inline short about_nearest_pen(const short (*c)[3], const short *pens, short n, const short *want)
{
	long best = -1;
	short i, pen = 0, lim = 32767;
	for (i = 0; i < n; i++) {
		short e0 = (short)(c[i][0] - want[0]), e1, e2;
		long d;
		if (e0 < 0)
			e0 = (short)-e0;
		if (e0 > lim)
			continue;
		e1 = (short)(c[i][1] - want[1]);
		if (e1 < 0)
			e1 = (short)-e1;
		if (e1 > lim)
			continue;
		e2 = (short)(c[i][2] - want[2]);
		if (e2 < 0)
			e2 = (short)-e2;
		if (e2 > lim)
			continue;
		d = (long)e0 * e0 + (long)e1 * e1 + (long)e2 * e2;
		if (best < 0 || d < best) {
			best = d;
			pen = pens ? pens[i] : i;
			lim = about_isqrt(d);
		}
	}
	return pen;
}

#endif
