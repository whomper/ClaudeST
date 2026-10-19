/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * model.c - what dsp/ec.a56 does, in C: the same commands and the same
 * numbers (unreduced as the DSP leaves them), so src/ecdsp.c can be
 * tested on the build machine. sim.c runs the real program.
 */
#include <string.h>
#include <stdint.h>
#include "model.h"

#define DMASK 0x7fffffUL

static int n;
static uint32_t mp, p[24], p2[24], reg[128][32];
static int want, wreg, wpos;		/* words still expected, and where they go */
static uint32_t outq[32];
static int outn, outpos;
unsigned long model_ops[8];		/* commands of each kind */

/* d = a + s * b (s = 1 or -1); the carry out (0, 1 or -1) */
static int addsub(uint32_t *d, const uint32_t *a, const uint32_t *b, int s)
{
	int64_t c = 0;
	int i;
	for (i = 0; i < n; i++) {
		int64_t v = (int64_t)a[i] + s * (int64_t)b[i] + c;
		d[i] = (uint32_t)(v & DMASK);
		c = v >> 23;
	}
	return (int)c;
}

static void mont(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint64_t t[50];
	int i, j;
	memset(t, 0, sizeof(t));
	for (i = 0; i < n; i++) {		/* CIOS, digit by digit */
		uint64_t c = 0, m;
		for (j = 0; j < n; j++) {
			c += t[j] + (uint64_t)a[j] * b[i];
			t[j] = c & DMASK;
			c >>= 23;
		}
		t[n] += c;
		m = ((t[0] * mp) & DMASK);
		c = (t[0] + m * p[0]) >> 23;
		for (j = 1; j < n; j++) {
			c += t[j] + m * p[j];
			t[j - 1] = c & DMASK;
			c >>= 23;
		}
		c += t[n];
		t[n - 1] = c & DMASK;
		t[n] = c >> 23;
	}
	for (i = 0; i < n; i++)
		d[i] = (uint32_t)t[i];
}

static const uint32_t *canon(const uint32_t *a, uint32_t *t)
{
	return addsub(t, a, p, -1) ? a : t;
}

void model_reset(void)
{
	want = 0;
	outn = outpos = 0;
}

void model_put(unsigned long w)
{
	int o, d, a, b, i;
	uint32_t t[32], u[32];
	if (want) {
		if (wreg == -2) {		/* CFG: N, mp, p */
			if (wpos == 0)
				n = (int)w, want = n + 2;
			else if (wpos == 1)
				mp = (uint32_t)w;
			else
				p[wpos - 2] = (uint32_t)w;
			wpos++;
			if (!--want) {
				memset(reg, 0, sizeof(reg));
				addsub(p2, p, p, 1);
			}
		} else {
			reg[wreg][wpos++] = (uint32_t)w;
			want--;
		}
		return;
	}
	o = (int)(w >> 21) & 7;
	d = (int)(w >> 14) & 127;
	a = (int)(w >> 7) & 127;
	b = (int)w & 127;
	model_ops[o]++;
	switch (o) {
	case 1:
		want = 3;		/* until N is known */
		wreg = -2;
		wpos = 0;
		break;
	case 2:
		want = n;
		wreg = d;
		wpos = 0;
		break;
	case 3:
		mont(t, reg[a], reg[b]);
		memcpy(reg[d], t, n * 4);
		break;
	case 4:
		addsub(t, reg[a], reg[b], 1);
		memcpy(reg[d], addsub(u, t, p2, -1) ? t : u, n * 4);
		break;
	case 5:
		if (addsub(t, reg[a], reg[b], -1)) {
			addsub(u, t, p2, 1);
			memcpy(reg[d], u, n * 4);
		} else
			memcpy(reg[d], t, n * 4);
		break;
	case 6: {
		const uint32_t *c = canon(reg[a], t);
		for (i = 0; i < n; i++)
			outq[i] = c[i];
		outn = n;
		outpos = 0;
		break;
	}
	case 7: {
		const uint32_t *c = canon(reg[a], t);
		uint32_t z = 0;
		for (i = 0; i < n; i++)
			z |= c[i];
		outq[0] = !z;
		outn = 1;
		outpos = 0;
		break;
	}
	}
}

unsigned long model_get(void)
{
	return outpos < outn ? outq[outpos++] : 0;
}
