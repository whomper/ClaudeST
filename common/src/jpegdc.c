/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * jpegdc.c - the JPEGs TJpgDec won't take, read at 1/8 of their size:
 * only each 8x8 block's average (its DC value), which is all a 64x64
 * picture of a photo needs, and which needs no IDCT. That covers
 * progressive JPEGs (their first scan has the averages of every block:
 * the rest is never read), the "extended" baseline (SOF1), any sampling
 * (4:4:0 too), any assignment of Huffman tables, and CMYK from Adobe's
 * programs.
 *
 * The averages are kept a byte each per component, then turned into
 * colours, one per 8x8 block of the full-size picture. A 1000x1000
 * photo: 16 K blocks, 24 KB with its colour.
 *
 * For a 68000: bytes read one at a time (never a word at an odd
 * address); a block costs a few bits read and one multiply by its
 * quantization step, the colours 16-bit multiplies.
 */
#include <stdlib.h>
#include <string.h>
#include "jpegdc.h"

typedef struct {
	unsigned char bits[17];		/* codes of each length */
	unsigned char val[256];
	long maxcode[18];
	int valptr[17];
	long mincode[17];
	int ok;
} HUFF;

typedef struct {
	int id, h, v, tq;
	int bw, bh;			/* its blocks across and down (whole MCUs) */
	unsigned char *dc;		/* the block averages, 0..255 */
	int done;			/* its DC values have been read */
	int pred;
	int td, ta;			/* the tables of the scan being read */
} COMP;

struct JDC {
	const unsigned char *p, *end;
	int W, H, ncomp, prog, hmax, vmax, mcux, mcuy, restart, adobe, transform;
	int q0[4];			/* each quantization table's DC step */
	HUFF dc[4], ac[4];
	COMP c[4];
	/* bits */
	unsigned long acc;
	int nbits, marker;
	long phantom;			/* zero bytes made up past the data */
	int why;
};

/* A photo cut short reads on in zeros, a few bytes' worth at most; one
   that only says it is 65535 x 65535 would have every block of it read
   from nothing, for minutes on a 68000: given up as damaged instead */
#define PHANTOM_MAX 4096L

/* two bytes, read one at a time: GCC would merge them into one word
   read, an address error on a 68000 when p is odd */
static int rd16(const unsigned char *p)
{
	const volatile unsigned char *v = p;
	int hi = v[0];
	return (hi << 8) | v[1];
}

static void build(HUFF *h)
{
	int l, k = 0;
	long code = 0;
	for (l = 1; l <= 16; l++) {
		h->valptr[l] = k;
		h->mincode[l] = code;
		code += h->bits[l];
		k += h->bits[l];
		h->maxcode[l] = h->bits[l] ? code - 1 : -1;
		code <<= 1;
	}
	h->maxcode[17] = 0x7FFFFFFFL;
	h->ok = 1;
}

/* ---- the entropy-coded data, a bit at a time ---- */

static int byte_in(JDC *j)
{
	int b;
	if (j->marker || j->p >= j->end) {
		j->phantom++;
		return 0;		/* past a marker: zeros, as the standard says */
	}
	b = *j->p++;
	if (b == 0xFF) {
		int n = j->p < j->end ? *j->p : 0;
		if (n == 0) {
			j->p++;		/* a stuffed FF */
		} else {
			j->marker = n;	/* RSTn, or the end of the scan */
			j->p--;
			return 0;
		}
	}
	return b;
}

static int getbit(JDC *j)
{
	if (!j->nbits) {
		j->acc = (unsigned long)byte_in(j);
		j->nbits = 8;
	}
	j->nbits--;
	return (int)((j->acc >> j->nbits) & 1);
}

static long getbits(JDC *j, int n)
{
	long v = 0;
	while (n--)
		v = (v << 1) | getbit(j);
	return v;
}

static int decode(JDC *j, const HUFF *h)
{
	long code = getbit(j);
	int l = 1;
	while (l <= 16 && code > h->maxcode[l]) {
		code = (code << 1) | getbit(j);
		l++;
	}
	if (l > 16)
		return -1;
	return h->val[h->valptr[l] + (int)(code - h->mincode[l])];
}

static int extend(long v, int s)
{
	if (!s)
		return 0;
	if (v < (1L << (s - 1)))
		v -= (1L << s) - 1;
	return (int)v;
}

/* to the next restart marker: the bits start again, the averages from 0 */
static int restart(JDC *j)
{
	int i;
	j->nbits = 0;
	if (!j->marker) {
		/* skip what's left of the interval up to the marker */
		while (j->p + 1 < j->end && !(j->p[0] == 0xFF && j->p[1] >= 0xD0 && j->p[1] <= 0xD7))
			j->p++;
		if (j->p + 1 >= j->end)
			return 0;
		j->marker = j->p[1];
	}
	if (j->marker < 0xD0 || j->marker > 0xD7)
		return 0;
	j->p += 2;
	j->marker = 0;
	for (i = 0; i < j->ncomp; i++)
		j->c[i].pred = 0;
	return 1;
}

/* one block: its DC value kept; a sequential scan's AC values skipped */
static int block(JDC *j, COMP *c, int bx, int by, int al)
{
	int s = decode(j, &j->dc[c->td]), k, v;
	if (s < 0 || s > 11 || j->phantom > PHANTOM_MAX)
		return 0;
	c->pred += extend(getbits(j, s), s);
	if (!j->prog) {
		const HUFF *a = &j->ac[c->ta];
		if (!a->ok)
			return 0;
		for (k = 1; k < 64;) {
			int rs = decode(j, a), r, z;
			if (rs < 0)
				return 0;
			r = rs >> 4;
			z = rs & 15;
			if (!z) {
				if (r != 15)
					break;	/* the end of the block */
				k += 16;
			} else {
				k += r;
				getbits(j, z);
				k++;
			}
		}
	}
	if (bx < c->bw && by < c->bh) {
		/* the average: DC times its step, over 8, around 128 */
		long d = (long)c->pred * (1L << al) * j->q0[c->tq];
		v = (int)(d >= 0 ? d >> 3 : -((-d) >> 3)) + 128;
		c->dc[(long)by * c->bw + bx] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
	}
	return 1;
}

int (*jdc_poll)(void);

static int scan(JDC *j, COMP **sc, int ns, int al)
{
	int mx, my, i, n = 0;
	if (ns == 1) {
		/* one component alone: its blocks in rows, as far as it reaches */
		COMP *c = sc[0];
		int bw = (int)((((long)j->W * c->h + j->hmax - 1) / j->hmax + 7) / 8);
		int bh = (int)((((long)j->H * c->v + j->vmax - 1) / j->vmax + 7) / 8);
		for (my = 0; my < bh; my++)
			for (mx = 0; mx < bw; mx++) {
				if (j->restart && n && n % j->restart == 0 && !restart(j))
					return 0;
				if (!block(j, c, mx, my, al))
					return 0;
				n++;
				if (jdc_poll && !(n & 7) && !jdc_poll())
					return 0;
			}
		return 1;
	}
	for (my = 0; my < j->mcuy; my++)
		for (mx = 0; mx < j->mcux; mx++) {
			if (j->restart && n && n % j->restart == 0 && !restart(j))
				return 0;
			for (i = 0; i < ns; i++) {
				COMP *c = sc[i];
				int x, y;
				for (y = 0; y < c->v; y++)
					for (x = 0; x < c->h; x++)
						if (!block(j, c, mx * c->h + x, my * c->v + y, al))
							return 0;
			}
			n++;
			if (jdc_poll && !(n & 7) && !jdc_poll())
				return 0;
		}
	return 1;
}

int jdc_why(const JDC *j)
{
	return j ? j->why : JW_MEMORY;
}

void jdc_free(JDC *j)
{
	int i;
	if (!j)
		return;
	for (i = 0; i < 4; i++)
		free(j->c[i].dc);
	free(j);
}

JDC *jdc_open(const unsigned char *p, long n, int *w, int *h, int *why)
{
	JDC *j = calloc(1, sizeof(JDC));
	const unsigned char *end = p + n;
	int i;
	*why = JW_MEMORY;
	if (!j)
		return 0;
	*why = JW_DAMAGED;
	if (n < 4 || p[0] != 0xFF || p[1] != 0xD8)
		goto bad;
	p += 2;
	while (p + 4 <= end) {
		int m, len;
		const unsigned char *s;
		if (p[0] != 0xFF) {
			p++;		/* stray bytes between segments */
			continue;
		}
		m = p[1];
		if (m == 0xFF) {
			p++;
			continue;
		}
		if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) {
			p += 2;
			continue;
		}
		if (m == 0xD9)
			break;
		len = rd16(p + 2);
		s = p + 4;
		if (len < 2 || s + len - 2 > end)
			goto bad;
		switch (m) {
		case 0xC0: case 0xC1: case 0xC2:
			if (j->ncomp)
				goto bad;		/* a second frame: not a JPEG we read */
			if (s[0] != 8) {
				*why = JW_UNSUPPORTED;	/* 12-bit */
				goto bad;
			}
			j->prog = m == 0xC2;
			j->H = rd16(s + 1);
			j->W = rd16(s + 3);
			j->ncomp = s[5];
			if (!j->W || !j->H || j->ncomp < 1 || j->ncomp > 4 || len < 8 + 3 * j->ncomp)
				goto bad;
			for (i = 0; i < j->ncomp; i++) {
				COMP *c = &j->c[i];
				c->id = s[6 + 3 * i];
				c->h = s[7 + 3 * i] >> 4;
				c->v = s[7 + 3 * i] & 15;
				c->tq = s[8 + 3 * i] & 3;
				if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4)
					goto bad;
				if (c->h > j->hmax)
					j->hmax = c->h;
				if (c->v > j->vmax)
					j->vmax = c->v;
			}
			j->mcux = (j->W + 8 * j->hmax - 1) / (8 * j->hmax);
			j->mcuy = (j->H + 8 * j->vmax - 1) / (8 * j->vmax);
			for (i = 0; i < j->ncomp; i++) {
				COMP *c = &j->c[i];
				c->bw = j->mcux * c->h;
				c->bh = j->mcuy * c->v;
				c->dc = malloc((size_t)c->bw * c->bh);
				if (!c->dc) {
					*why = JW_MEMORY;
					goto bad;
				}
				memset(c->dc, 128, (size_t)c->bw * c->bh);
			}
			break;
		case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB:
		case 0xCD: case 0xCE: case 0xCF:
			*why = JW_UNSUPPORTED;		/* lossless, hierarchical, arithmetic */
			goto bad;
		case 0xDB: {
			const unsigned char *q = s, *qe = s + len - 2;
			while (q + 3 <= qe) {		/* q[1], q[2] are read */
				int pq = q[0] >> 4, tq = q[0] & 3;
				j->q0[tq] = pq ? rd16(q + 1) : q[1];
				q += 1 + (pq ? 128 : 64);
			}
			break;
		}
		case 0xC4: {
			const unsigned char *q = s, *qe = s + len - 2;
			while (q + 17 <= qe) {
				HUFF *hf = (q[0] >> 4) ? &j->ac[q[0] & 3] : &j->dc[q[0] & 3];
				int k, tot = 0;
				hf->bits[0] = 0;
				for (k = 1; k <= 16; k++)
					tot += hf->bits[k] = q[k];
				if (tot > 256 || q + 17 + tot > qe)
					goto bad;
				memcpy(hf->val, q + 17, tot);
				build(hf);
				q += 17 + tot;
			}
			break;
		}
		case 0xDD:
			j->restart = rd16(s);
			break;
		case 0xEE:
			if (len >= 14 && !memcmp(s, "Adobe", 5)) {
				j->adobe = 1;
				j->transform = s[11];
			}
			break;
		case 0xDA:
			/* the first scan: here the reading starts (jdc_read) */
			if (!j->ncomp)
				goto bad;
			j->p = p;
			j->end = end;
			*w = j->W;
			*h = j->H;
			*why = JW_OK;
			return j;
		}
		p = s + len - 2;
	}
bad:
	jdc_free(j);
	return 0;
}

/* each scan that has DC values for the first time, until every
   component has them; a progressive JPEG's refinements are passed by */
int jdc_read(JDC *j)
{
	const unsigned char *p = j->p;
	int left = j->ncomp;
	j->why = JW_DAMAGED;
	while (p + 4 <= j->end && left > 0) {
		int m, len, ns, i, ss, ah, al;
		const unsigned char *s;
		COMP *sc[4];
		if (p[0] != 0xFF || p[1] == 0xFF || (p[1] >= 0xD0 && p[1] <= 0xD7) || p[1] == 0x00) {
			p++;		/* the data of a scan passed by */
			continue;
		}
		m = p[1];
		if (m == 0xD9)
			break;
		len = rd16(p + 2);
		s = p + 4;
		if (len < 2 || s + len - 2 > j->end)
			return 0;
		if (m == 0xC4) {
			/* tables can change between scans */
			const unsigned char *q = s, *qe = s + len - 2;
			while (q + 17 <= qe) {
				HUFF *hf = (q[0] >> 4) ? &j->ac[q[0] & 3] : &j->dc[q[0] & 3];
				int k, tot = 0;
				for (k = 1; k <= 16; k++)
					tot += hf->bits[k] = q[k];
				if (tot > 256 || q + 17 + tot > qe)
					return 0;
				memcpy(hf->val, q + 17, tot);
				build(hf);
				q += 17 + tot;
			}
		} else if (m == 0xDD) {
			j->restart = rd16(s);
		} else if (m == 0xDA) {
			ns = s[0];
			if (ns < 1 || ns > 4 || len < 6 + 2 * ns)
				return 0;
			for (i = 0; i < ns; i++) {
				int k;
				sc[i] = 0;
				for (k = 0; k < j->ncomp; k++)
					if (j->c[k].id == s[1 + 2 * i])
						sc[i] = &j->c[k];
				if (!sc[i])
					return 0;
				sc[i]->td = s[2 + 2 * i] >> 4 & 3;
				sc[i]->ta = s[2 + 2 * i] & 3;
			}
			ss = s[1 + 2 * ns];
			ah = s[3 + 2 * ns] >> 4;
			al = s[3 + 2 * ns] & 15;
			p = s + len - 2;
			/* a scan with DC values not read yet: read it */
			if (ss == 0 && ah == 0) {
				int fresh = 0;
				for (i = 0; i < ns; i++)
					if (!sc[i]->done)
						fresh = 1;
				if (fresh) {
					for (i = 0; i < ns; i++) {
						if (!j->dc[sc[i]->td].ok)
							return 0;
						sc[i]->pred = 0;
					}
					j->p = p;
					j->nbits = 0;
					j->marker = 0;
					if (!scan(j, sc, ns, j->prog ? al : 0))
						return 0;
					for (i = 0; i < ns; i++)
						if (!sc[i]->done) {
							sc[i]->done = 1;
							left--;
						}
					p = j->p;
				}
			}
			continue;
		}
		p = s + len - 2;
	}
	if (left > 0)
		return 0;
	j->why = JW_OK;
	return 1;
}

/* a block's colour (bx, by count 8x8 blocks of the full-size picture) */
void jdc_rgb(const JDC *j, int bx, int by, int *r, int *g, int *b)
{
	int v[4], i;
	for (i = 0; i < j->ncomp; i++) {
		const COMP *c = &j->c[i];
		int x = (int)((long)bx * c->h / j->hmax), y = (int)((long)by * c->v / j->vmax);
		if (x >= c->bw)
			x = c->bw - 1;
		if (y >= c->bh)
			y = c->bh - 1;
		v[i] = c->dc[(long)y * c->bw + x];
	}
	if (j->ncomp < 3) {
		*r = *g = *b = v[0];
		return;
	}
	if (j->ncomp == 3 && j->adobe && j->transform == 0) {
		*r = v[0];
		*g = v[1];
		*b = v[2];
		return;
	}
	{
		/* Y Cb Cr: 1.402, 0.344, 0.714 and 1.772 in 256ths, 16-bit products */
		short y0 = (short)v[0], cb = (short)(v[1] - 128), cr = (short)(v[2] - 128);
		int rr = y0 + ((short)359 * cr >> 8);
		int gg = y0 - (((short)88 * cb + (short)183 * cr) >> 8);
		int bb = y0 + ((short)454 * cb >> 8);
		rr = rr < 0 ? 0 : rr > 255 ? 255 : rr;
		gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
		bb = bb < 0 ? 0 : bb > 255 ? 255 : bb;
		if (j->ncomp == 4) {
			/* Adobe's CMYK (or YCCK) is stored inverted: the darkness K over it */
			short k = (short)v[3];
			if (!j->adobe || j->transform == 0) {
				rr = v[0];
				gg = v[1];
				bb = v[2];
			} else {
				/* YCCK: the colours come out as cyan, magenta, yellow */
				rr = 255 - rr;
				gg = 255 - gg;
				bb = 255 - bb;
			}
			rr = ((short)rr * k) >> 8;
			gg = ((short)gg * k) >> 8;
			bb = ((short)bb * k) >> 8;
		}
		*r = rr;
		*g = gg;
		*b = bb;
	}
}
