/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_mont.c - mont030.S's ColdFire code (mont030, dbl030) and rsa030.c
 * on it, under qemu-m68k -cpu cfv4e, against the same program built on
 * the host with mont030 and dbl030 in C (-DHOST, 64-bit products): Mont-
 * gomery products of 2 to 130 words, doublings at and near n, and RSA
 * public operations of 64 to 4096 bits with e = 3 and 65537, every word
 * of every answer folded into one sum that both must print alike */
#include <stdint.h>
#ifdef HOST
#include <stdio.h>
#include <string.h>
#else
#include "rt.h"
#endif
#include "rsa030.h"

uint32_t mont030(uint32_t *buf, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i, long len);
void dbl030(uint32_t *r, const uint32_t *n, long k);

#ifdef HOST
uint32_t mont030(uint32_t *buf, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i, long len)
{
	uint32_t dh = 0;
	long u, v;
	for (u = 0; u < len; u++) {
		uint32_t xu = x[u], f = (buf[1] + xu * y[0]) * m0i, r1 = 0, r2 = 0;
		uint64_t zh;
		for (v = 0; v < len; v++) {
			uint64_t z = (uint64_t)xu * y[v] + buf[v + 1] + r1;
			r1 = (uint32_t)(z >> 32);
			z = (uint64_t)f * m[v] + (uint32_t)z + r2;
			r2 = (uint32_t)(z >> 32);
			buf[v] = (uint32_t)z;
		}
		zh = (uint64_t)dh + r1 + r2;
		buf[len] = (uint32_t)zh;
		dh = (uint32_t)(zh >> 32);
	}
	return dh;
}
#endif

/* (d is buf[1..len]: each step reads d[v] at buf[v + 1] and writes the
   new d[v - 1] at buf[v], buf[0] taking the word divided away, as the
   assembly does) */

void dbl_ref(uint32_t *r, const uint32_t *n, long k)
{
	uint32_t c = 0, b = 0;
	long i;
	int ge = 1;
	for (i = 0; i < k; i++) {
		uint32_t t = r[i];
		r[i] = t << 1 | c;
		c = t >> 31;
	}
	if (!c)
		for (i = k - 1; i >= 0; i--)
			if (r[i] != n[i]) {
				ge = r[i] > n[i];
				break;
			}
	if (c || ge)
		for (i = 0; i < k; i++) {
			uint32_t t = r[i] - n[i] - b;
			b = (r[i] < n[i]) || (r[i] - n[i] < b);
			r[i] = t;
		}
}
#ifdef HOST
void dbl030(uint32_t *r, const uint32_t *n, long k) { dbl_ref(r, n, k); }
#endif

static uint32_t seed = 12345;
static uint32_t rnd(void)
{
	uint32_t lo;
	seed = seed * 1103515245U + 12345U;
	lo = seed >> 16;
	seed = seed * 1103515245U + 12345U;
	return lo | (seed >> 16) << 16;
}

static uint32_t sum = 2166136261U;
static void fold(uint32_t w) { int i; for (i = 0; i < 4; i++) sum = (sum ^ (w >> (8 * i) & 255)) * 16777619U; }

static uint32_t N[132], X[132], Y[132], B[134], R[132];

int main(void)
{
	long k, t, i;
	int fails = 0;
	/* Montgomery products */
	for (k = 2; k <= 130; k += (k < 10 ? 1 : 16)) {
		for (t = 0; t < 6; t++) {
			uint32_t inv, m0i;
			for (i = 0; i < k; i++)
				N[i] = rnd(), X[i] = rnd(), Y[i] = rnd();
			N[0] |= 1;
			N[k - 1] |= 0x80000000U;
			X[k - 1] = X[k - 1] % N[k - 1];
			Y[k - 1] = Y[k - 1] % N[k - 1];
			if (t == 5)
				for (i = 0; i < k; i++)
					X[i] = Y[i] = N[i] - (i == 0);	/* n - 1: every carry */
			inv = N[0];
			for (i = 0; i < 5; i++)
				inv *= 2 - N[0] * inv;
			m0i = (uint32_t)-inv;
			memset(B, 0, sizeof B);
			fold(mont030(B, X, Y, N, m0i, k));
			for (i = 0; i <= k; i++)
				fold(B[i]);
			/* doubling: r < n, r = n - 1 and r near 2^(32k) / 2 */
			for (i = 0; i < k; i++)
				R[i] = t == 0 ? N[i] - (i == 0) : t == 1 ? (i == k - 1 ? 0x7fffffffU : 0xffffffffU) : X[i];
			if (t == 1 && R[k - 1] >= N[k - 1])
				R[k - 1] = N[k - 1] - 1;
			{
				uint32_t S[132];
				memcpy(S, R, sizeof S);
				dbl030(R, N, k);
				dbl_ref(S, N, k);
				if (memcmp(R, S, (size_t)k * 4) != 0)
					fails++;
			}
			for (i = 0; i < k; i++)
				fold(R[i]);
		}
	}
	/* RSA public operations through rsa030.c */
	{
		static const long sizes[] = { 8, 64, 128, 256, 384, 512 };
		static unsigned char n[512], x[512];
		static const unsigned char e3[] = { 3 }, e65537[] = { 1, 0, 1 };
		long s;
		for (s = 0; s < 6; s++) {
			long nl = sizes[s];
			for (t = 0; t < 2; t++) {
				for (i = 0; i < nl; i++)
					n[i] = (unsigned char)rnd(), x[i] = (unsigned char)rnd();
				n[0] |= 0x80;
				n[nl - 1] |= 1;
				x[0] &= 0x7f;
#ifdef HOST
				unsigned char x0[512];
				memcpy(x0, x, (size_t)nl);
#endif
				fold(rsa030_public(x, (size_t)nl, n, (size_t)nl, t ? e65537 : e3, t ? 3 : 1));
#ifdef HOST
				if (nl == 8) {	/* x^e mod n in 128 bits, against rsa030's */
					unsigned __int128 b = 0, nn = 0, r = 1, got = 0;
					unsigned long e = t ? 65537 : 3;
					for (i = 0; i < 8; i++)
						b = b << 8 | x0[i], nn = nn << 8 | n[i], got = got << 8 | x[i];
					for (; e; e >>= 1, b = b * b % nn)
						if (e & 1)
							r = r * b % nn;
					if (r != got)
						fails++, printf("mont: RSA of 64 bits differs from x^e mod n\n");
				}
#endif
				for (i = 0; i < nl; i++)
					fold(x[i]);
			}
		}
	}
#ifdef HOST
	printf("mont: sum %08x%s\n", sum, fails ? " (the doubling's reference differs)" : "");
#else
	out("mont: sum "); out_hex(sum); out(fails ? " (dbl030 differs from its reference)\n" : "\n");
#endif
	return fails != 0;
}
