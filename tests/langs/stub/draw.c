/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * draw.c - the app's own drawing that the Languages dialog takes (gemui.h), for
 * LANGT.PRG (the file dialog's FPTEST.PRG has the same): EPlay's, the system font only (from EUpdate's). Common's fast paths where
 * they are faster (the guidelines' Speed, AERA-Atari 482f184): plain
 * text by text_plain and bold by text_fx where they go into the screen
 * (TOS's VDI), else the VDI's text as before; the clip through
 * text_clip, which text_n and text_plain go by; the VDI's attributes set
 * only when they change. scr_fill isn't used: a Falcon's VDI fills as
 * fast (scr_cpu_wins, b3fafbe).
 */
#include <string.h>
#include "ui.h"
#include "text.h"

static short cur_mode = -1, cur_fill = -1, cur_line = -1, cur_tcol = -1, cur_fx = -1, fill_set;

static void mode(short m)
{
	if (cur_mode != m) {
		vswr_mode(vdi_h, m);
		cur_mode = m;
	}
}

/* the attributes as they are not known (another's drawing: the About box) */
void draw_forget(void)
{
	cur_mode = cur_fill = cur_line = cur_tcol = cur_fx = -1;
	fill_set = 0;
}

void clip_on(GRECT *r)
{
	short pxy[4];
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	text_clip(vdi_h, pxy);
}

void clip_off(void)
{
	text_noclip(vdi_h);
}

/* bitmaps the VDI copies must be in ST RAM: one small block */
static unsigned short st_fallback[256];
static unsigned short *st_buf;
void *st_ram(long n)
{
	if (!st_buf) {
		void *p = Mxalloc(sizeof(st_fallback), 0);
		st_buf = (long)p > 0 ? p : st_fallback;
	}
	return n <= (long)sizeof(st_fallback) ? st_buf : 0;
}

void fill(GRECT *r, short color)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	if (!fill_set) {
		vsf_interior(vdi_h, 1);
		vsf_perimeter(vdi_h, 0);
		fill_set = 1;
	}
	if (cur_fill != color) {
		vsf_color(vdi_h, color);
		cur_fill = color;
	}
	mode(1);
	vr_recfl(vdi_h, pxy);
}

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	mode(1);
	if (cur_line != color) {
		vsl_color(vdi_h, color);
		cur_line = color;
	}
	v_pline(vdi_h, 2, p);
}

void hline(short x1, short x2, short y)
{
	line_col(x1, y, x2, y, 1);
}

void frame(GRECT *r, short color)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	mode(1);
	if (cur_line != color) {
		vsl_color(vdi_h, color);
		cur_line = color;
	}
	v_pline(vdi_h, 5, p);
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	if (n > cols)
		n = cols;
	if (n <= 0)
		return;
	if (!(cw & 7))
		x = (x + 3) & ~7;		/* text on a byte: the VDI's fast way */
	/* plain or bold, black on the white ground: common's fast ways,
	   where they go into the screen (TOS's VDI: text_plain_state 0
	   before the first check, 1 after), in MD_REPLACE as they want it */
	if (!(flags & (TX_INVERSE | TX_DIM)) && text_plain_state() >= 0 &&
	    (!(flags & TX_BOLD) || text_fx_fast(1, 1))) {
		short fx;
		mode(1);
		if (cur_tcol != 1) {
			vst_color(vdi_h, 1);
			cur_tcol = 1;
		}
		if (cur_fx != 0) {
			vst_effects(vdi_h, 0);
			cur_fx = 0;
		}
		fx = (flags & TX_BOLD) ? 1 : 0;
		text_plain(vdi_h, x, y, s, n);
		if (fx)
			text_fx(vdi_h, x, y, s, n, fx, 1);	/* (bold on the plain text) */
		return;
	}
	/* else as before: the VDI's text, transparent (text_n: v_gtext_n,
	   clipped the fast way on TOS's VDI, as it is under NVDI) */
	mode(2);
	if (cur_tcol != ((flags & TX_INVERSE) ? 0 : 1)) {
		cur_tcol = (flags & TX_INVERSE) ? 0 : 1;
		vst_color(vdi_h, cur_tcol);
	}
	{
		short fx = (flags & TX_BOLD) ? 1 : (flags & TX_DIM) ? 2 : 0;
		if (cur_fx != fx) {
			vst_effects(vdi_h, fx);
			cur_fx = fx;
		}
	}
	text_n(vdi_h, x, y, s, n);
}

void font_sys_push(void)
{
}

void font_sys_pop(void)
{
}

int rc_intersect(const GRECT *a, GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x, y1 = a->y > b->y ? a->y : b->y;
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	b->x = x1;
	b->y = y1;
	b->w = x2 - x1;
	b->h = y2 - y1;
	return b->w > 0 && b->h > 0;
}
