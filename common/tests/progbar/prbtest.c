/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * PRBTEST.PRG: the family's progress bar (ui/progbar.c) as the machine
 * draws it, for a look (run.py). On a white ground, five status bars of
 * the bar's ground (light grey with 16 colours or more, else white), each
 * with a bar 3 pixels high at its foot: a quarter, half, three quarters
 * and the whole, and a fifth one whole drawn in 37 pieces as a load
 * would. A taller strip at the bottom shows the whole bar 24 pixels high,
 * so the colours can be seen. It says "PRB ready" on the console
 * (NatFeats) once drawn, and "PRB pens N: p p p ..." with the pens found.
 */
#include <stdio.h>
#include "tos.h"
#include "gemui.h"
#include "dialog.h"
#include "progbar.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar;

static void box(short x, short y, short w, short h, short pen)
{
	short pxy[4];
	vsf_interior(vdi_h, 1);
	vsf_perimeter(vdi_h, 0);
	vswr_mode(vdi_h, 1);
	vsf_color(vdi_h, pen);
	pxy[0] = x;
	pxy[1] = y;
	pxy[2] = x + w - 1;
	pxy[3] = y + h - 1;
	v_bar(vdi_h, pxy);
}

int main(void)
{
	short wo[57], ex[57], bw, bh, ground, i, y, bx, bwid, sh;
	static const short at[5] = { 256, 512, 768, 1024, 1024 };
	char line[200];
	int n;

	appl_init();
	graf_mouse(256, 0);
	vdi_h = v_opnvwk_(graf_handle(&cw, &ch, &bw, &bh), wo);
	vq_extnd(vdi_h, 1, ex);
	planes = ex[4];
	scr_w = (short) (wo[0] + 1);
	scr_h = (short) (wo[1] + 1);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	ground = planes >= 4 ? pen_nearest(vdi_h, 16, 733, 733, 733, 0) : 0;
	progbar_pens(vdi_h, ground);
	wind_update(1);
	box(desk_x, desk_y, desk_w, desk_h, 0);
	bx = desk_x + 16;
	bwid = desk_w - 32;
	sh = (short) (ch + 6);
	for (i = 0, y = desk_y + 8; i < 5; i++, y += sh + 6) {
		box(bx, y, bwid, sh, ground);
		box(bx, y, bwid, 1, 1);		/* the status bar's line above */
		if (i < 4)
			progbar_fill(vdi_h, bx + 1, y + sh - 3, bwid - 2, 3, 0, at[i]);
		else {
			short k, from = 0;
			for (k = 1; k <= 37; k++) {
				short to = (short) ((long) PB_FULL * k / 37);
				progbar_fill(vdi_h, bx + 1, y + sh - 3, bwid - 2, 3, from, to);
				from = to;
			}
		}
	}
	box(bx, y, bwid, 26, ground);
	progbar_fill(vdi_h, bx + 1, y + 1, bwid - 2, 24, 0, PB_FULL);
	wind_update(0);
	n = snprintf(line, sizeof line, "PRB pens %d:", progbar_npens());
	for (i = 0; i < progbar_npens() && n < (int) sizeof line - 6; i++)
		n += snprintf(line + n, sizeof line - n, " %d", progbar_pen(i));
	nf_debug(line);
	nf_debug("\n");
	for (i = 0; i < progbar_npens(); i++) {
		short c[3];
		vq_color(vdi_h, progbar_pen(i), c);
		snprintf(line, sizeof line, "PRB rgb %d %d %d %d\n", progbar_pen(i), c[0], c[1], c[2]);
		nf_debug(line);
	}
	snprintf(line, sizeof line, "PRB planes %d ground %d\nPRB ready\n", planes, ground);
	nf_debug(line);
	for (;;) {
		short msg[8];
		AEVENT ev;
		if (evnt_multi_(MU_KEYBD | MU_TIMER, 0, 0, 0, 60000UL, msg, &ev) & MU_KEYBD)
			break;
	}
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
