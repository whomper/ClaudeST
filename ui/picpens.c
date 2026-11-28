/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * picpens.c - a picture's own colours in pens 192-255 (picpens.h).
 */
#ifdef PIC_OVL_BUILD
/* in PIC.OVL: the program's VDI through PIC_HOST (head.c); the program
   checked the planes */
void vs_color(short h, short index, const short *rgb);
void vq_color(short h, short index, short *rgb);
#else
#include "gem.h"

extern short planes;	/* gemui.h's: the screen's planes */
#endif
#include "picpens.h"

static short saved[PICPENS_MAX][3];
static short held;		/* pens set from 192 on */

int picpens_take(short vdi, int n, const short rgb[][3])
{
	short i;
#ifndef PIC_OVL_BUILD
	if (planes != 8)
		return 0;
#endif
	if (n < 1 || n > PICPENS_MAX)
		return 0;
	for (i = held; i < n; i++)
		vq_color(vdi, (short)(PICPENS_FIRST + i), saved[i]);
	for (i = 0; i < n; i++)
		vs_color(vdi, (short)(PICPENS_FIRST + i), rgb[i]);
	/* fewer than held last time: the rest back as they were */
	for (i = (short)n; i < held; i++)
		vs_color(vdi, (short)(PICPENS_FIRST + i), saved[i]);
	held = (short)n;
	return 1;
}

void picpens_give(short vdi)
{
	short i;
	for (i = 0; i < held; i++)
		vs_color(vdi, (short)(PICPENS_FIRST + i), saved[i]);
	held = 0;
}

int picpens_held(void)
{
	return held;
}

#ifndef PIC_OVL_BUILD
int picpens_on_top(short wh)
{
	short top = -1, b, c, d;
	return wh >= 0 && wind_get(0, WF_TOP, &top, &b, &c, &d) && top == wh;
}
#endif
