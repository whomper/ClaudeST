/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * popup.c - right-click menus (the AERA guidelines, 3.3), ECal's: a
 * GEM-style popup at the mouse, in the system font. Click an item, or
 * press, drag and release; arrows, Return and Esc work too. Adapted from
 * Claude ST's popup. Drawn with the app's own primitives (gemui.h).
 *
 * Labels starting with '-' are separators, with '~' disabled items, and
 * with '\x08' (the Atari check mark) checked ones.
 */
#include <string.h>
#include "gemui.h"
#include "popup.h"

static short px, py, pw, ih;

static void pframe(short x, short y, short w, short h)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	frame(&r, 1);
}

static int selectable(const char *l)
{
	return l[0] != '-' && l[0] != '~';
}

static void item(const char *const *lab, short i, short on)
{
	GRECT r;
	const char *l = lab[i];
	short y = py + 2 + i * ih, x0 = px + 2 * cw;
	r.x = px + 1;
	r.y = y;
	r.w = pw - 2;
	r.h = ih;
	if (l[0] == '-') {
		fill(&r, 0);
		hline(px + 3, px + pw - 4, y + ih / 2);
		return;
	}
	fill(&r, on ? 1 : 0);
	if (l[0] == '\x08') {
		/* a check mark in the margin, where the first of the two
		   spaces would be: the text stays where unchecked text is */
		text_at(px + cw / 2, y + (ih - ch) / 2, "\x08", 1, 1, on ? TX_INVERSE : 0);
		l++;
		x0 = px + 3 * cw;
	} else if (l[0] == '~') {
		l++;
	}
	text_at(x0, y + (ih - ch) / 2, l, strlen(l), (pw - 2 * cw) / cw,
		TX_LTR | (on ? TX_INVERSE : 0) | (lab[i][0] == '~' ? TX_DIM : 0));
}

static short next(const char *const *lab, short n, short from, short d)
{
	short i = from, k;
	for (k = 0; k < n; k++) {
		i += d;
		if (i < 0)
			i = n - 1;
		if (i >= n)
			i = 0;
		if (selectable(lab[i]))
			return i;
	}
	return from;
}

static short mouse(short *mx, short *my)
{
	short mb, ks;
	graf_mkstate(mx, my, &mb, &ks);
	return mb & 3;
}

GRECT popup_area;

short popup(short x, short y, const char *const *lab, short n)
{
	short i, w = 0, sel = -1, res = -1, held, moved = 0, bh;
	short mx, my, ox, oy, m[8], clipr[4];
	GRECT shadow, body;
	AEVENT e;

	font_sys_push();
	for (i = 0; i < n; i++) {
		short l = (short)strlen(lab[i]) - (lab[i][0] == '~');
		if (l > w)
			w = l;
	}
	ih = ch + 2;
	pw = (w + 4) * cw;
	bh = n * ih + 4;
	if (x + pw + 2 > desk_x + desk_w)
		x = desk_x + desk_w - pw - 2;
	if (x < desk_x)
		x = desk_x;
	if (y + bh + 2 > desk_y + desk_h)
		y = y - bh > desk_y ? y - bh : desk_y + desk_h - bh - 2;
	if (y < desk_y)
		y = desk_y;
	px = x;
	py = y;
	popup_area.x = x;
	popup_area.y = y;
	popup_area.w = pw + 3;
	popup_area.h = bh + 3;

	wind_update(BEG_UPDATE);
	wind_update(3);			/* BEG_MCTRL: we own the mouse */
	form_dial(FMD_START, x, y, pw + 3, bh + 3);
	graf_mouse(M_OFF, 0);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vdi_h, 1, clipr);
	shadow.x = x + 2;
	shadow.y = y + 2;
	shadow.w = pw + 1;
	shadow.h = bh + 1;
	fill(&shadow, 1);
	body.x = x;
	body.y = y;
	body.w = pw;
	body.h = bh;
	fill(&body, 0);
	pframe(x, y, pw, bh);
	for (i = 0; i < n; i++)
		item(lab, i, 0);
	graf_mouse(M_ON, 0);

	held = mouse(&ox, &oy);
	for (;;) {
		short hit = -1, nsel, ex, ey;
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_TIMER, held ? 1 : 0x101, 3, 0, 20, m, &e);
		mouse(&mx, &my);
		ex = (e.which & MU_BUTTON) ? e.mx : mx;
		ey = (e.which & MU_BUTTON) ? e.my : my;
		if (ex - ox > 3 || ox - ex > 3 || ey - oy > 3 || oy - ey > 3)
			moved = 1;
		if (ex >= px && ex < px + pw && ey >= py + 2 && ey < py + 2 + n * ih) {
			hit = (ey - py - 2) / ih;
			if (!selectable(lab[hit]))
				hit = -1;
		}
		nsel = sel;
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);
			if (as == 0x1b || sc == 0x61)
				break;
			if (sc == 0x48)
				nsel = next(lab, n, sel < 0 ? 0 : sel, -1);
			else if (sc == 0x50)
				nsel = next(lab, n, sel < 0 ? n - 1 : sel, 1);
			else if ((as == 0x0d || as == ' ') && sel >= 0) {
				res = sel;
				break;
			}
		} else if (moved) {
			nsel = hit;
		}
		if (nsel != sel) {
			graf_mouse(M_OFF, 0);
			if (sel >= 0)
				item(lab, sel, 0);
			if (nsel >= 0)
				item(lab, nsel, 1);
			graf_mouse(M_ON, 0);
			sel = nsel;
		}
		if (!(e.which & MU_BUTTON))
			continue;
		if (held) {			/* the opening button came up */
			held = 0;
			if (moved && hit >= 0) {	/* press, drag, release */
				res = hit;
				break;
			}
		} else {			/* a click: an item, or outside to cancel */
			res = hit;
			break;
		}
	}
	vs_clip(vdi_h, 0, clipr);
	wind_update(2);			/* END_MCTRL */
	wind_update(END_UPDATE);
	while (mouse(&mx, &my))
		evnt_timer_(10);
	form_dial(FMD_FINISH, x, y, pw + 3, bh + 3);
	font_sys_pop();
	return res;
}
