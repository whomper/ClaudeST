/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * OFFCMP.PRG: a piece of the screen drawn through offdraw.c (off_begin,
 * off_end) against the same drawn on the screen, on the machine's own
 * TOS (run.py). The piece: a fill, the VDI's text, text_plain's, a
 * button by objc_draw; at places on and off a 16-pixel group, and pieces
 * of 1 to OFF_ROWS rows. For each: the screen not changed before
 * off_end, and after it byte for byte as the direct drawing. Then the
 * time of a progress row's redraw each way. Lines go to the emulator's
 * console as "OFC ...", the last "OFC done".
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "text.h"
#include "offdraw.h"

short vdi_h, cw, ch, scr_w, scr_h, planes;
static MFDB scr, mem;
static unsigned char *buf_a, *buf_b;
static long bytes;
static int differ, flick, checks;
static char line[160];
static OBJECT btn[1];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

static void grab(short x, short y, short w, short h, unsigned char *to)
{
	short p[8], c[4] = { 0, 0, 0, 0 };
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
	vs_clip(vdi_h, 0, c);
	vro_cpyfm(vdi_h, 3, p, &scr, &mem);
}

/* a grey ground over the place */
static void ground(short x, short y, short w, short h)
{
	short p[4], c[4] = { 0, 0, 0, 0 };
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	vs_clip(vdi_h, 0, c);
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, 1);
	vsf_perimeter(vdi_h, 0);
	vr_recfl(vdi_h, p);
}

/* the piece, drawn under the clip (x, y, w, h) */
static void piece(short x, short y, short w, short h)
{
	short c[4], p[4];
	c[0] = x;
	c[1] = y;
	c[2] = x + w - 1;
	c[3] = y + h - 1;
	text_clip(vdi_h, c);
	scr_fill(vdi_h, c, 0);
	p[0] = x + 3;
	p[1] = y + 2;
	p[2] = x + w / 3;
	p[3] = y + h - 3;
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, 1);
	vr_recfl(vdi_h, p);
	vst_color(vdi_h, 1);
	vswr_mode(vdi_h, 2);
	v_gtext_n(vdi_h, x + w / 3 + 5, y + 1, "43%  5.9/13.6MB", 15);
	vswr_mode(vdi_h, 1);
	text_plain(vdi_h, (short)((x + w / 2 + 7) & ~7), y + 1, "73 KB/s", 7);
	btn[0].ob_x = (short)(x + w - 7 * cw);
	btn[0].ob_y = y + 3;
	objc_draw(btn, 0, 0, x, y, w, h);
}

/* the shown screen's bytes of rows y.. (h of them), read by the CPU from
   Physbase: the VDI's raster copy reads the logical screen, which is the
   buffer between off_begin and off_end */
static unsigned long shown_sum(short y, short h)
{
	unsigned char *s = (unsigned char *)trap14_w(2);	/* Physbase */
	long bpl = (long)((scr_w + 15) / 16) * 2 * planes, k, n = bpl * h;
	unsigned long sum = 0;
	s += (long)y * bpl;
	for (k = 0; k < n; k++)
		sum = sum * 31 + s[k];
	return sum;
}

static void one(short x, short y, short w, short h)
{
	short gx = x - 16 < 0 ? 0 : x - 16, gy = y - 4 < 0 ? 0 : y - 4, gw = w + 32, gh = h + 8;
	size_t size;
	if (gx + gw > scr_w)
		gw = (short)(scr_w - gx);
	if (gy + gh > scr_h)
		gh = (short)(scr_h - gy);
	size = (size_t)((gw + 15) / 16) * 2 * gh * planes;
	/* direct */
	ground(gx, gy, gw, gh);
	piece(x, y, w, h);
	grab(gx, gy, gw, gh, buf_a);
	/* off the screen */
	ground(gx, gy, gw, gh);
	checks++;
	if (off_begin(vdi_h, x, y, w, h)) {
		unsigned long before = shown_sum(gy, gh);
		piece(x, y, w, h);
		if (shown_sum(gy, gh) != before) {
			flick++;
			snprintf(line, sizeof(line), "OFC SHOWN BEFORE off_end: %d,%d %dx%d", x, y, w, h);
			say(line);
		}
		off_end();
	} else
		piece(x, y, w, h);
	grab(gx, gy, gw, gh, buf_b);
	if (memcmp(buf_a, buf_b, size)) {
		differ++;
		snprintf(line, sizeof(line), "OFC DIFFERS %d,%d %dx%d (off %d)", x, y, w, h, off_state());
		say(line);
	}
}

int main(void)
{
	short wo[57], d, k;
	static const short hs[] = { 1, 2, 7, 16, 19, 33, 64 };
	appl_init();
	vdi_h = v_opnvwk_(graf_handle(&cw, &ch, &d, &d), wo);
	scr_w = wo[0] + 1;
	scr_h = wo[1] + 1;
	vq_extnd(vdi_h, 1, wo);
	planes = wo[4];
	vst_alignment(vdi_h, 0, 5);
	text_font(vdi_h, 1, cw, ch);
	memset(&scr, 0, sizeof(scr));
	memset(&mem, 0, sizeof(mem));
	mem.fd_nplanes = planes;
	bytes = (long)((scr_w + 15) / 16) * 2 * scr_h * planes;
	buf_a = (unsigned char *)Malloc(bytes);
	buf_b = (unsigned char *)Malloc(bytes);
	memset(btn, 0, sizeof(btn));
	btn[0].ob_next = btn[0].ob_head = btn[0].ob_tail = -1;
	btn[0].ob_type = G_BUTTON;
	btn[0].ob_flags = SELECTABLE | EXIT | LASTOB;
	btn[0].ob_spec = (long)"Stop";
	btn[0].ob_width = 5 * cw;
	btn[0].ob_height = ch;
	graf_mouse(256, 0);
	wind_update(1);
	{
		/* text_plain on: a word drawn its way first */
		short c[4];
		c[0] = 0;
		c[1] = 0;
		c[2] = scr_w - 1;
		c[3] = scr_h - 1;
		text_clip(vdi_h, c);
		vswr_mode(vdi_h, 1);
		vst_color(vdi_h, 1);
		text_plain(vdi_h, 16, scr_h - 2 * ch, "OFFCMP", 6);
	}
	for (k = 0; k < (short)(sizeof(hs) / sizeof(hs[0])); k++) {
		one(32, 40 + k * 3, 320, hs[k]);
		one(45, 40 + k * 3, 301, hs[k]);
		one(scr_w - 330, 100, 330, hs[k]);
	}
	one(7, scr_h - 70, 600, 64);
	snprintf(line, sizeof(line), "OFC screen %dx%d planes %d: %d pieces, %d differ, %d shown before off_end; off %d",
		 scr_w, scr_h, planes, checks, differ, flick, off_state());
	say(line);
	{
		/* a progress row's redraw, 20 times each way */
		unsigned long t0, t1, t2;
		short y = 120;
		t0 = tos_ms();
		for (k = 0; k < 20; k++)
			piece(16, y, 600, ch);
		t1 = tos_ms();
		for (k = 0; k < 20; k++)
			if (off_begin(vdi_h, 16, y, 600, ch)) {
				piece(16, y, 600, ch);
				off_end();
			}
		t2 = tos_ms();
		snprintf(line, sizeof(line), "OFC time: a row %dx%d on the screen %lu us, off it %lu us", 600, ch,
			 (t1 - t0) * 50, (t2 - t1) * 50);
		say(line);
	}
	wind_update(0);
	graf_mouse(257, 0);
	say(differ || flick || off_state() <= 0 ? "OFC FAILED" : "OFC all the same");
	say("OFC done");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
