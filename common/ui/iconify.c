/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * iconify.c - iconified windows (see iconify.h), from ECal's win.c and
 * about.c.
 */
#include <string.h>
#include "tos.h"
#include "gemui.h"
#include "aboutbox.h"
#include "iconify.h"

#define ICON_W 32
#define ICON_H 32

short icon_kind(short kind)
{
	static short can = -1;
	short o1, o2, o3, o4;
	if (can < 0)
		can = (aes_version() >= 0x0400 || get_cookie(0x3F414749L))	/* ?AGI */
		      && appl_getinfo(11, &o1, &o2, &o3, &o4) && (o1 & 0x80);	/* WF_ICONIFY */
	return can ? kind | SMALLER : kind;
}

/* The desktop's palette is everyone's, so the colour icon is drawn in
   the standard pens: white, black, red, green, the greys */
void icon_draw(const GRECT *work, const GRECT *clip, const unsigned short *icon,
	       const unsigned char *icon16, const char *status)
{
	static const short std[16] = { 0, 1, 0, 0, 0, 0, 0, 0, 8, 9, 1, 1, 0, 2, 3, 9 };
	GRECT c = *clip, all = *work, r;
	short n = status ? (short)strlen(status) : 0, x, y, room, sw, sh;
	font_sys_push();
	/* the status in the 6x6 font, as the desktop labels its icons: in an
	   iconified window of 72x72 the system font leaves no room under the
	   icon (font_sys_pop() sets the app's font and size again) */
	vst_height(vdi_h, 4, &sw, &sh);
	room = n && work->h >= ICON_H + sh + 2 && work->w >= n * sw;
	x = work->x + (work->w - ICON_W) / 2;
	y = work->y + (work->h - ICON_H - (room ? sh + 1 : 0)) / 2;
	clip_on(&c);
	fill(&all, 0);
	if (planes >= 4 && icon16) {
		about_icon16(icon16, x, y, 1, 1, std);
	} else if (icon) {
		/* in runs of fills, which keep to the clip as a raster copy wouldn't */
		short row, col, e;
		for (row = 0; row < ICON_H; row++)
			for (col = 0; col < ICON_W; col = e) {
				short on = (icon[row * 2 + col / 16] >> (15 - (col & 15))) & 1;
				for (e = col + 1; e < ICON_W && ((icon[row * 2 + e / 16] >> (15 - (e & 15))) & 1) == on; e++)
					;
				if (on) {
					r.x = x + col;
					r.y = y + row;
					r.w = e - col;
					r.h = 1;
					fill(&r, 1);
				}
			}
	}
	if (room) {
		vswr_mode(vdi_h, 2);	/* transparent, in black, plain, from the top */
		vst_color(vdi_h, 1);
		vst_effects(vdi_h, 0);
		vst_alignment(vdi_h, 0, 5);
		v_gtext_n(vdi_h, work->x + (work->w - n * sw) / 2, y + ICON_H + 1, status, n);
	}
	clip_off();
	font_sys_pop();
}

/* a and b's overlap in a; 0 if none */
static int overlap(GRECT *a, const GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x, y1 = a->y > b->y ? a->y : b->y;
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	if (x2 <= x1 || y2 <= y1)
		return 0;
	a->x = x1;
	a->y = y1;
	a->w = x2 - x1;
	a->h = y2 - y1;
	return 1;
}

void icon_redraw(short h, const GRECT *area, const unsigned short *icon,
		 const unsigned char *icon16, const char *status)
{
	GRECT work, want, box;
	if (h <= 0)
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(h, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	want = work;
	if (!area || overlap(&want, area)) {
		wind_get(h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h) {
			if (overlap(&box, &want))
				icon_draw(&work, &box, icon, icon16, status);
			wind_get(h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}
