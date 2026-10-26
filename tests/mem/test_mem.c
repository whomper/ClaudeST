/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_mem.c - atari/libc.c's memcpy, memmove, memset and memchr (68000
 * code by hand) against plain C, every length to 300 and some long ones,
 * at every alignment of source and destination, with guard bytes round
 * the destination; run as 68000 and as 68030 code under qemu-m68k (a
 * Linux program, no C library). build/mem.c is cut from libc.c by the
 * Makefile.
 *   make test
 */
#include <stddef.h>

typedef unsigned char u8;
typedef unsigned int u32;
#include "build/mem.c"

static long sys(long n, long a, long b, long c)
{
	register long d0 __asm__("d0") = n;
	register long d1 __asm__("d1") = a;
	register long d2 __asm__("d2") = b;
	register long d3 __asm__("d3") = c;
	__asm__ volatile("trap #0" : "+d"(d0) : "d"(d1), "d"(d2), "d"(d3) : "memory");
	return d0;
}

static void say(const char *s)
{
	long n = 0;
	while (s[n])
		n++;
	sys(4, 1, (long)s, n);
}

static void num(unsigned long v)
{
	char b[12];
	int i = 11;
	b[i] = 0;
	do {			/* no division: libgcc's is 68020 code */
		unsigned long q = 0, t = 1000000000UL;
		(void)t;
		while (v >= 10) {
			unsigned long p = 10, k = 1;
			while (p << 1 <= v)
				p <<= 1, k <<= 1;
			v -= p;
			q += k;
		}
		b[--i] = (char)('0' + v);
		v = q;
	} while (v);
	say(b + i);
}

static u8 src[70000 + 16], dst[70000 + 32], ref[70000 + 32];
static unsigned long fails, checks;
static u32 seed = 12345;

static u8 rnd(void)		/* xorshift: no multiply (libgcc's is 68020 code) */
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return (u8)(seed >> 8);
}

static void check(int ok, const char *what, size_t n, int a, int b)
{
	checks++;
	if (ok)
		return;
	if (fails++ < 10) {
		say("FAIL ");
		say(what);
		say(" n=");
		num(n);
		say(" a=");
		num(a);
		say(" b=");
		num(b);
		say("\n");
	}
}

static int same(size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (dst[i] != ref[i])
			return 0;
	return 1;
}

static void one(size_t n, int a, int b)
{
	size_t i;
	u8 *r;
	for (i = 0; i < n + 16; i++)
		src[i] = rnd();
	for (i = 0; i < n + 32; i++)
		dst[i] = ref[i] = (u8)((i << 3) - i);
	/* memcpy */
	r = memcpy(dst + 8 + a, src + b, n);
	for (i = 0; i < n; i++)
		ref[8 + a + i] = src[b + i];
	check(r == dst + 8 + a && same(n + 32), "memcpy", n, a, b);
	/* memset */
	r = memset(dst + 8 + a, (int)(0x100 + (b << 5) + b + 7), n);
	for (i = 0; i < n; i++)
		ref[8 + a + i] = (u8)((b << 5) + b + 7);
	check(r == dst + 8 + a && same(n + 32), "memset", n, a, b);
	/* memmove, overlapping both ways */
	for (i = 0; i < n + 32; i++)
		dst[i] = ref[i] = rnd();
	memmove(dst + 8 + a, dst + 8 + b, n);
	{
		u8 t[400];
		if (n <= sizeof(t)) {
			for (i = 0; i < n; i++)
				t[i] = ref[8 + b + i];
			for (i = 0; i < n; i++)
				ref[8 + a + i] = t[i];
			check(same(n + 32), "memmove", n, a, b);
		}
	}
	/* memchr: the first of a byte, or none */
	if (n) {
		size_t at = rnd(), k;
		while (at >= n)
			at -= n;
		u8 c = src[b + at], *want = 0;
		for (k = 0; k < n; k++)
			if (src[b + k] == c) {
				want = src + b + k;
				break;
			}
		check(memchr(src + b, c, n) == want, "memchr", n, a, b);
		for (k = 0; k < n; k++)
			if (src[b + k] == 0xA5)
				src[b + k] = 0xA4;
		check(memchr(src + b, 0xA5, n) == 0, "memchr none", n, a, b);
	}
}

int main(void)
{
	size_t n;
	int a, b;
	static const size_t big[] = { 4095, 4096, 32767, 32768, 32769, 65535, 65536, 65537, 69999 };
#ifdef M68020
	mem_unaligned_ok = 1;
#endif
	for (n = 0; n <= 300; n++)
		for (a = 0; a < 4; a++)
			for (b = 0; b < 4; b++)
				one(n, a, b);
	for (n = 0; n < sizeof(big) / sizeof(big[0]); n++)
		for (a = 0; a < 2; a++)
			for (b = 0; b < 2; b++)
				one(big[n], a, b);
	num(checks);
	say(fails ? " checks, FAILED\n" : " checks, all passed\n");
	return fails ? 1 : 0;
}

__asm__(".globl _start\n_start:\n\tjsr main\n\tmove.l %d0,%d1\n\tmoveq #1,%d0\n\ttrap #0\n");
