/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_030.c - TLS.OVL's 68030 assembly against BearSSL's C, run as
 * 68030 code under qemu-m68k (a Linux program, no C library):
 *   - br_i31_montymul (i31_montmul_030.c with mont31_030.S) against
 *     BearSSL's own src/int/i31_montmul.c, word for word, moduli of 2 to
 *     4096 bits, the curves' sizes among them;
 *   - rsa030_public (rsa030.c with mont030.S) against br_rsa_i31_public,
 *     1024 to 4096 bits, short moduli, e = 3 and 65537, and its refusals
 *     (x not below n, an even n).
 *   make test
 */
#include <stddef.h>
#include <stdint.h>
#include "bearssl.h"
#include "rsa030.h"

void br_i31_montymul(uint32_t *d, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i);
void br_i31_montymul_c(uint32_t *d, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i);
uint32_t br_i31_decode_reduce(uint32_t *x, const void *src, size_t len, const uint32_t *m);
void br_i31_decode(uint32_t *x, const void *src, size_t len);
uint32_t br_i31_ninv31(uint32_t x);

/* ---- the bits of a C library BearSSL needs, and Linux's write and exit */
void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d; const unsigned char *b = s;
	if (a < b) while (n--) *a++ = *b++;
	else { a += n; b += n; while (n--) *--a = *--b; }
	return d;
}
void *memset(void *d, int c, size_t n) { unsigned char *a = d; while (n--) *a++ = (unsigned char)c; return d; }
int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
	return 0;
}
static long sys3(long nr, long a, long b, long c)
{
	register long d0 __asm__("d0") = nr;
	register long d1 __asm__("d1") = a;
	register long d2 __asm__("d2") = b;
	register long d3 __asm__("d3") = c;
	__asm__ volatile("trap #0" : "+d"(d0) : "d"(d1), "d"(d2), "d"(d3) : "memory");
	return d0;
}
static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(4, 1, (long)s, n); }
static void num(unsigned long v)
{
	char b[12]; int i = 11; b[i] = 0;
	do b[--i] = (char)('0' + v % 10); while (v /= 10);
	out(b + i);
}

static int failed, checks;
/* "ok   what", or "ok   pre BITS what" with bits; (n) the cases that failed */
static void check_bits(int ok, const char *pre, int bits, const char *what, unsigned long n)
{
	checks++;
	failed += !ok;
	out(ok ? "ok   " : "FAIL ");
	if (pre) {
		out(pre);
		num((unsigned long)bits);
	}
	out(what);
	if (n) { out(" ("); num(n); out(" wrong)"); }
	out("\n");
}
#define check(ok, what, n) check_bits(ok, 0, 0, what, n)

static uint32_t seed = 12345;
static unsigned char rnd(void) { seed = seed * 1103515245u + 12345u; return (unsigned char)(seed >> 16); }

/* ---- br_i31_montymul: the assembly against BearSSL's C */
static int montymul_case(int bits)
{
	static unsigned char b[520];
	static uint32_t m[140], x[140], y[140], d1[140], d2[140];
	size_t len = (size_t)(bits + 7) >> 3, i;
	uint32_t m0i;
	for (i = 0; i < len; i++)
		b[i] = rnd();
	b[0] &= (unsigned char)(0xFF >> ((8 - bits % 8) % 8));
	b[0] |= (unsigned char)(0x80 >> ((8 - bits % 8) % 8));	/* exactly `bits` bits */
	b[len - 1] |= 1;
	br_i31_decode(m, b, len);
	m0i = br_i31_ninv31(m[1]);
	for (i = 0; i < len; i++)
		b[i] = rnd();
	br_i31_decode_reduce(x, b, len, m);
	for (i = 0; i < len; i++)
		b[i] = rnd();
	br_i31_decode_reduce(y, b, len, m);
	br_i31_montymul(d1, x, y, m, m0i);
	br_i31_montymul_c(d2, x, y, m, m0i);
	return !memcmp(d1, d2, (((size_t)m[0] + 31) >> 5) * 4 + 4);
}

/* ---- rsa030_public against br_rsa_i31_public */
static int rsa_case(int bits, int e3)
{
	static unsigned char n[512], a[512], c[512];
	static const unsigned char e65537[] = { 1, 0, 1 }, e3b[] = { 3 };
	size_t len = (size_t)(bits + 7) >> 3, i;
	br_rsa_public_key k;
	for (i = 0; i < len; i++)
		n[i] = rnd();
	n[0] &= (unsigned char)(0xFF >> ((8 - bits % 8) % 8));
	n[0] |= (unsigned char)(0x80 >> ((8 - bits % 8) % 8));
	n[len - 1] |= 1;
	for (i = 0; i < len; i++)
		a[i] = rnd();
	a[0] = n[0] >> 1;		/* below n */
	memcpy(c, a, len);
	k.n = n, k.nlen = len, k.e = (unsigned char *)(e3 ? e3b : e65537), k.elen = e3 ? 1 : 3;
	if (!br_rsa_i31_public(a, len, &k))
		return 0;
	return rsa030_public(c, len, n, len, k.e, k.elen) && !memcmp(a, c, len);
}

int main(void)
{
	static const int sizes[] = { 2, 31, 32, 33, 62, 63, 64, 127, 255, 256, 384, 521, 1024, 1536, 2047, 2048, 3072,
				     4095, 4096 };
	static const int rsizes[] = { 1024, 1031, 2040, 2047, 2048, 3072, 4000, 4096 };
	unsigned i, r, bad;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		for (bad = 0, r = 0; r < 40; r++)
			bad += !montymul_case(sizes[i]);
		check_bits(!bad, "br_i31_montymul, ", sizes[i], " bits: the assembly's product is the C's, 40 times",
			   bad);
	}
	for (i = 0; i < sizeof(rsizes) / sizeof(rsizes[0]); i++) {
		for (bad = 0, r = 0; r < 6; r++)
			bad += !rsa_case(rsizes[i], r & 1);
		check_bits(!bad, "rsa030_public, ", rsizes[i], " bits: BearSSL's answer, e = 3 and 65537", bad);
	}
	{
		static unsigned char n[256], x[256];
		static const unsigned char e[] = { 1, 0, 1 };
		for (i = 0; i < 256; i++)
			n[i] = rnd();
		n[0] |= 0x80, n[255] |= 1;
		memcpy(x, n, 256);
		check(!rsa030_public(x, 256, n, 256, e, 3), "rsa030_public: x = n refused", 0);
		n[255] &= 0xFE;
		x[0] = 1;
		check(!rsa030_public(x, 256, n, 256, e, 3), "rsa030_public: an even n refused", 0);
	}
	out(failed ? "FAILED\n" : "all passed\n");
	return failed != 0;
}

__asm__(".globl _start\n_start:\n\tjsr main\n\tmove.l %d0,%d1\n\tmoveq #1,%d0\n\ttrap #0\n");
