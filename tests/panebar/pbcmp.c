/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * PBCMP.PRG, pb_draw's fast way (pb_vdi) against objc_draw on the
 * machine's own AES and VDI (run_cmp.py): a bar down (on a 16-pixel group
 * and off it) and one across, lists of many sizes, the slider moved a
 * line, a page and further each way. Each move: the bar drawn whole by
 * the AES at the old top, then the slider's move (pb_moved) drawn the fast
 * way, and again by objc_draw; the bar's bytes off the screen, each
 * compared byte for byte with the whole bar drawn afresh by objc_draw at
 * the new top (so pb_moved's box is checked too: big enough). Then boxes
 * with an arrow in them and whole bars, over the bar blacked out, the
 * fast way against objc_draw. Then the time of a line's move and of a
 * whole bar each way. Lines go to the emulator's console (NatFeats) as
 * "PBC ...", the last "PBC done"; a difference says "PBC DIFFERS".
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "text.h"
#include "dialog.h"
#include "panebar.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;

static MFDB scr, mem;
static unsigned char *buf_a, *buf_b;
static long bytes;
static int differ, checks, attr_lost;
static char line[160];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

/* the bar's box off the screen */
static void grab(const PANEBAR *b, unsigned char *to)
{
	short p[8], c[4] = { 0, 0, 0, 0 };
	p[0] = b->r.x;
	p[1] = b->r.y;
	p[2] = b->r.x + b->r.w - 1;
	p[3] = b->r.y + b->r.h - 1;
	p[4] = p[5] = 0;
	p[6] = b->r.w - 1;
	p[7] = b->r.h - 1;
	mem.fd_w = b->r.w;
	mem.fd_h = b->r.h;
	mem.fd_wdwidth = (short)((b->r.w + 15) / 16);
	mem.fd_addr = to;
	memset(to, 0x55, bytes);
	vs_clip(vdi_h, 0, c);
	vro_cpyfm(vdi_h, 3, p, &scr, &mem);
	text_reclip(vdi_h);
}

/* the bar whole at top old, then its move to top new, drawn the fast way
   (fast) or by the AES, into to */
static void move(PANEBAR *b, long old, long top, int fast, unsigned char *to)
{
	GRECT m;
	b->top = old;
	pb_vdi(0);
	pb_draw(b, &b->r);
	b->top = top;
	m = pb_moved(b, old);
	/* the app's own fill and mode, as an app that keeps them would have
	   them: the fast way must leave them so (ECal, EContact, EMail) */
	vswr_mode(vdi_h, 2);
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 4);
	vsf_color(vdi_h, 1);
	pb_vdi(fast ? vdi_h : 0);
	pb_draw(b, &m);
	pb_vdi(0);
	{
		short a[5];
		vqf_attributes(vdi_h, a);
		if (a[0] != 2 || a[1] != 1 || a[2] != 4 || a[3] != 2)
			attr_lost++;
	}
	vswr_mode(vdi_h, 1);
	grab(b, to);
}

static void one(PANEBAR *b, long old, long top, const char *what)
{
	size_t size = (size_t)(((b->r.w + 15) / 16) * 2L * b->r.h * planes);
	int way;
	/* the whole bar drawn afresh by the AES at the new top */
	b->top = top;
	pb_vdi(0);
	pb_draw(b, &b->r);
	grab(b, buf_b);
	/* against it, the move drawn the fast way, and by the AES (as under
	   NVDI): each drawn right, and pb_moved's box big enough */
	for (way = 1; way >= 0; way--) {
		long n = pb_vdi_count();
		move(b, old, top, way, buf_a);
		checks++;
		if (memcmp(buf_a, buf_b, size)) {
			differ++;
			snprintf(line, sizeof(line), "PBC DIFFERS %s: total %ld shown %ld, top %ld to %ld (%s)", what,
				 b->total, b->shown, old, top, pb_vdi_count() > n ? "fast" : "by the AES");
			say(line);
		}
	}
}

static void moves(PANEBAR *b, const char *what)
{
	static const long sizes[][2] = { { 300, 15 }, { 40, 15 }, { 20, 15 }, { 1000, 10 }, { 16, 15 }, { 100, 50 },
					 { 31, 30 }, { 5000, 20 } };
	short s;
	for (s = 0; s < (short)(sizeof(sizes) / sizeof(sizes[0])); s++) {
		long total = sizes[s][0], shown = sizes[s][1], range = total - shown, t;
		b->total = total;
		b->shown = shown;
		for (t = 0; t <= range; t += range > 40 ? range / 40 : 1) {
			if (t + 1 <= range)
				one(b, t, t + 1, what);		/* a line down */
			if (t >= 1)
				one(b, t, t - 1, what);		/* a line up */
			if (t + shown <= range)
				one(b, t, t + shown, what);	/* a page */
			if (t + 3 * shown <= range)
				one(b, t, t + 3 * shown, what);	/* further */
		}
	}
}

/* the bar's box blacked out, then c of it drawn, fast or by the AES */
static void boxed(PANEBAR *b, const GRECT *c, int fast, unsigned char *to)
{
	short p[4];
	p[0] = b->r.x;
	p[1] = b->r.y;
	p[2] = b->r.x + b->r.w - 1;
	p[3] = b->r.y + b->r.h - 1;
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, 1);
	vr_recfl(vdi_h, p);
	pb_vdi(fast ? vdi_h : 0);
	pb_draw(b, c);
	pb_vdi(0);
	grab(b, to);
}

/* boxes with an arrow in them, and whole bars, the fast way against the
   AES's */
static void wholes(PANEBAR *b, const char *what)
{
	static const long sizes[][2] = { { 300, 15 }, { 20, 15 }, { 16, 15 }, { 5000, 20 } };
	short s, k, len = b->across ? b->r.w : b->r.h;
	size_t size = (size_t)(((b->r.w + 15) / 16) * 2L * b->r.h * planes);
	for (s = 0; s < (short)(sizeof(sizes) / sizeof(sizes[0])); s++) {
		long range;
		b->total = sizes[s][0];
		b->shown = sizes[s][1];
		range = b->total - b->shown;
		for (k = 0; k < 4; k++) {
			GRECT c = b->r;
			short *a = b->across ? &c.x : &c.y, *l = b->across ? &c.w : &c.h, j;
			b->top = range > 0 ? range * k / 3 : 0;
			for (j = 0; j < 4; j++) {
				c = b->r;
				if (j == 1)
					*l = 30;			/* the up arrow and some track */
				else if (j == 2) {
					*a += len - 30;			/* the down arrow and some track */
					*l = 30;
				} else if (j == 3) {
					*a += 7;			/* from in the arrow into the track */
					*l = 40;
				}
				boxed(b, &c, 1, buf_a);
				boxed(b, &c, 0, buf_b);
				checks++;
				if (memcmp(buf_a, buf_b, size)) {
					differ++;
					snprintf(line, sizeof(line), "PBC DIFFERS %s: whole, total %ld shown %ld top %ld, box %d", what,
						 b->total, b->shown, b->top, j);
					say(line);
				}
			}
		}
	}
}

static void timed_whole(PANEBAR *b, const char *what)
{
	short k;
	unsigned long t0, t1, t2;
	b->total = 300;
	b->shown = 15;
	b->top = 100;
	pb_vdi(0);
	t0 = tos_ms();
	for (k = 0; k < 10; k++)
		pb_draw(b, &b->r);
	t1 = tos_ms();
	pb_vdi(vdi_h);
	for (k = 0; k < 10; k++)
		pb_draw(b, &b->r);
	t2 = tos_ms();
	pb_vdi(0);
	snprintf(line, sizeof(line), "PBC time: %s, the whole bar: objc_draw %lu ms, fast %lu ms", what,
		 (t1 - t0) / 10, (t2 - t1) / 10);
	say(line);
	{
		/* the track alone (no arrow in the box) the fast way */
		GRECT c = b->r;
		short a = pb_arrow_h();
		if (b->across) {
			c.x += pb_width() + 1;
			c.w -= 2 * pb_width() + 2;
		} else {
			c.y += a + 1;
			c.h -= 2 * a + 2;
		}
		pb_vdi(vdi_h);
		t0 = tos_ms();
		for (k = 0; k < 10; k++)
			pb_draw(b, &c);
		t1 = tos_ms();
		pb_vdi(0);
		snprintf(line, sizeof(line), "PBC time: %s, the track alone, fast %lu ms; the track's pattern %d", what,
			 (t1 - t0) / 10, pb_vdi_pattern());
		say(line);
	}
}

static void timed(PANEBAR *b, const char *what)
{
	short k;
	GRECT m;
	unsigned long t0, t1, t2;
	b->total = 300;
	b->shown = 15;
	b->top = 100;
	pb_vdi(0);
	pb_draw(b, &b->r);
	b->top = 101;
	m = pb_moved(b, 100);
	t0 = tos_ms();
	for (k = 0; k < 20; k++)
		pb_draw(b, &m);
	t1 = tos_ms();
	pb_vdi(vdi_h);
	for (k = 0; k < 20; k++)
		pb_draw(b, &m);
	t2 = tos_ms();
	pb_vdi(0);
	snprintf(line, sizeof(line), "PBC time: %s, a line's move: objc_draw %lu ms, fast %lu ms", what,
		 (t1 - t0) / 20, (t2 - t1) / 20);
	say(line);
}

int main(void)
{
	short wo[57], d, sh;
	PANEBAR down, off, across;
	short c[4];
	appl_init();
	vdi_h = v_opnvwk_(graf_handle(&cw, &ch, &d, &d), wo);
	scr_w = wo[0] + 1;
	sh = wo[1] + 1;
	scr_h = sh;
	vq_extnd(vdi_h, 1, wo);
	planes = wo[4];
	vst_alignment(vdi_h, 0, 5);
	text_font(vdi_h, 1, cw, ch);
	memset(&scr, 0, sizeof(scr));
	memset(&mem, 0, sizeof(mem));
	mem.fd_nplanes = planes;
	bytes = (long)((scr_w + 15) / 16) * 2 * sh * planes;	/* the most a bar can take */
	buf_a = (unsigned char *)Malloc(bytes);
	buf_b = (unsigned char *)Malloc(bytes);
	graf_mouse(256, 0);
	wind_update(1);

	/* text_plain on: some text drawn its way on white first */
	c[0] = 0;
	c[1] = 0;
	c[2] = scr_w - 1;
	c[3] = sh - 1;
	text_clip(vdi_h, c);
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, 0);
	vsf_perimeter(vdi_h, 0);
	{
		short p[4] = { 0, sh - 3 * ch, scr_w - 1, sh - 1 };
		vr_recfl(vdi_h, p);
	}
	vst_color(vdi_h, 1);
	text_plain(vdi_h, 16, sh - 2 * ch, "PBCMP: pb_draw against objc_draw", 32);
	snprintf(line, sizeof(line), "PBC screen %dx%d planes %d, text_plain %d, pb_width %d, pb_height %d, look %s",
		 scr_w, sh, planes, text_plain_state(), pb_width(), pb_height(), win_look_3d() ? "3D" : "flat");
	say(line);

	memset(&down, 0, sizeof(down));
	down.r.x = 192;
	down.r.y = 40;
	down.r.w = pb_width();
	down.r.h = sh - 40 - 4 * ch;
	off = down;
	off.r.x = 261;		/* off a 16-pixel group */
	memset(&across, 0, sizeof(across));
	across.across = 1;
	across.r.x = 300;
	across.r.y = 60;
	across.r.w = scr_w - 340;
	across.r.h = pb_height();

	{
		long q = Fopen("QUICK", 0);	/* run_cmp.py --quick: the whole bars and the times only */
		if (q >= 0)
			Fclose((short)q);
		else {
			moves(&down, "down");
			moves(&off, "down, off a group");
			moves(&across, "across");
		}
	}
	wholes(&down, "down");
	wholes(&off, "down, off a group");
	wholes(&across, "across");
	snprintf(line, sizeof(line), "PBC %d moves, %d differ; fast way %d, %ld boxes drawn by it, the track's pattern %d",
		 checks, differ, pb_vdi_state(), pb_vdi_count(), pb_vdi_pattern());
	say(line);
	timed(&down, "down");
	timed(&across, "across");
	timed_whole(&down, "down");
	timed_whole(&across, "across");
	wind_update(0);
	graf_mouse(257, 0);
	snprintf(line, sizeof(line), "PBC the app's fill and mode changed after %d moves", attr_lost);
	say(line);
	say(differ || attr_lost || pb_vdi_state() != 1 ? "PBC FAILED" : "PBC all the same");
	say("PBC done");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
