/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * BADGECMP.PRG: badge_draw (ui/badge.c) against the VDI's own drawing of
 * the same badge (four filled rectangles for the box, the 6 x 6 text
 * transparent at 3, 2 inside it), on the machine's own TOS (run.py): on a
 * plain ground and a patterned one, in each pen the screen has up to 16,
 * at places on and off a 16-pixel group, in rows taller than the badge,
 * clipped at each edge, and again after its text changed. Byte for byte
 * each. Then the time of 100 badges each way. Lines go to the emulator's
 * console as "BDG ...", the last "BDG done".
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "badge.h"

static short vdi_h, scr_w, scr_h, planes, cw, ch, text_h;
static MFDB scr, mem;
static unsigned char *buf_a, *buf_b;
static long bytes;
static int differ, checks;
static char line[160];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

static void clip(const short *c)
{
	short all[4];
	if (!c) {
		all[0] = 0;
		all[1] = 0;
		all[2] = scr_w - 1;
		all[3] = scr_h - 1;
		c = all;
	}
	vs_clip(vdi_h, 1, c);
}

static void grab(short x, short y, short w, short h, unsigned char *to)
{
	short p[8];
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	p[4] = p[5] = 0;
	p[6] = w - 1;
	p[7] = h - 1;
	mem.fd_w = w;
	mem.fd_h = h;
	mem.fd_wdwidth = (short)((w + 15) / 16);
	mem.fd_addr = to;
	memset(to, 0x55, bytes);
	clip(0);
	vro_cpyfm(vdi_h, 3, p, &scr, &mem);
}

static void ground(short x, short y, short w, short h, int pat)
{
	short p[4];
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	clip(0);
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, pat ? 2 : 1);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, pat ? 1 : 0);
	vsf_perimeter(vdi_h, 0);
	vr_recfl(vdi_h, p);
}

static void rf(short x, short y, short w, short h)
{
	short p[4];
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	vr_recfl(vdi_h, p);
}

/* the badge as the VDI draws it */
static short vdi_badge(short x, short y, short rh, const char *s, short pen)
{
	short n = (short)strlen(s), a, b, sw, sh, w, h, by;
	vst_height(vdi_h, 4, &sw, &sh);
	w = n * sw + 5;
	h = sh + 4;
	by = y + (rh - h) / 2;
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_perimeter(vdi_h, 0);
	vsf_color(vdi_h, pen);
	rf(x, by, w, 1);
	rf(x, by + h - 1, w, 1);
	rf(x, by, 1, h);
	rf(x + w - 1, by, 1, h);
	vswr_mode(vdi_h, 2);
	vst_color(vdi_h, pen);
	vst_effects(vdi_h, 0);
	v_gtext_n(vdi_h, x + 3, by + 2, s, n);
	vst_height(vdi_h, text_h, &a, &b);
	return w;
}

static void one(short x, short y, short rh, const char *s, short pen, int pat, const short *c)
{
	static BADGE bd;
	short gx = x - 16 < 0 ? 0 : x - 16, gy = y - 4 < 0 ? 0 : y - 4, gw = 200, gh = rh + 8, w1, w2;
	size_t size;
	if (gx + gw > scr_w)
		gw = scr_w - gx;
	size = (size_t)((gw + 15) / 16) * 2 * gh * planes;
	ground(gx, gy, gw, gh, pat);
	clip(c);
	w1 = vdi_badge(x, y, rh, s, pen);
	grab(gx, gy, gw, gh, buf_a);
	ground(gx, gy, gw, gh, pat);
	clip(c);
	w2 = badge_draw(&bd, x, y, rh, s, pen, c);
	grab(gx, gy, gw, gh, buf_b);
	checks++;
	if (w1 != w2 || memcmp(buf_a, buf_b, size)) {
		differ++;
		snprintf(line, sizeof(line), "BDG DIFFERS \"%s\" at %d,%d row %d pen %d %s%s (width %d, the VDI's %d)",
			 s, x, y, rh, pen, pat ? "patterned" : "plain", c ? " clipped" : "", w2, w1);
		say(line);
	}
}

int main(void)
{
	short wo[57], d, k, pens, p;
	static const char *const texts[] = { "320 KBPS", "44.1 KHZ", "DSP", "FDI", "DMA", "VBR 205 KBPS",
					     "abc xyz 0123456789 .", "HE" };
	static const short xs[] = { 16, 23, 31, 40, 47 };
	appl_init();
	vdi_h = v_opnvwk_(graf_handle(&cw, &ch, &d, &d), wo);
	scr_w = wo[0] + 1;
	scr_h = wo[1] + 1;
	pens = wo[13];
	vq_extnd(vdi_h, 1, wo);
	planes = wo[4];
	vst_alignment(vdi_h, 0, 5);
	text_h = ch >= 16 ? 13 : 6;
	{
		short a, b;
		vst_height(vdi_h, text_h, &a, &b);
	}
	badge_init(vdi_h, text_h);
	memset(&scr, 0, sizeof(scr));
	memset(&mem, 0, sizeof(mem));
	mem.fd_nplanes = planes;
	bytes = (long)((200 + 15) / 16) * 2 * 64 * planes;
	buf_a = (unsigned char *)Malloc(bytes);
	buf_b = (unsigned char *)Malloc(bytes);
	graf_mouse(256, 0);
	wind_update(1);
	if (pens > 16)
		pens = 16;
	for (k = 0; k < (short)(sizeof(texts) / sizeof(texts[0])); k++)
		for (p = 1; p < pens; p++) {
			short x = xs[(k + p) % 5], y = 40 + 2 * p;
			one(x, y, 14, texts[k], p, 0, 0);
			one(x, y, 14, texts[k], p, 1, 0);
		}
	/* rows of other heights, a badge then another text in the same one */
	for (k = 10; k <= 24; k += 7) {
		one(33, 60, k, "44.1 KHZ", 1, 0, 0);
		one(33, 60, k, "48 KHZ", 1, 0, 0);
	}
	/* clipped at each edge */
	{
		short c[4];
		c[0] = 40; c[1] = 0; c[2] = scr_w - 1; c[3] = scr_h - 1;
		one(33, 80, 14, "320 KBPS", 1, 0, c);
		c[0] = 0; c[2] = 60;
		one(33, 80, 14, "320 KBPS", 1, 0, c);
		c[2] = scr_w - 1; c[1] = 85;
		one(33, 80, 14, "320 KBPS", 1, 1, c);
		c[1] = 0; c[3] = 86;
		one(33, 80, 14, "320 KBPS", 1, 1, c);
		c[0] = 200; c[1] = 0; c[2] = 300; c[3] = 20;
		one(33, 80, 14, "320 KBPS", 1, 0, c);		/* all outside */
	}
	snprintf(line, sizeof(line), "BDG screen %dx%d planes %d: %d badges, %d differ", scr_w, scr_h, planes, checks,
		 differ);
	say(line);
	{
		static BADGE t;
		unsigned long t0, t1, t2;
		t0 = tos_ms();
		for (k = 0; k < 100; k++)
			vdi_badge(16, 120, 14, "44.1 KHZ", 1);
		t1 = tos_ms();
		for (k = 0; k < 100; k++)
			badge_draw(&t, 16, 140, 14, "44.1 KHZ", 1, 0);
		t2 = tos_ms();
		snprintf(line, sizeof(line), "BDG time: a badge the VDI's way %lu us, badge_draw's %lu us%s",
			 (t1 - t0) * 10, (t2 - t1) * 10, badge_vdi ? " (the VDI's way: the font isn't TOS's)" : "");
		say(line);
	}
	wind_update(0);
	graf_mouse(257, 0);
	say(differ ? "BDG FAILED" : "BDG all the same");
	say("BDG done");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
