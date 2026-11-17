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
static signed char state;	/* 0 not tried, 1 loaded, 2 the DSP's, 3 the 68030's, -1 none */

static long supexec(void *f) { return tos_supexec(f); }

static const PIC_HOST host = {
	sizeof(PIC_HOST),
	malloc, calloc, realloc, free,
	pf_ms, conn_log, supexec, get_cookie,
	pf_open, pf_read, pf_write, pf_close,
	(void (*)(short, PIC_MFDB *, PIC_MFDB *))vr_trnfm,
	(void (*)(short, short, const short *, PIC_MFDB *, PIC_MFDB *))vro_cpyfm
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
	    !(image = ovl_load(path, &taken, err, sizeof(err))))
		return;
	ovl_flush();			/* its code was written as data */
	o = image;
	if (o->magic != PICOVL_MAGIC || o->major != PICOVL_MAJOR ||
	    o->size < (short)sizeof(PIC_OVL) || !o->init(&host)) {
		free(image);
		image = 0;
		return;
	}
	ov = o;
	state = 1;
	conn_log("Photo", " -- ", "PIC.OVL loaded", 14);
}

int pic_ovl_step(PIC_PAL *pal, const char *near, long key)
{
	if (state == 0) {
		load();
		return state > 0 ? -1 : 0;
	}
	if (state == 1) {
		ov->near_load(pal, near, key);
		state = ov->prepare(pal) ? 2 : 3;
		return -1;
	}
	return state == 2;
}

int pic_ovl_cells(const unsigned short *cells, int cw, int ch, int w, int h, int cx,
		  const short *half, PIC_PAL *pal, unsigned char *out, unsigned char *used)
{
	return state == 2 && ov->dither_cells(cells, cw, ch, w, h, cx, half, pal, out, used);
}

int pic_ovl_chunky8(const unsigned char *pix, int w, int h, long key, short vdi,
		    int sx, int sy, int sw, int sh, int dx, int dy)
{
	return ov && ov->chunky8(pix, w, h, key, vdi, sx, sy, sw, sh, dx, dy);
}

void pic_ovl_palette(PIC_PAL *pal, const char *near, long key)
{
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
	ov = 0;
	free(image);
	image = 0;
	state = 0;
}
