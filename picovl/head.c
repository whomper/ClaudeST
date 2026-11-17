/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * head.c - PIC.OVL's side of src/picovl.h: the PIC_OVL table at the start
 * of its text (ovl.ld), and the program's memory, clock and log through
 * the PIC_HOST table it was given. The rest is the family's own code:
 * src/pic.c's dither on the DSP (PIC_OVL_BUILD) and atari/dspjpeg.c.
 */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "pic.h"
#include "picovl.h"
#include "dspjpeg.h"

#ifndef PICOVL_VERSION
#define PICOVL_VERSION "0.0"
#endif

extern int mem_unaligned_ok;		/* libc.c */

static const PIC_HOST *H;

static int init(const PIC_HOST *h)
{
	if (!h || h->size < (long)sizeof(PIC_HOST))
		return 0;
	H = h;
	mem_unaligned_ok = 1;		/* a 68020 or later: the program checked */
	if (dspjpeg_present())
		pic_dsp = &dspjpeg_port;	/* (a TT: none, near_load and near_save only) */
	return 1;
}

/* ---- pal->near kept in a file (cache\\near.dat), for its palette ---- */

#define NEAR_N (32L * 32 * 32)
static const char magic[8] = "ECNEAR1";
static long known;		/* entries known when read or last written */

static long count(const PIC_PAL *pal)
{
	long i, k = 0;
	for (i = 0; i < NEAR_N; i++)
		k += pal->near[i] != 0;
	return k;
}

static void near_load(PIC_PAL *pal, const char *path, long key)
{
	char head[16];
	long sum = 0;
	int h;
	known = 0;
	if (!pal->near || !path[0] || (h = H->pf_open(path, 0)) < 0)
		return;
	if (H->pf_read(h, head, 16) == 16 && !memcmp(head, magic, 8)) {
		memcpy(&sum, head + 8, 4);
		if (sum == key && head[12] == (char)(pal->n >> 8) && head[13] == (char)pal->n &&
		    H->pf_read(h, pal->near, NEAR_N) == NEAR_N)
			known = count(pal);
		else
			memset(pal->near, 0, NEAR_N);	/* (another palette's, or cut short) */
	}
	H->pf_close(h);
}

static void near_save(PIC_PAL *pal, const char *path, long key)
{
	char head[16];
	long k;
	int h;
	if (!pal->near || pal->n <= 4 || !path[0] || (k = count(pal)) <= known)
		return;
	memset(head, 0, sizeof(head));
	memcpy(head, magic, 8);
	memcpy(head + 8, &key, 4);
	head[12] = (char)(pal->n >> 8);
	head[13] = (char)pal->n;
	if ((h = H->pf_open(path, 1)) < 0)
		return;
	if (H->pf_write(h, head, 16) == 16 && H->pf_write(h, pal->near, NEAR_N) == NEAR_N)
		known = k;
	H->pf_close(h);
}

static int dither(const PIC *p, PIC_PAL *pal, unsigned char *out, int (*poll)(void))
{
	int r;
	pic_poll = poll;
	r = pic_dither_dsp(p, pal, out);
	pic_poll = 0;
	return r;
}

static int dither_cells(const unsigned short *cells, int cw, int ch, int w, int h, int cx,
			const short *half, PIC_PAL *pal, unsigned char *out, unsigned char *used)
{
	PIC p;
	unsigned short *px;
	unsigned char *al;
	long i;
	int x, y, r;
	if (!(px = malloc((long)w * h * 3)))
		return 0;
	al = (unsigned char *)(px + (long)w * h);
	for (y = 0, i = 0; y < h; y++) {
		const unsigned short *src = cells + ((long)y * ch / h) * cw;
		int lo = half ? cx - half[y] : 0, hi = half ? cx + half[y] : w - 1;
		for (x = 0; x < w; x++, i++) {
			px[i] = src[(long)x * cw / w];
			al[i] = x < lo || x > hi ? 0 : 255;
		}
	}
	memset(&p, 0, sizeof(p));
	p.w = w;
	p.h = h;
	p.px = px;
	p.alpha = half ? al : 0;
	r = dither(&p, pal, out, 0);
	if (r)
		for (i = 0; i < (long)w * h; i++) {
			if (!al[i])
				out[i] = 255;
			else if (used)
				used[out[i]] = 1;
		}
	free(px);
	return r;
}

/* ---- an 8-bit chunky picture to an 8-plane screen (chunky8) ---- */

static unsigned short *b8;
static long b8cap, b8key = -1;
static int b8w, b8h;

static int chunky8(const unsigned char *pix, int w, int h, long key, short vdi,
		   int sx, int sy, int sw, int sh, int dx, int dy)
{
	/* VDI pen to the register its pixel holds (pens 16-254 their own) */
	static const unsigned char hw16[16] = { 0, 255, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
	int wpl = (w + 15) / 16, x, y, p, m;
	long plane = (long)wpl * h, n = plane * 8;
	unsigned short *std, *dev, *msk;
	PIC_MFDB s, d;
	short pxy[8];
	if (n * 3 > b8cap) {
		free(b8);
		b8cap = 0;
		b8key = -1;
		if (!(b8 = malloc(n * 3 * 2)) || (unsigned long)b8 + n * 6 > 0x1000000UL) {
			free(b8);
			b8 = 0;
			return 0;
		}
		b8cap = n * 3;
	}
	std = b8;
	dev = b8 + n;
	msk = b8 + 2 * n;
	memset(&s, 0, sizeof(s));
	s.fd_w = wpl * 16;
	s.fd_h = h;
	s.fd_wdwidth = wpl;
	s.fd_nplanes = 8;
	d = s;
	if (key != b8key || w != b8w || h != b8h) {
		s.fd_stand = 1;
		for (m = 0; m < 2; m++) {	/* the picture, then the mask */
			memset(std, 0, n * 2);
			for (y = 0; y < h; y++) {
				const unsigned char *in = pix + (long)y * w;
				for (x = 0; x < w; x++) {
					int v = in[x];
					unsigned short bit = (unsigned short)(0x8000U >> (x & 15)), *o;
					if (v == 255 || v == 0)
						continue;
					v = m ? 255 : v < 16 ? hw16[v] : v;
					o = std + (long)y * wpl + (x >> 4);
					for (p = 0; p < 8; p++, o += plane)
						if (v & (1 << p))
							*o |= bit;
				}
			}
			s.fd_addr = std;
			d.fd_addr = m ? msk : dev;
			d.fd_stand = 0;
			H->vr_trnfm(vdi, &s, &d);
		}
		b8key = key;
		b8w = w;
		b8h = h;
	}
	s.fd_stand = 0;
	pxy[0] = sx;
	pxy[1] = sy;
	pxy[2] = sx + sw - 1;
	pxy[3] = sy + sh - 1;
	pxy[4] = dx;
	pxy[5] = dy;
	pxy[6] = dx + sw - 1;
	pxy[7] = dy + sh - 1;
	memset(&d, 0, sizeof(d));	/* the screen */
	s.fd_addr = msk;
	H->vro_cpyfm(vdi, 4, pxy, &s, &d);	/* NOT S AND D */
	s.fd_addr = dev;
	H->vro_cpyfm(vdi, 7, pxy, &s, &d);	/* S OR D */
	return 1;
}

__attribute__((section(".picovl"), used))
const PIC_OVL picovl = {
	PICOVL_MAGIC, PICOVL_MAJOR, sizeof(PIC_OVL), PICOVL_VERSION,
	init, pic_dither_dsp_prepare, dither, pic_dither_state, near_load, near_save, dither_cells, chunky8
};

/* ---- the program's, through PIC_HOST ---- */

void *malloc(size_t n) { return H->malloc(n); }
void *calloc(size_t n, size_t m) { return H->calloc(n, m); }
void *realloc(void *p, size_t n) { return H->realloc(p, n); }
void free(void *p) { H->free(p); }
unsigned long pf_ms(void) { return H->pf_ms(); }
void conn_log(const char *who, const char *dir, const char *text, long n)
{
	H->conn_log(who, dir, text, n);
}
long tos_supexec(void *f) { return H->supexec(f); }
long get_cookie(long id) { return H->get_cookie(id); }
