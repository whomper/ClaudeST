/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic_ovl.c - the program's side of PIC.OVL (src/picovl.h): pictures on
 * the Falcon's DSP, and the table of nearest pens kept in a file. Loaded
 * on a 68020 or later (a TT too: the table without the DSP) into the
 * program's memory (ovl.c, as TLS.OVL), a step a turn (pic_ovl_step): the
 * file, then the table and the DSP's check. Tried once: without the file
 * or its memory the program dithers on the 68030 as it did. Small, as
 * every program carries it, an ST's too.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "plat.h"
#include "conn.h"
#include "util.h"
#include "ovl.h"
#include "picovl.h"
#include "pic_ovl.h"
#include "tlsovl.h"	/* tls_ovl_path */

static const PIC_OVL *ov;
static void *image;
static signed char state;	/* 0 not tried, 1 loaded, 2 the DSP's, 3 the 68030's, -1 none,
				   4 its table read, the check in steps (1.2's prepare_step) */

/* a member of 1.2's or later, when this PIC.OVL has it (an older one is
   loaded too: its one-shot calls then) */
#define HAS(m) (ov->size >= (short)(offsetof(PIC_OVL, m) + sizeof(ov->m)))

static long supexec(void *f) { return tos_supexec(f); }

static const PIC_HOST host = {
	sizeof(PIC_HOST),
	malloc, calloc, realloc, free,
	pf_ms, conn_log, supexec, get_cookie,
	pf_open, pf_read, pf_write, pf_close,
	(void (*)(short, PIC_MFDB *, PIC_MFDB *))vr_trnfm,
	(void (*)(short, short, const short *, PIC_MFDB *, PIC_MFDB *))vro_cpyfm,
	vs_color, vq_color
};

static void load(void)
{
	const PIC_OVL *o;
	long taken;
	char err[80], path[200], *p;
	state = -1;
	/* beside TLS.OVL (tls_ovl_path's folder) */
	str_copy(path, tls_ovl_path, sizeof(path) - 8);
	p = path + strlen(path);
	while (p > path && p[-1] != '\\' && p[-1] != '/')
		p--;
	strcpy(p, "pic.ovl");
	/* (_CPU: an ST never loads it; nor a FireBee, _CF_, whose FireTOS
	   says _CPU 60: PIC.OVL is 68030 code with a DSP part, and a ColdFire
	   has no DSP and not all of the 68030's set; the 68000 path draws the
	   same pictures there, as on an ST. EContact, 15 Nov 2026) */
	if (get_cookie(0x5F435055L) < 20 || get_cookie(0x5F43465FL) ||
	    !(image = ovl_load_aligned(path, &taken, err, sizeof(err))))
		return;
	ovl_flush();			/* its code was written as data */
	o = image;
	if (o->magic != PICOVL_MAGIC || o->major != PICOVL_MAJOR ||
	    o->size < (short)offsetof(PIC_OVL, prepare_step) || !o->init(&host)) {
		ovl_free(image);
		image = 0;
		return;
	}
	ov = o;
	state = 1;
	conn_log("Photo", " -- ", "PIC.OVL loaded", 14);
}

/* pal's share to the overlay: 1 the DSP will dither with it; 0 not (an
   overlay older than 1.3 knows only all of it: a soft dither stays the
   68030's, the same pens) */
static int share(const PIC_PAL *pal)
{
	int s = pal->soft ? pal->soft : 16;
	if (HAS(soft)) {
		ov->soft(s);
		return 1;
	}
	return s == 16;
}

int pic_ovl_step(PIC_PAL *pal, const char *near, long key)
{
	if (state == 0) {
		load();
		return state > 0 ? -1 : 0;
	}
	if (state == 1) {
		ov->near_load(pal, near, key);
		if (HAS(prepare_step)) {
			state = 4;	/* (the table's read a step of its own) */
			return -1;
		}
		state = ov->prepare(pal) ? 2 : 3;
		return -1;
	}
	if (state == 4) {
		int r;
		share(pal);
		r = ov->prepare_step(pal);
		if (r >= 0)
			state = r ? 2 : 3;
		return -1;
	}
	return state == 2;
}

int pic_ovl_cells(const unsigned short *cells, int cw, int ch, int w, int h, int cx,
		  const short *half, PIC_PAL *pal, unsigned char *out, unsigned char *used)
{
	return state == 2 && share(pal) && ov->dither_cells(cells, cw, ch, w, h, cx, half, pal, out, used);
}

int pic_ovl_chunky8(const unsigned char *pix, int w, int h, long key, short vdi,
		    int sx, int sy, int sw, int sh, int dx, int dy)
{
	return ov && ov->chunky8(pix, w, h, key, vdi, sx, sy, sw, sh, dx, dy);
}

void pic_ovl_palette(PIC_PAL *pal, const char *near, long key)
{
	if (ov && state == 4)
		ov->prepare_step(0);	/* (a check under way begins again, for these pens) */
	if (ov)
		ov->near_load(pal, near, key);
}

void pic_ovl_near_save(PIC_PAL *pal, const char *near, long key)
{
	if (ov)
		ov->near_save(pal, near, key);
}

void pic_ovl_unload(void)
{
	pic_ovl_pens_give();
	if (ov && HAS(chunky8_prepare)) {
		if (state == 4)
			ov->prepare_step(0);		/* (what the check holds) */
		ov->chunky8_prepare(0, 0, 0, 0, 0);	/* (the copies' memory) */
	}
	ov = 0;
	ovl_free(image);
	image = 0;
	state = 0;
}

/* chunky8's copies made for key before it draws, a step a call (pix kept
   until done): 1 ready, -1 call again, 0 not (no PIC.OVL, or no room).
   An older PIC.OVL without the steps: 1, chunky8 makes them as before */
int pic_ovl_chunky8_prepare(const unsigned char *pix, int w, int h, long key, short vdi)
{
	if (!ov)
		return 0;
	return HAS(chunky8_prepare) ? ov->chunky8_prepare(pix, w, h, key, vdi) : 1;
}

/* 1.4: a picture's own colours in pens 192-255 (the guidelines 5.2). The
   VDI handle is the one pens_take was given, for the give back */
static short pens_vdi = -1;

int pic_ovl_quant(const unsigned short *px, long n, long skip, int want, short rgb[][3])
{
	return ov && HAS(quant) ? ov->quant(px, n, skip, want, rgb) : 0;
}

/* 1.5: the same as a job. With PIC.OVL 1.4, begin gives NULL: the
   program calls pic_ovl_quant then, or keeps the screen's palette */
void *pic_ovl_quant_begin(const unsigned short *px, long n, long skip, int want)
{
	return ov && HAS(quant_end) ? ov->quant_begin(px, n, skip, want) : NULL;
}

int pic_ovl_quant_step(void *job, long budget, short rgb[][3])
{
	return ov && job && HAS(quant_end) ? ov->quant_step(job, budget, rgb) : 0;
}

void pic_ovl_quant_end(void *job)
{
	if (ov && job && HAS(quant_end))
		ov->quant_end(job);
}

int pic_ovl_pens_take(short vdi, int n, const short rgb[][3])
{
	if (!ov || !HAS(pens_take) || !ov->pens_take(vdi, n, rgb))
		return 0;
	pens_vdi = vdi;
	return 1;
}

void pic_ovl_pens_give(void)
{
	if (ov && HAS(pens_give) && pens_vdi >= 0)
		ov->pens_give(pens_vdi);
	pens_vdi = -1;
}

int pic_ovl_pens_held(void)
{
	return pens_vdi >= 0;
}
