/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * offpage.c - a page drawn off the screen (offpage.h). The EdDI bindings
 * and the start-up test are ECal's (ui/win.c, its EdDI way); the logical
 * screen moved to a buffer, ECal's other way on plain TOS, isn't here:
 * EMail draws off the screen under NVDI only (Erez, 13 Nov 2026).
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "offpage.h"

static short scr_w, scr_h, planes;
static void *mem;		/* Mxalloc'd, NULL: none */
static MFDB bm_mfdb;
static short bm;		/* the bitmap's workstation, 0: none */
static const char *state = "not tried";

/* EdDI's off-screen bitmaps (VDI 100 and 101, sub-opcode 1), not in
   common/'s bindings */
static short bc[12], bin[20], bout[57], bpin[4], bpout[12];
static void *bpb[5] = { bc, bin, bpin, bout, bpout };

static void vdi_bm(void)
{
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
			 : : "g"((long)bpb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

static short v_opnbm(short screen, MFDB *m)
{
	short i;
	memset(bc, 0, sizeof(bc));
	for (i = 0; i < 10; i++)
		bin[i] = 1;
	bin[10] = 2;			/* raster coordinates */
	bin[11] = m->fd_w - 1;
	bin[12] = m->fd_h - 1;
	for (i = 13; i < 20; i++)
		bin[i] = 0;		/* the screen's pixel size and format */
	bc[0] = 100;
	bc[3] = 20;
	bc[5] = 1;
	bc[6] = screen;
	bc[7] = (short)((unsigned long)m >> 16);
	bc[8] = (short)((unsigned long)m & 0xffff);
	vdi_bm();
	return bc[6] != screen ? bc[6] : 0;
}

static void v_clsbm(short h)
{
	memset(bc, 0, sizeof(bc));
	bc[0] = 101;
	bc[5] = 1;
	bc[6] = h;
	vdi_bm();
}

/* a short black bar at the top left drawn with h: does it land in b? */
static int lands_in(unsigned char *b, short h)
{
	short i, pxy[4], changed = 0;
	for (i = 0; i < 64; i++)
		b[i] = 0x55;
	vs_clip(h, 0, pxy);
	vswr_mode(h, 1);
	vsf_interior(h, 1);
	vsf_color(h, 1);
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = 15;
	pxy[3] = 0;
	vr_recfl(h, pxy);
	for (i = 0; i < 2 * planes && i < 64; i++)
		changed |= b[i] != 0x55;
	return changed;
}

int offpage_init(short screen, short w, short hgt, short nplanes, long spare)
{
	long size, room;
	unsigned char *b;
	short h;
	if (bm)
		return 1;
	scr_w = w;
	scr_h = hgt;
	planes = nplanes;
	size = (long)((scr_w + 15) / 16) * 2 * planes * scr_h;
	if (!get_cookie(0x45644449L)) {			/* EdDI */
		state = "none: no EdDI";
		return 0;
	}
	/* only with `spare` bytes left beside it: a 4 MB Falcon in 256
	   colours gives 300 KB to it */
	room = (long)Mxalloc(-1L, 3);
	if (room < 0)
		room = (long)Malloc(-1L);
	if (planes < 1 || room < size + 256 + spare) {
		state = "none: too little memory";
		return 0;
	}
	mem = Mxalloc(size + 256, 3);
	if ((long)mem == -32 || !mem)
		mem = Malloc(size + 256);
	if ((long)mem <= 0) {
		mem = 0;
		state = "none: no memory";
		return 0;
	}
	b = (unsigned char *)(((unsigned long)mem + 255) & ~255UL);
	memset(&bm_mfdb, 0, sizeof(bm_mfdb));
	bm_mfdb.fd_addr = b;
	bm_mfdb.fd_w = (scr_w + 15) & ~15;
	bm_mfdb.fd_h = scr_h;
	bm_mfdb.fd_wdwidth = (scr_w + 15) / 16;
	bm_mfdb.fd_nplanes = planes;
	{
		MFDB m = bm_mfdb;
		h = v_opnbm(screen, &m);
	}
	if (h > 0 && lands_in(b, h)) {
		bm = h;
		state = "an EdDI bitmap";
		offpage_fonts();
		return 1;
	}
	if (h > 0)
		v_clsbm(h);
	Mfree(mem);
	mem = 0;
	state = "none: the bitmap's test failed";
	return 0;
}

short offpage_handle(void)
{
	return bm;
}

void offpage_fonts(void)
{
	static int loaded;
	if (bm && !loaded && vq_gdos()) {
		vst_load_fonts(bm, 0);
		loaded = 1;
	}
}

void offpage_copy(short h, short x, short y, short w, short hgt)
{
	MFDB scr;
	short pxy[8];
	if (!bm || w <= 0 || hgt <= 0)
		return;
	memset(&scr, 0, sizeof(scr));
	pxy[0] = pxy[4] = x;
	pxy[1] = pxy[5] = y;
	pxy[2] = pxy[6] = x + w - 1;
	pxy[3] = pxy[7] = y + hgt - 1;
	/* the VDI clips raster copies to the last clip, TOS's as NVDI's:
	   the piece's own */
	vs_clip(h, 1, pxy);
	vro_cpyfm(h, 3, pxy, &bm_mfdb, &scr);	/* S_ONLY */
}

void offpage_exit(void)
{
	if (bm)
		v_clsbm(bm);
	bm = 0;
	if (mem)
		Mfree(mem);
	mem = 0;
}

const char *offpage_state(void)
{
	return state;
}
