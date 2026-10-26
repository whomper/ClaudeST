/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rsa030.c - the RSA public operation (a signature check) on the 68020
 * and up, with mont030.S: x = x^e mod n, big-endian bytes as BearSSL's
 * br_rsa_public. Not constant-time: it only ever sees public values.
 */
#include <string.h>
#include <stdint.h>
#include "rsa030.h"

#define MAXW (4096 / 32 + 2)

uint32_t mont030(uint32_t *buf, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i, long len);
void dbl030(uint32_t *r, const uint32_t *n, long k);

static uint32_t N[MAXW], A[MAXW], X[MAXW], T[MAXW], B[MAXW + 1];

static int ge(const uint32_t *a, const uint32_t *b, long k)
{
	while (k-- > 0)
		if (a[k] != b[k])
			return a[k] > b[k];
	return 1;
}

static void sub(uint32_t *a, const uint32_t *b, long k)
{
	uint32_t c = 0;
	long i;
	for (i = 0; i < k; i++) {
		uint32_t t = a[i] - b[i] - c;
		c = (a[i] < b[i]) || (a[i] - b[i] < c);
		a[i] = t;
	}
}

/* d = a * b / R mod n */
static void mont(uint32_t *d, const uint32_t *a, const uint32_t *b, uint32_t m0i, long k)
{
	uint32_t dh;
	memset(B, 0, (size_t)(k + 1) * 4);
	dh = mont030(B, a, b, N, m0i, k);
	memcpy(d, B + 1, (size_t)k * 4);
	if (dh || ge(d, N, k))
		sub(d, N, k);
}

static void decode(uint32_t *w, long k, const unsigned char *s, size_t len)
{
	long i;
	memset(w, 0, (size_t)k * 4);
	for (i = 0; i < (long)len; i++) {
		long b = (long)len - 1 - i;	/* byte i from the end */
		w[i >> 2] |= (uint32_t)s[b] << (8 * (i & 3));
	}
}

uint32_t rsa030_public(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		       const unsigned char *e, size_t elen)
{
	long k, i, bits, top;
	uint32_t m0i, inv;
	int started = 0;
	while (nlen > 0 && !*n) {
		n++;
		nlen--;
	}
	while (elen > 0 && !*e) {
		e++;
		elen--;
	}
	if (!nlen || !elen || nlen > 512 || xlen > nlen + 8 || !(n[nlen - 1] & 1))
		return 0;
	k = (long)(nlen + 3) / 4;
	if (k < 2)
		return 0;
	decode(N, k, n, nlen);
	decode(X, k, x + (xlen > nlen ? xlen - nlen : 0), xlen > nlen ? nlen : xlen);
	if (ge(X, N, k))
		return 0;
	/* m0i = -1/n mod 2^32 (Newton: each step doubles the right bits) */
	inv = N[0];
	for (i = 0; i < 5; i++)
		inv *= 2 - N[0] * inv;
	m0i = (uint32_t)-inv;
	/* R^2 mod n, R = 2^(32k): 2^(bits-1) < n, doubled up to 2^(64k) */
	for (bits = 32 * k; !(N[(bits - 1) >> 5] >> ((bits - 1) & 31) & 1); bits--)
		;
	memset(T, 0, (size_t)k * 4);
	T[(bits - 1) >> 5] = (uint32_t)1 << ((bits - 1) & 31);
	for (i = bits - 1; i < 64 * k; i++)
		dbl030(T, N, k);
	mont(A, X, T, m0i, k);		/* x R */
	memcpy(T, A, (size_t)k * 4);
	/* left to right over e's bits */
	for (i = 0; i < (long)elen; i++) {
		for (top = 7; top >= 0; top--) {
			int bit = e[i] >> top & 1;
			if (!started) {
				started = bit;
				continue;
			}
			mont(T, T, T, m0i, k);
			if (bit)
				mont(T, T, A, m0i, k);
		}
	}
	memset(A, 0, (size_t)k * 4);
	A[0] = 1;
	mont(T, T, A, m0i, k);		/* out of Montgomery */
	for (i = 0; i < (long)xlen; i++) {
		long b = (long)xlen - 1 - i;
		x[b] = i < 4 * k ? (unsigned char)(T[i >> 2] >> (8 * (i & 3))) : 0;
	}
	return 1;
}
