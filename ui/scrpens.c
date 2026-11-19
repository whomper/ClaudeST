/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * scrpens.c - the screen's colours by hardware register (scrpens.h)
 */
#include <string.h>
#include "gem.h"
#include "scrpens.h"

int scr_pen_reg(int pen, int planes)
{
	static const unsigned char r4[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
	static const unsigned char r2[4] = { 0, 3, 1, 2 };

	if (planes == 1)
		return pen;
	if (planes == 2)
		return r2[pen & 3];
	if (planes == 4)
		return r4[pen & 15];
	if (pen < 16)
		return pen == 1 ? 255 : r4[pen];
	return pen == 255 ? 15 : pen;
}

int scr_pens(short vdi, int planes, unsigned char rgb[256][3])
{
	int n, pen;

	if (planes > 8)
		return 0;
	n = 1 << planes;
	memset(rgb, 0, 256 * 3);
	if (planes == 1) {
		rgb[0][0] = rgb[0][1] = rgb[0][2] = 255;
		return 2;
	}
	for (pen = 0; pen < n; pen++) {
		short c[3];
		int r = scr_pen_reg(pen, planes);

		vq_color(vdi, pen, c);
		rgb[r][0] = (unsigned char)(c[0] * 255L / 1000);
		rgb[r][1] = (unsigned char)(c[1] * 255L / 1000);
		rgb[r][2] = (unsigned char)(c[2] * 255L / 1000);
	}
	return n;
}
