/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * DLGCMP.PRG, dlg_draw's fast way (the root's fill, flat G_BOXTEXTs and
 * plain G_STRINGs by the CPU) against objc_draw on the machine's own AES
 * and VDI (run_cmp.py). Trees: one laid out as EFTP's Settings (a band,
 * five tabs 2 pixels off a byte, a page of words, ticks, a field, a
 * choice, buttons, a hidden page), one of the cases (words at each of a
 * byte's eight pixels, each justification, pen and writing mode, boxes
 * taller than a line, words longer than their box, words under a button
 * drawn after them and over a box drawn before them, a disabled word),
 * and roots in black and hollow. Each at several places on the screen,
 * drawn whole and object by object (dlg_draw's clip round each), over a
 * grey ground: the box round it off the screen, each way, compared byte
 * for byte. Then the time of the whole Settings each way. Lines go to
 * the emulator's console (NatFeats) as "DLC ...", the last "DLC done"; a
 * difference says "DLC DIFFERS".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "text.h"
#include "dialog.h"

extern unsigned long dlg_fast_t[4];
extern unsigned long dlg_fast_kt[8];
extern long dlg_fast_kn[8];

short vdi_h, cw, ch, scr_w, scr_h, planes;

#define NOBJ 96
static OBJECT tree[NOBJ];
static TEDINFO ted[40];
static short nobj, nted, last;
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

static void begin(long spec, short state, short w, short h)
{
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	nted = 0;
	last = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = state;
	tree[0].ob_spec = spec;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

/* a child of the root, in pixels */
static short add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	o->ob_head = o->ob_tail = -1;
	o->ob_next = 0;
	if (last)
		tree[last].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last = i;
	return i;
}

static void end(void)
{
	tree[nobj - 1].ob_flags |= LASTOB;
}

static long tedi(const char *text, short just, short color, short thick)
{
	TEDINFO *t = &ted[nted++];
	memset(t, 0, sizeof(*t));
	t->te_ptext = (char *)text;
	t->te_ptmplt = "";
	t->te_pvalid = "";
	t->te_font = 3;
	t->te_just = just;
	t->te_color = color;
	t->te_thickness = thick;
	t->te_txtlen = (short)strlen(text) + 1;
	t->te_tmplen = 1;
	return (long)t;
}

static short str(short x, short y, const char *s)
{
	return add(G_STRING, 0, 0, (long)s, x, y, (short)(strlen(s) * cw), ch);
}

/* EFTP's Settings, as ui/dialogs.c lays it out (SW 13, SX 15) */
static char fld[24] = "60";
static char tmpl[24] = "____", valid[24] = "9999";
static void tree_settings(void)
{
	static const char *names[] = { "  Transfers", "  Names", "  Look", "  Connection", "  Secure" };
	static const char *words[] = { "When a file is already there", "Keep the date and time",
				       "Beep when transfers are done", "Shorten long names", "Time out (seconds)" };
	short k, sw = 13, sx = 15, gap = (short)(ch / 2);
	begin(0x00021100L, OUTLINED, (short)((sx + 42) * cw), (short)(16 * ch));
	add(G_BOXTEXT, 0, 0, tedi(" Settings", 0, 0x1071, 0), 0, 0, (short)((sx + 42) * cw), ch);
	add(G_BOX, 0, 0, 0x00001171L, (short)(sw * cw), ch, 1, (short)(15 * ch));
	for (k = 0; k < 5; k++)
		add(G_BOXTEXT, TOUCHEXIT, 0, tedi(names[k], 0, k ? 0x1180 : 0x1071, 0), 2,
		    (short)((2 + k * 2) * ch + k * gap), (short)((sw - 1) * cw + 6), ch);
	add(G_IBOX, 0, 0, 0, (short)((sx - 1) * cw), (short)(2 * ch), (short)(42 * cw), (short)(12 * ch));
	str((short)(sx * cw), (short)(2 * ch), "Transfers");
	add(G_BOX, 0, 0, 0x00001171L, (short)(sx * cw), (short)(3 * ch + 2), (short)(40 * cw), 1);
	for (k = 0; k < 4; k++) {
		short y = (short)((4 + k) * ch + k * (ch * 3 / 8));
		add(G_BOX, SELECTABLE, k == 1 ? SELECTED : 0, 0x00FF1100L, (short)(sx * cw), y, (short)(2 * cw), ch);
		str((short)((sx + 3) * cw), y, words[k]);
	}
	add(G_BOXTEXT, TOUCHEXIT, 0, tedi(" Ask                    ...", 0, 0x1180, -1), (short)((sx + 12) * cw),
	    (short)(9 * ch), (short)(28 * cw), ch);
	{
		TEDINFO *t = &ted[nted++];
		memset(t, 0, sizeof(*t));
		t->te_ptext = fld;
		t->te_ptmplt = tmpl;
		t->te_pvalid = valid;
		t->te_font = 3;
		t->te_color = 0x1180;
		t->te_thickness = -1;
		t->te_txtlen = 5;
		t->te_tmplen = 5;
		str((short)(sx * cw), (short)(11 * ch), words[4]);
		add(G_FBOXTEXT, EDITABLE, 0, (long)t, (short)((sx + 20) * cw), (short)(11 * ch), (short)(4 * cw), ch);
	}
	for (k = 0; k < 30; k++)		/* another page's, hidden */
		add(G_STRING, HIDETREE, 0, (long)words[k % 5], (short)(sx * cw), (short)((4 + k % 8) * ch), 28 * 8, ch);
	add(G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", (short)((sx + 18) * cw), (short)(14 * ch), (short)(10 * cw), ch);
	add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", (short)((sx + 30) * cw), (short)(14 * ch), (short)(10 * cw), ch);
	end();
}

/* the cases */
static void tree_cases(void)
{
	static const short colors[] = { 0x1180, 0x1071, 0x1100, 0x1171, 0x1081, 0x1001, 0x1170, 0x1080 };
	short k, y;
	begin(0x00011100L, 0, (short)(76 * cw), (short)(13 * ch + 56));
	for (k = 0; k < 8; k++)		/* a byte's eight pixels */
		str((short)(8 + k * 9 * cw + k), (short)(ch / 2), "Wordy");
	y = (short)(2 * ch);
	for (k = 0; k < 24; k++) {	/* justification x pen and mode x a box taller and off a byte */
		short just = (short)(k % 3), c = colors[k % 8], x = (short)(8 + (k % 4) * 18 * cw + (k % 7)),
		      h = (short)(k >= 12 ? ch + 5 : ch);
		add(G_BOXTEXT, 0, 0, tedi(k & 1 ? "Ab" : "  Names ", just, c, 0), x,
		    (short)(y + (k / 4) * (ch + 8)), (short)(14 * cw + (k % 5)), h);
	}
	y = (short)(y + 6 * (ch + 8) + ch);
	/* longer than its box: drawn whole by the AES all the same */
	add(G_STRING, 0, 0, (long)"a word longer than its box", 8, y, 4 * 8, ch);
	/* under a button drawn after it: the AES's */
	str(40 * 8, y, "under a button");
	add(G_BUTTON, SELECTABLE, 0, (long)"Button", (short)(40 * 8 + 3 * cw), (short)(y - 2), (short)(8 * cw), ch);
	/* over a box drawn before it, in black: the CPU's, and so is the box's */
	add(G_BOX, 0, 0, 0x00001171L, 58 * 8, y, 16 * 8, ch);
	add(G_STRING, 0, 0, (long)"on black", 59 * 8 + 3, y, 8 * 8, ch);
	y = (short)(y + 2 * ch);
	add(G_STRING, 0, DISABLED, (long)"disabled", 8, y, 8 * 8, ch);
	add(G_BOXTEXT, 0, SELECTED, tedi("selected", 2, 0x1180, 0), 20 * 8, y, 12 * 8, ch);
	add(G_BOXTEXT, 0, 0, tedi("framed", 2, 0x1180, 1), 36 * 8, y, 12 * 8, ch);
	add(G_BOXTEXT, 0, 0, tedi("patterned", 2, 0x1142, 0), 52 * 8, y, 12 * 8, ch);
	end();
}

/* a root in black, its words white on it, and one hollow and shadowed */
static void tree_black(int hollow)
{
	begin(hollow ? 0x00011100L : 0x00FF1171L, hollow ? SHADOWED : OUTLINED, (short)(30 * cw), (short)(6 * ch));
	add(G_BOXTEXT, 0, 0, tedi(" white on black", 0, 0x1071, 0), 3, ch, (short)(20 * cw), ch);
	add(G_BOXTEXT, 0, 0, tedi("black on white", 2, 0x1180, 0), 5, (short)(3 * ch), (short)(20 * cw), ch);
	str(8, (short)(5 * ch), "a word");
	end();
}

/* frames, buttons and pictures (the hub's (a) to (c), 31 Oct 2026): flat
   boxes with frames inside and outside, outlined and shadowed, selected;
   buttons plain, EXIT, DEFAULT, selected and disabled, at each of a
   byte's eight pixels; a choice; ticks as EFTP draws them */
static unsigned short tick_bits[16] = { 0x0000, 0x7FFE, 0x4002, 0x4002, 0x4006, 0x400E, 0x401A, 0x6032,
					0x7062, 0x58C2, 0x4D82, 0x4702, 0x4202, 0x4002, 0x7FFE, 0x0000 };
static BITBLK tick_blk = { tick_bits, 2, 16, 0, 0, 1 };
static void tree_frames(void)
{
	static const char *names[] = { "OK", "Cancel", "Skip", "Resume", "Replace", "Apply", "Help", "Close" };
	short k, y = 6;
	begin(0x00021100L, OUTLINED, (short)(74 * cw), (short)(16 * ch + 40));
	/* boxes: frames of 1 and 2 inside and outside, in black and in
	   white, hollow and solid; selected; outlined; shadowed */
	add(G_BOX, 0, 0, 0x00011100L, 8, y, 6 * cw, ch);
	add(G_BOX, 0, 0, 0x00021100L, 8 + 8 * cw, y, 6 * cw, ch);
	add(G_BOX, 0, 0, 0x00FF1100L, 8 + 16 * cw + 3, y, 6 * cw, ch);
	add(G_BOX, 0, 0, 0x00FE1100L, 8 + 24 * cw + 5, y, 6 * cw, ch);
	add(G_BOX, 0, 0, 0x00011171L, 8 + 32 * cw, y, 6 * cw, ch);
	add(G_BOX, 0, SELECTED, 0x00FF1100L, 8 + 40 * cw + 1, y, 2 * cw, ch);
	add(G_BOX, 0, OUTLINED, 0x00011100L, 8 + 46 * cw, y, 6 * cw, ch);
	add(G_BOX, 0, SHADOWED, 0x00011100L, 8 + 56 * cw + 2, y, 6 * cw, ch);
	add(G_BOX, 0, 0, 0x00010100L, 8 + 66 * cw, y, 6 * cw, ch);	/* a frame in white */
	y = (short)(y + ch + 12);
	/* buttons at each of a byte's eight pixels */
	for (k = 0; k < 8; k++)
		add(G_BUTTON, SELECTABLE | (k & 1 ? EXIT : 0) | (k == 3 || k == 6 ? DEFAULT : 0),
		    k == 5 ? SELECTED : 0, (long)names[k], (short)(8 + k * 9 * cw + k), y, (short)(7 * cw + (k & 3)), ch);
	y = (short)(y + ch + 12);
	add(G_BUTTON, SELECTABLE | EXIT, DISABLED, (long)"Disabled", 8, y, 10 * cw, ch);
	add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, DISABLED, (long)"Off", 8 + 12 * cw + 3, y, 10 * cw, ch);
	add(G_BUTTON, SELECTABLE | RBUTTON, SELECTED, (long)"By type", 8 + 24 * cw, y, 9 * cw, ch);
	add(G_BUTTON, SELECTABLE | RBUTTON, 0, (long)"As text", 8 + 34 * cw + 4, y, 9 * cw, ch);
	add(G_BUTTON, SELECTABLE | EXIT, 0, (long)"A long word in a button", 8 + 44 * cw, y, 8 * cw, ch);
	y = (short)(y + ch + 12);
	/* a choice, as EFTP's: a thin frame outside, the text on white */
	add(G_BOXTEXT, TOUCHEXIT, 0, tedi(" Ask                    ...", 0, 0x1180, -1), 8 + 3, y, 28 * cw, ch);
	add(G_BOXTEXT, TOUCHEXIT, 0, tedi("centred", 2, 0x1180, 1), 8 + 32 * cw, y, 12 * cw + 3, ch);
	add(G_BOXTEXT, 0, 0, tedi("black", 2, 0x1071, -2), 8 + 48 * cw + 6, y, 10 * cw, ch);
	y = (short)(y + ch + 12);
	/* ticks: on, and off, and selected; on a byte and off one */
	for (k = 0; k < 4; k++)
		add(G_IMAGE, TOUCHEXIT, k == 3 ? SELECTED : 0, (long)&tick_blk, (short)(8 + k * 4 * cw + (k & 1) * 3), y, 2 * cw,
		    ch);
	str(8 + 18 * cw, y, "after a tick");
	end();
}

/* the screen from (x, y), w by h, off it */
static void grab(short x, short y, short w, short h, unsigned char *to)
{
	short p[8], c[4] = { 0, 0, 0, 0 };
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > scr_w)
		w = (short)(scr_w - x);
	if (y + h > scr_h)
		h = (short)(scr_h - y);
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
	text_reclip(vdi_h);
}

/* a grey ground over the box, as each draw starts from */
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
	text_reclip(vdi_h);
}

/* object obj drawn by dlg_draw each way over the same ground, and
   compared */
static void one(short obj, const char *what)
{
	short x = tree[0].ob_x - 12, y = tree[0].ob_y - 12, w = tree[0].ob_width + 24, h = tree[0].ob_height + 24;
	long n = dlg_fast_count();
	size_t size;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x + w > scr_w)
		w = (short)(scr_w - x);
	if (y + h > scr_h)
		h = (short)(scr_h - y);
	size = (size_t)((w + 15) / 16) * 2 * h * planes;
	ground(x, y, w, h);
	dlg_fast_on = 0;
	dlg_draw(tree, obj);
	grab(x, y, w, h, buf_b);
	ground(x, y, w, h);
	dlg_fast_on = 1;
	dlg_draw(tree, obj);
	grab(x, y, w, h, buf_a);
	checks++;
	if (memcmp(buf_a, buf_b, size)) {
		size_t k;
		long first = -1, nb = 0;
		for (k = 0; k < size; k++)
			if (buf_a[k] != buf_b[k]) {
				if (first < 0)
					first = (long)k;
				nb++;
			}
		if (!differ) {
			/* the first: both off to the disk, for a look (run_cmp.py) */
			long f = Fcreate("DLC_DIFF.BIN", 0);
			if (f >= 0) {
				short hd[4];
				hd[0] = w;
				hd[1] = h;
				hd[2] = planes;
				hd[3] = obj;
				Fwrite((short)f, sizeof(hd), hd);
				Fwrite((short)f, (long)size, buf_b);
				Fwrite((short)f, (long)size, buf_a);
				Fclose((short)f);
			}
		}
		differ++;
		snprintf(line, sizeof(line), "DLC DIFFERS %s, object %d at %d,%d: %ld bytes, the first at %ld (row %ld)%s", what,
			 obj, tree[0].ob_x, tree[0].ob_y, nb, first, first / (((w + 15) / 16) * 2 * planes),
			 dlg_fast_count() > n ? "" : " (not the fast way)");
		say(line);
	}
}

static long fast_draws;

static void all(const char *what)
{
	static const short at[][2] = { { 32, 48 }, { 40, 49 }, { 61, 50 }, { 7, 23 } };
	short k, i;
	for (k = 0; k < 4; k++) {
		long n = dlg_fast_count();
		tree[0].ob_x = at[k][0];
		tree[0].ob_y = (short)(at[k][1] * (ch == 8 ? 1 : 1));
		if (tree[0].ob_x + tree[0].ob_width + 8 > scr_w || tree[0].ob_y + tree[0].ob_height + 8 > scr_h)
			continue;
		one(0, what);
		for (i = 1; i < nobj; i++)
			if (!(tree[i].ob_flags & HIDETREE))
				one(i, what);
		fast_draws += dlg_fast_count() - n;
	}
}

static void timed(void)
{
	short k, x, y;
	unsigned long t0, t1, t2;
	tree_settings();
	tree[0].ob_x = 96;
	tree[0].ob_y = (short)(scr_h > 300 ? 81 : 40);
	x = tree[0].ob_x - 3;
	y = tree[0].ob_y - 3;
	dlg_fast_on = 0;
	t0 = tos_ms();
	for (k = 0; k < 4; k++)
		dlg_draw(tree, 0);
	t1 = tos_ms();
	dlg_fast_on = 1;
	memset(dlg_fast_t, 0, sizeof(dlg_fast_t));
	memset(dlg_fast_kt, 0, sizeof(dlg_fast_kt));
	memset(dlg_fast_kn, 0, sizeof(dlg_fast_kn));
	for (k = 0; k < 4; k++)
		dlg_draw(tree, 0);
	t2 = tos_ms();
	snprintf(line, sizeof(line), "DLC time: the CPU's objects in 4 draws, ms/count: strings %lu/%ld, boxtexts %lu/%ld, boxes %lu/%ld, buttons %lu/%ld, images %lu/%ld",
		 dlg_fast_kt[1], dlg_fast_kn[1], dlg_fast_kt[2], dlg_fast_kn[2], dlg_fast_kt[4], dlg_fast_kn[4],
		 dlg_fast_kt[5], dlg_fast_kn[5], dlg_fast_kt[6], dlg_fast_kn[6]);
	say(line);
	(void)x;
	(void)y;
	snprintf(line, sizeof(line), "DLC time: Settings whole: objc_draw %lu ms, fast %lu ms (the walk %lu, the root's fill %lu, objc_draw %lu, the CPU's objects %lu)",
		 (t1 - t0) / 4, (t2 - t1) / 4, dlg_fast_t[0] / 4, dlg_fast_t[1] / 4, dlg_fast_t[2] / 4, dlg_fast_t[3] / 4);
	say(line);
	dlg_fast_on = 0;
	t0 = tos_ms();
	for (k = 0; k < 4; k++)
		dlg_draw(tree, 4);
	t1 = tos_ms();
	dlg_fast_on = 1;
	for (k = 0; k < 4; k++)
		dlg_draw(tree, 4);
	t2 = tos_ms();
	snprintf(line, sizeof(line), "DLC time: a tab (the fast way for every part): objc_draw %lu ms, fast %lu ms", (t1 - t0) / 4, (t2 - t1) / 4);
	say(line);
	{
		/* the CPU's pieces alone: 28 letters off a byte and on one, a
		   tab's box */
		static const char t[] = "When a file is already there";
		short p[4] = { 98, 113, 199, 128 }, c[4] = { 0, 0, 639, 399 };
		c[2] = scr_w - 1;
		c[3] = scr_h - 1;
		scr_clip(c);
		graf_mouse(M_OFF, 0);
		t0 = tos_ms();
		for (k = 0; k < 50; k++)
			scr_text(98, 113, t, 28, 1, ch, 0);
		t1 = tos_ms();
		for (k = 0; k < 50; k++)
			scr_text(96, 113, t, 28, 1, ch, 0);
		t2 = tos_ms();
		snprintf(line, sizeof(line), "DLC time: scr_text of 28 letters: off a byte %lu us, on one %lu us", (t1 - t0) * 20,
			 (t2 - t1) * 20);
		say(line);
		t0 = tos_ms();
		for (k = 0; k < 50; k++)
			scr_fill(vdi_h, p, 0);
		t1 = tos_ms();
		snprintf(line, sizeof(line), "DLC time: scr_fill of a tab's box %lu us", (t1 - t0) * 20);
		say(line);
		{
			short q[4];
			unsigned long u0, u1, u2, u3, u4;
			u0 = tos_ms();
			for (k = 0; k < 100; k++)
				(void)trap14_w(3);
			u1 = tos_ms();
			q[0] = 96; q[1] = 100; q[2] = 103; q[3] = 100;	/* 8 x 1 */
			for (k = 0; k < 100; k++)
				scr_fill(vdi_h, q, 0);
			u2 = tos_ms();
			q[2] = 551;		/* 456 x 1 */
			for (k = 0; k < 100; k++)
				scr_fill(vdi_h, q, 0);
			u3 = tos_ms();
			q[2] = 103; q[3] = 355;	/* 8 x 256 */
			for (k = 0; k < 10; k++)
				scr_fill(vdi_h, q, 0);
			u4 = tos_ms();
			snprintf(line, sizeof(line), "DLC time: Logbase %lu us; scr_fill 8x1 %lu us, 456x1 %lu us, 8x256 %lu us",
				 (u1 - u0) * 10, (u2 - u1) * 10, (u3 - u2) * 10, (u4 - u3) * 100);
			say(line);
			{
				/* the root's box: one fill, one span, and its middle band's
				   seven spans (outline, white, frame, inside, ...) */
				short x = 96, y = 80, w = 456, hh = 256, one[3], seven[21];
				short pp[4];
				pp[0] = x; pp[1] = y; pp[2] = x + w - 1; pp[3] = y + hh - 1;
				one[0] = x; one[1] = x + w - 1; one[2] = 0;
				seven[0] = x - 3; seven[1] = x - 3; seven[2] = 1;
				seven[3] = x - 2; seven[4] = x - 1; seven[5] = 0;
				seven[6] = x; seven[7] = x + 1; seven[8] = 1;
				seven[9] = x + 2; seven[10] = x + w - 3; seven[11] = 0;
				seven[12] = x + w - 2; seven[13] = x + w - 1; seven[14] = 1;
				seven[15] = x + w; seven[16] = x + w + 1; seven[17] = 0;
				seven[18] = x + w + 2; seven[19] = x + w + 2; seven[20] = 1;
				if (y + hh > scr_h)
					hh = (short)(scr_h - y), pp[3] = y + hh - 1;
				u0 = tos_ms();
				for (k = 0; k < 4; k++)
					scr_fill(vdi_h, pp, 0);
				u1 = tos_ms();
				for (k = 0; k < 4; k++)
					scr_spans(y, (short)(y + hh - 1), one, 1);
				u2 = tos_ms();
				for (k = 0; k < 4; k++)
					scr_spans(y, (short)(y + hh - 1), seven, 7);
				u3 = tos_ms();
				snprintf(line, sizeof(line), "DLC time: the root's box: scr_fill %lu ms, scr_spans of one span %lu ms, of seven %lu ms",
					 (u1 - u0) / 4, (u2 - u1) / 4, (u3 - u2) / 4);
				say(line);
			}
		}
		graf_mouse(M_ON, 0);
		scr_clip(0);
	}
}

/* the screen under a dialog kept and put back (dlg_under): each place,
   the same after; and its time for Settings' box */
static void under(void)
{
	static const short at[][4] = { { 93, 78, 462, 262 }, { 0, 0, 100, 40 }, { 17, 5, 33, 9 } };
	short k;
	unsigned long t0;
	for (k = 0; k < 3; k++) {
		short x = at[k][0], y = at[k][1], w = at[k][2], h = at[k][3], gx = x - 20, gy = y - 20;
		short gw = w + 40, gh = h + 40;
		size_t size;
		void *u;
		if (gx < 0)
			gx = 0;
		if (gy < 0)
			gy = 0;
		if (gx + gw > scr_w)
			gw = (short)(scr_w - gx);
		if (gy + gh > scr_h)
			gh = (short)(scr_h - gy);
		size = (size_t)((gw + 15) / 16) * 2 * gh * planes;
		ground(gx, gy, gw, gh);
		grab(gx, gy, gw, gh, buf_b);
		u = dlg_under(x, y, w, h);
		{
			short p[4] = { x, y, x + w - 1, y + h - 1 };
			vsf_interior(vdi_h, 1);
			vsf_color(vdi_h, 1);
			vr_recfl(vdi_h, p);
		}
		dlg_under_back(u);
		grab(gx, gy, gw, gh, buf_a);
		checks++;
		if (!u || memcmp(buf_a, buf_b, size)) {
			differ++;
			snprintf(line, sizeof(line), "DLC DIFFERS the screen under %d,%d %dx%d put back%s", x, y, w, h,
				 u ? "" : " (not kept)");
			say(line);
		}
	}
	{
		unsigned long t1, t2, t3, t4;
		long bpl;
		short pl, sw, shh, r;
		unsigned char *base = scr_at(&bpl, &pl, &sw, &shh), *m;
		t0 = tos_ms();
		for (k = 0; k < 4; k++)
			dlg_under_back(dlg_under(93, 78, 462, 262));
		t1 = tos_ms();
		dlg_fast_on = 0;
		for (k = 0; k < 4; k++)
			dlg_under_back(dlg_under(93, 78, 462, 262));
		dlg_fast_on = 1;
		t2 = tos_ms();
		for (k = 0; k < 4; k++)
			free(malloc(32L * 2 * 270));
		t3 = tos_ms();
		m = malloc(64L * 270);
		for (k = 0; k < 4 && base; k++)
			for (r = 0; r < 270 && 78 + r < shh; r++)	/* the screen's rows only */
				memcpy(m + r * 64L, base + (78 + r) * bpl + 10, 64);
		t4 = tos_ms();
		free(m);
		graf_mouse(M_OFF, 0);
		graf_mouse(M_ON, 0);
		t0 -= 0;
		snprintf(line, sizeof(line), "DLC time: the screen under Settings kept and put back: by the CPU %lu ms, the VDI's way %lu ms; malloc and free %lu ms, the rows one way %lu ms",
			 (t1 - t0) / 4, (t2 - t1) / 4, (t3 - t2) / 4, (t4 - t3) / 4);
		say(line);
		t0 = tos_ms();
		for (k = 0; k < 10; k++) {
			graf_mouse(M_OFF, 0);
			graf_mouse(M_ON, 0);
		}
		snprintf(line, sizeof(line), "DLC time: graf_mouse off and on %lu us", (tos_ms() - t0) * 100);
		say(line);
	}
}

int main(void)
{
	short wo[57], d, c[4];
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
	graf_mouse(256, 0);
	wind_update(1);

	/* text_plain on: some text drawn its way on white first */
	c[0] = 0;
	c[1] = 0;
	c[2] = scr_w - 1;
	c[3] = scr_h - 1;
	text_clip(vdi_h, c);
	vswr_mode(vdi_h, 1);
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, 0);
	vsf_perimeter(vdi_h, 0);
	{
		short p[4] = { 0, scr_h - 3 * ch, scr_w - 1, scr_h - 1 };
		vr_recfl(vdi_h, p);
	}
	vst_color(vdi_h, 1);
	text_plain(vdi_h, 16, scr_h - 2 * ch, "DLGCMP: dlg_draw against objc_draw", 34);
	snprintf(line, sizeof(line), "DLC screen %dx%d planes %d cell %dx%d, text_plain %d, 3D %d", scr_w, scr_h, planes,
		 cw, ch, text_plain_state(), dlg_3d_ok());
	say(line);

	{
		/* run_cmp.py --look: the AES's own drawing of the frames' tree
		   off to LOOK.BIN, for a look at its pixels, and no more */
		long q = Fopen("LOOK", 0);
		if (q >= 0) {
			short x, y, w, h;
			size_t size;
			long f;
			Fclose((short)q);
			tree_frames();
			tree[0].ob_x = 32;
			tree[0].ob_y = 24;
			x = 16;
			y = 8;
			w = (short)(tree[0].ob_width + 32);
			h = (short)(tree[0].ob_height + 32);
			if (y + h > scr_h)
				h = (short)(scr_h - y);
			size = (size_t)((w + 15) / 16) * 2 * h * planes;
			ground(x, y, w, h);
			dlg_fast_on = 0;
			objc_draw(tree, 0, 8, x, y, w, h);
			grab(x, y, w, h, buf_b);
			f = Fcreate("LOOK.BIN", 0);
			if (f >= 0) {
				short hd[4];
				hd[0] = w;
				hd[1] = h;
				hd[2] = planes;
				hd[3] = 0;
				Fwrite((short)f, sizeof(hd), hd);
				Fwrite((short)f, (long)size, buf_b);
				Fwrite((short)f, (long)size, buf_b);
				Fclose((short)f);
			}
			say("DLC look written");
			say("DLC done");
			wind_update(0);
			graf_mouse(257, 0);
			v_clsvwk(vdi_h);
			appl_exit();
			return 0;
		}
	}
	dlg_fast_part = 0;		/* every part the fast way, to check it */
	tree_settings();
	all("Settings");
	tree_cases();
	all("cases");
	tree_frames();
	all("frames");
	tree_black(0);
	all("black root");
	tree_black(1);
	all("hollow root");
	snprintf(line, sizeof(line), "DLC %d draws, %d differ; %ld of them and their objects the fast way", checks, differ,
		 fast_draws);
	say(line);
	under();
	timed();
	wind_update(0);
	graf_mouse(257, 0);
	/* a 68020 and later: the AES's way by design (scr_cpu_wins), checked
	   all the same */
	snprintf(line, sizeof(line), "DLC the fast way %s", scr_cpu_wins() ? "on (a 68000)" : "off (not a 68000: the AES's)");
	say(line);
	say(differ || (scr_cpu_wins() && !fast_draws) || (!scr_cpu_wins() && fast_draws) ? "DLC FAILED" : "DLC all the same");
	say("DLC done");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
