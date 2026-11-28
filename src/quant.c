/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * quant.c - a median cut of a picture's 5-6-5 pixels (quant.h).
 *
 * The pixels are copied once and each box sorted along its widest side by
 * counting (32 or 64 values), so no qsort and no 32-bit multiply per pixel.
 * The sort goes from one of two buffers into the other at the same place,
 * so nothing is copied back (each box knows which buffer holds it); each
 * side has a loop of its own, and a box's span is measured with plain
 * locals. A job (quant_begin, quant_step, quant_end) does the work a
 * budget of pixel visits at a time, so a caller's turn stays short.
 *
 * The colours are those of the code before the job, value for value: the
 * same stable counting sort, the same medians and means.
 */
#include <stdlib.h>
#include "quant.h"

#define R5(v) ((v) >> 11)
#define G6(v) (((v) >> 5) & 63)
#define B5(v) ((v) & 31)

typedef struct {
	long start, len;
	unsigned char side;		/* 0 R, 1 G, 2 B: the widest, scaled to 6 bits */
	unsigned char in;		/* which buffer holds its pixels: 0 or 1 */
	short width;			/* its span on that side, 6 bits */
} BOX;

enum { Q_COPY, Q_CUT, Q_MEAN, Q_DONE };

struct QUANT_JOB {
	const unsigned short *src;
	long n, skip, at, m;
	unsigned short *buf[2];
	BOX *box;
	int want, nb, phase, k;
};

/* the box's widest side and its span (R and B doubled to G's 6 bits) */
static void measure(const unsigned short *p, BOX *b)
{
	const unsigned short *q = p + b->start, *e = q + b->len;
	unsigned rl = 31, rh = 0, gl = 63, gh = 0, bl = 31, bh = 0;
	while (q < e) {
		unsigned v = *q++, c;
		c = v >> 11;
		if (c < rl)
			rl = c;
		if (c > rh)
			rh = c;
		c = (v >> 5) & 63;
		if (c < gl)
			gl = c;
		if (c > gh)
			gh = c;
		c = v & 31;
		if (c < bl)
			bl = c;
		if (c > bh)
			bh = c;
	}
	/* G first, then R, then B, each only when wider */
	b->side = 1;
	b->width = (short) (gh - gl);
	if ((int) ((rh - rl) << 1) > b->width) {
		b->width = (short) ((rh - rl) << 1);
		b->side = 0;
	}
	if ((int) ((bh - bl) << 1) > b->width) {
		b->width = (short) ((bh - bl) << 1);
		b->side = 2;
	}
}

/* one side's stable counting sort from q to t, len pixels */
#define SORT_BY(CH, M)							\
	do {								\
		for (i = 0; i < (M); i++)				\
			count[i] = 0;					\
		for (i = 0; i < len; i++)				\
			count[CH(q[i])]++;				\
		for (i = 0, at = 0; i < (M); i++) {			\
			long c = count[i];				\
			count[i] = at;					\
			at += c;					\
		}							\
		for (i = 0; i < len; i++) {				\
			unsigned v = q[i];				\
			t[count[CH(v)]++] = (unsigned short) v;		\
		}							\
	} while (0)

/* the box's pixels sorted along its side into the other buffer */
static void sort_box(QUANT_JOB *j, BOX *b)
{
	long count[64], i, at, len = b->len;
	const unsigned short *q = j->buf[b->in] + b->start;
	unsigned short *t = j->buf[b->in ^ 1] + b->start;
	if (b->side == 0)
		SORT_BY(R5, 32);
	else if (b->side == 1)
		SORT_BY(G6, 64);
	else
		SORT_BY(B5, 32);
	b->in ^= 1;
}

static int chan(unsigned v, int side)
{
	return side == 0 ? (int) R5(v) : side == 1 ? (int) G6(v) : (int) B5(v);
}

/* the box to cut: the most pixels times its width; -1 none */
static int pick(const QUANT_JOB *j)
{
	int best = -1, k;
	long score = 0;
	for (k = 0; k < j->nb; k++) {
		const BOX *b = &j->box[k];
		long s = b->width > 0 && b->len > 1 ? b->len * b->width : 0;
		if (s > score) {
			score = s;
			best = k;
		}
	}
	return best;
}

/* one cut: the box sorted and halved at the median, moved off a run of
   one value so both halves differ; its cost in pixel visits */
static long cut(QUANT_JOB *j, int best)
{
	BOX *b = &j->box[best], *nw;
	const unsigned short *p;
	long half, len = b->len;
	int c;
	sort_box(j, b);
	p = j->buf[b->in] + b->start;
	half = len >> 1;
	c = chan(p[half], b->side);
	while (half > 0 && chan(p[half - 1], b->side) == c)
		half--;
	if (half == 0) {
		half = len >> 1;
		while (half < len && chan(p[half], b->side) == c)
			half++;
	}
	nw = &j->box[j->nb++];
	nw->start = b->start + half;
	nw->len = len - half;
	nw->in = b->in;
	b->len = half;
	measure(j->buf[b->in], b);
	measure(j->buf[nw->in], nw);
	return 3 * len;
}

/* the box's mean, 8 bits a side, to VDI's 0-1000 */
static void mean(const QUANT_JOB *j, const BOX *b, short *rgb)
{
	unsigned long r = 0, g = 0, bl = 0, len = (unsigned long) b->len;
	const unsigned short *q = j->buf[b->in] + b->start, *e = q + b->len;
	while (q < e) {
		unsigned v = *q++;
		r += v >> 11;
		g += (v >> 5) & 63;
		bl += v & 31;
	}
	rgb[0] = (short) ((r * 1000 + len * 15) / (len * 31));
	rgb[1] = (short) ((g * 1000 + len * 31) / (len * 63));
	rgb[2] = (short) ((bl * 1000 + len * 15) / (len * 31));
}

QUANT_JOB *quant_begin(const unsigned short *px, long n, long skip, int want)
{
	QUANT_JOB *j;
	if (want < 1 || n <= 0)
		return NULL;
	if (want > 256)
		want = 256;
	j = malloc(sizeof *j);
	if (j == NULL)
		return NULL;
	j->buf[0] = malloc((size_t) n * sizeof *j->buf[0]);
	j->buf[1] = malloc((size_t) n * sizeof *j->buf[1]);
	j->box = malloc((size_t) want * sizeof *j->box);
	if (j->buf[0] == NULL || j->buf[1] == NULL || j->box == NULL) {
		quant_end(j);
		return NULL;
	}
	j->src = px;
	j->n = n;
	j->skip = skip;
	j->want = want;
	j->at = j->m = 0;
	j->nb = 0;
	j->k = 0;
	j->phase = Q_COPY;
	return j;
}

int quant_step(QUANT_JOB *j, long budget, short rgb[][3])
{
	long spent = 0;
	if (j == NULL)
		return 0;
	while (spent < budget && j->phase != Q_DONE) {
		switch (j->phase) {
		case Q_COPY: {
			long e = j->at + (budget - spent);
			const unsigned short *s = j->src;
			unsigned short *p = j->buf[0];
			if (e > j->n)
				e = j->n;
			spent += e - j->at;
			for (; j->at < e; j->at++)
				if ((long) s[j->at] != j->skip)
					p[j->m++] = s[j->at];
			if (j->at < j->n)
				break;
			if (j->m == 0) {
				j->phase = Q_DONE;
				break;
			}
			j->box[0].start = 0;
			j->box[0].len = j->m;
			j->box[0].in = 0;
			measure(j->buf[0], &j->box[0]);
			spent += j->m;
			j->nb = 1;
			j->phase = Q_CUT;
			break;
		}
		case Q_CUT: {
			int best = j->nb < j->want ? pick(j) : -1;
			if (best < 0) {
				j->phase = Q_MEAN;
				j->k = 0;
				break;
			}
			spent += cut(j, best);
			break;
		}
		default:	/* Q_MEAN */
			if (j->k >= j->nb) {
				j->phase = Q_DONE;
				break;
			}
			mean(j, &j->box[j->k], rgb[j->k]);
			spent += j->box[j->k].len;
			j->k++;
			break;
		}
	}
	return j->phase == Q_DONE ? j->nb : -1;
}

void quant_end(QUANT_JOB *j)
{
	if (j == NULL)
		return;
	free(j->buf[0]);
	free(j->buf[1]);
	free(j->box);
	free(j);
}

int quant_565(const unsigned short *px, long n, long skip, int want, short rgb[][3])
{
	QUANT_JOB *j = quant_begin(px, n, skip, want);
	int r;
	if (j == NULL)
		return 0;
	while ((r = quant_step(j, 1L << 30, rgb)) < 0)
		;
	quant_end(j);
	return r;
}
