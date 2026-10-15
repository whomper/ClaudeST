/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsprsa.c - the RSA public-key operation on the Falcon's DSP56001
 * (dsp/rsa.a56), for checking signatures in Falcon mode. The 68030
 * turns the numbers into 23-bit digits, works out the counts the DSP
 * program needs, loads it through the DSP's bootstrap and trades numbers
 * with it through the host port.
 *
 * The DSP is shared: it is locked with the XBIOS (Dsp_Lock) while the program
 * uses it, and a busy or missing DSP just means the 68030 does the work.
 */
#include <string.h>
#include "tos.h"
#include "dsprsa.h"
#include "plat.h"
#include "../dsp/rsa_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200   (*(volatile unsigned long *)0x4baL)
#define HISR    (*(volatile unsigned char *)0xffffa202L)
#define HTXH    (*(volatile unsigned char *)0xffffa205L)
#define HTXM    (*(volatile unsigned char *)0xffffa206L)
#define HTXL    (*(volatile unsigned char *)0xffffa207L)

#define MAXN 180		/* 23-bit digits: up to 4096-bit keys */
#define DMASK 0x7fffffUL

long get_cookie(long id);

int dsp_rsa_present(void)
{
	return (get_cookie(0x5F534E44L) & 8) != 0;	/* _SND: a DSP56001 */
}

/* ---------------- talking to the DSP (supervisor mode) ---------------- */

static void pause_ms(unsigned long ms)
{
	unsigned long t0 = pf_ms();
	while (pf_ms() - t0 < ms)
		;
}

static void dsp_reset(void)
{
	trap14_ww(30, 0x10);		/* Ongibit: hold the DSP in reset */
	pause_ms(5);
	trap14_ww(29, ~0x10);		/* Offgibit: let it go: it starts its bootstrap */
	pause_ms(5);
}

static unsigned long io_words[16 + 2 * MAXN];
static int io_n, io_out, io_fail;
static unsigned long io_res[MAXN];

static int put(unsigned long w)
{
	unsigned long t0 = HZ200;
	while (!(HISR & 2))
		if (HZ200 - t0 > 200)
			return 0;
	HTXH = (unsigned char)(w >> 16);
	HTXM = (unsigned char)(w >> 8);
	HTXL = (unsigned char)w;
	return 1;
}

static int get(unsigned long *w)
{
	unsigned long t0 = HZ200;
	while (!(HISR & 1))
		if (HZ200 - t0 > 2000)		/* 10 s: a 4096-bit key takes about 1 */
			return 0;
	*w = (unsigned long)HTXH << 16;
	*w |= (unsigned long)HTXM << 8;
	*w |= HTXL;
	return 1;
}

static void sup_run(void)
{
	int i;
	io_fail = 1;
	/* the bootstrap loads 512 words and starts them */
	for (i = 0; i < 512; i++)
		if (!put(dsp_rsa_prog[i]))
			return;
	for (i = 0; i < io_n; i++)
		if (!put(io_words[i]))
			return;
	for (i = 0; i < io_out; i++)
		if (!get(&io_res[i]))
			return;
	io_fail = 0;
}

/* ---------------- numbers ---------------- */

/* big-endian bytes -> n little-endian 23-bit digits (no division: the
   68000 has none for 32-bit numbers, and this runs for every bit) */
static void to_digits(unsigned long *d, int n, const unsigned char *b, size_t len)
{
	unsigned long acc = 0;
	int have = 0, k = 0;
	memset(d, 0, n * sizeof(*d));
	while (len-- && k < n) {
		acc |= (unsigned long)b[len] << have;
		have += 8;
		if (have >= 23) {
			d[k++] = acc & DMASK;
			acc >>= 23;
			have -= 23;
		}
	}
	if (k < n)
		d[k] = acc & DMASK;
}

static void from_digits(unsigned char *b, size_t len, const unsigned long *d, int n)
{
	unsigned long acc = 0;
	int have = 0, k = 0;
	while (len--) {
		if (have < 8 && k < n) {
			acc |= d[k++] << have;
			have += 23;
		}
		b[len] = (unsigned char)acc;
		acc >>= 8;
		have -= 8;
	}
}

static int bitlen(const unsigned char *b, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		if (b[i]) {
			int k = 7;
			while (!(b[i] & (1 << k)))
				k--;
			return (int)((len - i - 1) * 8 + k + 1);
		}
	return 0;
}

int dsp_rsa_public(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		   const unsigned char *e, size_t elen)
{
	static unsigned long nd[MAXN];
	unsigned long ev = 0, inv, n0, eal;
	int bits, N, D, s, best_s = 0, ebits, i, lock;
	long best = 0x7fffffffL, d0 = 0, r = 0;

	while (nlen && !*n) {
		n++;
		nlen--;
	}
	bits = bitlen(n, nlen);
	if (bits < 64 || !(n[nlen - 1] & 1))
		return 0;
	N = (bits + 2 + 22) / 23;
	if (N > MAXN)
		return 0;
	while (elen && !*e) {
		e++;
		elen--;
	}
	if (!elen || elen > 3)
		return 0;
	for (i = 0; i < (int)elen; i++)
		ev = (ev << 8) | e[i];
	if (ev < 3 || ev > 0x7fffffUL)
		return 0;
	ebits = 0;
	while ((ev >> ebits) > 1)
		ebits++;
	ebits++;				/* bits in e */
	eal = (ev << (24 - ebits + 1)) & 0xffffffUL;

	to_digits(nd, N, n, nlen);
	/* -n^-1 mod 2^23: Newton's iteration doubles the good bits */
	n0 = nd[0];
	inv = n0;				/* right in 3 bits */
	for (i = 0; i < 4; i++)
		inv = inv * (2 - n0 * inv);
	/* R^2 mod n = 2^(2D): 2^(D+d0) by doubling, s squarings, times 2^(D+r) */
	D = 23 * N;
	for (s = 1; s <= 12; s++) {
		long dd = D >> s, rr = D - (dd << s), cost;
		if (dd <= rr)
			break;
		cost = 14L * (dd + rr) + 12L * N * s;
		if (cost < best) {
			best = cost;
			best_s = s;
			d0 = dd;
			r = rr;
		}
	}
	if (!best_s)
		return 0;

	i = 0;
	io_words[i++] = (unsigned long)N;
	io_words[i++] = (0UL - inv) & DMASK;
	io_words[i++] = (unsigned long)(D + r - (bits - 1));
	io_words[i++] = (unsigned long)(d0 - r);
	io_words[i++] = (unsigned long)best_s;
	io_words[i++] = eal;
	io_words[i++] = (unsigned long)(ebits - 1);
	io_words[i++] = (unsigned long)((bits - 1) / 23);
	io_words[i++] = 1UL << ((bits - 1) % 23);
	memcpy(io_words + i, nd, N * sizeof(nd[0]));
	i += N;
	to_digits(io_words + i, N, x, xlen);
	i += N;
	io_n = i;
	io_out = N;

	lock = (int)trap14_w(104);		/* Dsp_Lock */
	if (lock == -1)
		return 0;			/* another program has it */
	/* reset: bit 4 of the sound chip's port A, through the XBIOS, which
	   changes it with interrupts off. Writing the chip directly raced the
	   system's own use of it (floppy, key click) on a real Falcon */
	dsp_reset();
	Supexec(sup_run);
	if (lock == 0)
		trap14_w(105);			/* Dsp_Unlock */
	if (io_fail)
		return 0;
	from_digits(x, xlen, io_res, N);
	return 1;
}
