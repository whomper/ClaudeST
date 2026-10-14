/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * aboutbox.c - the About box (see aboutbox.h), ECal's, drawn as EMail's and
 * Claude ST's were.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "gemui.h"
#include "aboutbox.h"

#define ICON_W 32
#define ICON_H 32

static short dx, dy, dw, dh;

static void line(short x1, short y1, short x2, short y2)
{
	line_col(x1, y1, x2, y2, 1);
}

static void rect_fill(short x1, short y1, short x2, short y2, short color)
{
	GRECT r;
	r.x = x1;
	r.y = y1;
	r.w = x2 - x1 + 1;
	r.h = y2 - y1 + 1;
	fill(&r, color);
}

static void box(short x, short y, short w, short h)
{
	rect_fill(x + 3, y + 3, x + w + 2, y + h + 2, 1);	/* shadow */
	rect_fill(x, y, x + w - 1, y + h - 1, 0);
	line(x, y, x + w - 1, y);
	line(x + w - 1, y, x + w - 1, y + h - 1);
	line(x + w - 1, y + h - 1, x, y + h - 1);
	line(x, y + h - 1, x, y);
	line(x + 2, y + 2, x + w - 3, y + 2);
	line(x + w - 3, y + 2, x + w - 3, y + h - 3);
	line(x + w - 3, y + h - 3, x + 2, y + h - 3);
	line(x + 2, y + h - 3, x + 2, y + 2);
}

/* the 32x32 icon scaled by sx, sy (2x on square pixels; 2x1 in ST
 * medium, whose pixels are twice as tall as wide), blitted from ST RAM */
static unsigned short icon_big[64 * 4];

static void draw_icon(const unsigned short *bits, short x, short y, short sx, short sy)
{
	MFDB src, dst;
	short pxy[8], colors[2], r, c, k, wpl = 2 * sx;
	short w = ICON_W * sx, h = ICON_H * sy;
	memset(icon_big, 0, sizeof(icon_big));
	for (r = 0; r < ICON_H; r++)
		for (c = 0; c < ICON_W; c++) {
			if (!(bits[r * 2 + c / 16] & (0x8000 >> (c & 15))))
				continue;
			for (k = 0; k < sx; k++) {
				short bx = c * sx + k, yy;
				for (yy = 0; yy < sy; yy++)
					icon_big[(r * sy + yy) * wpl + bx / 16] |= 0x8000 >> (bx & 15);
			}
		}
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = st_ram(sizeof(icon_big));
	if (!src.fd_addr)
		return;
	memcpy(src.fd_addr, icon_big, sizeof(icon_big));
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	colors[0] = 1;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

/* the 16-colour icon, as runs of VDI fills; pens 8..15 are lent the
 * icon's colours while the box is open */
void about_icon16(const unsigned char *px, short x, short y, short sx, short sy, const short *pens)
{
	short r, c;
	for (r = 0; r < ICON_H; r++) {
		for (c = 0; c < ICON_W; ) {
			unsigned char b = px[r * 16 + c / 2];
			short pen = (c & 1) ? (b & 15) : (b >> 4), e = c + 1;
			while (e < ICON_W) {
				unsigned char b2 = px[r * 16 + e / 2];
				if (((e & 1) ? (b2 & 15) : (b2 >> 4)) != pen)
					break;
				e++;
			}
			if (pen)
				rect_fill(x + c * sx, y + r * sy, x + e * sx - 1, y + r * sy + sy - 1,
					  pens ? pens[pen] : pen);
			c = e;
		}
	}
}

const char *about_ram(void)
{
	/* TT/Fast RAM starts at 16 MB: where TOS loaded the program and
	   where its memory came from */
	void *m = malloc(16);
	int code = (unsigned long)about_ram >= 0x01000000UL, heap = (unsigned long)m >= 0x01000000UL;
	free(m);
	return code && heap ? "TT RAM" : code || heap ? "TT RAM in part" : "";
}

static void center(short y, const char *s, short flags)
{
	short n = (short)strlen(s);
	text_at(dx + (dw - n * cw) / 2, y, s, n, n, flags | TX_LTR);
}

static void button(short x, short y, short w, short h, const char *label)
{
	short n = (short)strlen(label);
	rect_fill(x, y, x + w - 1, y + h - 1, 0);
	line(x, y, x + w - 1, y);
	line(x + w - 1, y, x + w - 1, y + h - 1);
	line(x + w - 1, y + h - 1, x, y + h - 1);
	line(x, y + h - 1, x, y);
	/* the default button's thick GEM border */
	line(x - 1, y - 1, x + w, y - 1);
	line(x + w, y - 1, x + w, y + h);
	line(x + w, y + h, x - 1, y + h);
	line(x - 1, y + h, x - 1, y - 1);
	text_at(x + (w - n * cw) / 2, y + (h - ch) / 2, label, n, n, TX_BOLD | TX_LTR);
}

static void wait_up(void)
{
	short mx, my, mb, ks;
	do {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (mb & 3)
			evnt_timer_(10);
	} while (mb & 3);
}

void about_show(const ABOUT *a)
{
	char v[40];
	short sx = 2, sy = gl_hchar >= 16 ? 2 : 1;
	short ih, lh, m[8], y, done = 0, i, nst = 0, widest = 0;
	short saved_rgb[8][3], colour;
	short bw, bh, by;
	AEVENT e;

	font_sys_push();
	ih = ICON_H * sy;
	lh = ch + (ch >= 16 ? 2 : 1);
	colour = planes >= 4 && a->icon16 && a->icon16_rgb;
	bw = 10 * cw;
	bh = ch + 6;
	for (i = 0; i < 3 && a->status[i]; i++) {
		if ((short)strlen(a->status[i]) > widest)
			widest = (short)strlen(a->status[i]);
		nst++;
	}
	dw = 50 * cw;
	if (widest + 4 > 50)			/* the status lines fit whole */
		dw = (widest + 4) * cw;
	if (dw > desk_w - 2 * cw)
		dw = desk_w - 2 * cw;
	dh = lh + ih + lh / 2 + lh + (lh + lh / 2) + (lh + lh / 2) + lh / 4 + lh + lh +
	     (lh + lh / 2) + (nst ? nst * lh + lh / 2 : 0) + bh + lh;
	if (dh > desk_h - 4)
		dh = desk_h - 4;
	dx = desk_x + (desk_w - dw) / 2;
	dy = desk_y + (desk_h - dh) / 2;
	by = dy + dh - bh - lh;

	wind_update(BEG_UPDATE);
	wind_update(3);
	form_dial(FMD_START, dx, dy, dw + 3, dh + 3);
	graf_mouse(M_OFF, 0);
	clip_off();
	box(dx, dy, dw, dh);

	y = dy + lh;
	if (colour) {
		for (i = 0; i < 8; i++) {
			vq_color(vdi_h, 8 + i, saved_rgb[i]);
			vs_color(vdi_h, 8 + i, a->icon16_rgb[i]);
		}
		about_icon16(a->icon16, dx + (dw - ICON_W * sx) / 2, y, sx, sy, 0);
	} else if (a->icon) {
		draw_icon(a->icon, dx + (dw - ICON_W * sx) / 2, y, sx, sy);
	}
	y += ih + lh / 2;
	center(y, a->name, TX_BOLD);
	{
		/* underlined, like EMail's and Claude ST's names */
		short n = (short)strlen(a->name), x = dx + (dw - n * cw) / 2;
		line(x, y + ch - 1, x + n * cw - 1, y + ch - 1);
	}
	y += lh;
	snprintf(v, sizeof(v), "Version %s", a->version);
	center(y, v, 0);
	y += lh + lh / 2;
	center(y, a->tagline, 0);
	y += lh + lh / 2;
	line(dx + 4 * cw, y - lh / 4, dx + dw - 4 * cw, y - lh / 4);
	y += lh / 4;
	center(y, "Created by Erez Yaary", TX_BOLD);
	y += lh;
	center(y, "\xBD 2026 Erez Yaary", 0);
	y += lh;
	center(y, "Free software under the GNU GPL 2 or later", 0);
	y += lh + lh / 2;
	for (i = 0; i < nst; i++, y += lh)
		center(y, a->status[i], 0);
	button(dx + (dw - bw) / 2, by, bw, bh, "OK");
	graf_mouse(M_ON, 0);
	wait_up();

	/* Return, Esc, Undo, Help, Space or a click closes it */
	while (!done) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);
			if (as == 0x0d || as == 0x1b || as == ' ' || sc == 0x61 || sc == 0x62)
				done = 1;
		}
		if (e.which & MU_BUTTON) {
			wait_up();
			done = 1;
		}
	}
	if (colour)
		for (i = 0; i < 8; i++)
			vs_color(vdi_h, 8 + i, saved_rgb[i]);
	wind_update(2);
	wind_update(END_UPDATE);
	form_dial(FMD_FINISH, dx, dy, dw + 3, dh + 3);
	font_sys_pop();
}
