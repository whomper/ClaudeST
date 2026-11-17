/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_ll64.c - atari/ll64.c (64-bit multiply, divide, remainder and
 * shifts in C, for the builds libgcc can't serve) as ColdFire code under
 * qemu-m68k -cpu cfv4e, against the host's own 64-bit arithmetic (-DHOST):
 * 200,000 random cases each, every answer folded into one sum that both
 * must print alike */
#ifdef HOST
#include <stdio.h>
#else
#include "rt.h"
#endif

typedef unsigned long long u64;
typedef long long s64;

static u64 seed = 1;
static u64 rnd(void)
{
	seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
	return seed ^ (seed >> 29);
}

static unsigned long sum = 2166136261UL;
static void fold(u64 v)
{
	int i;
	for (i = 0; i < 8; i++, v >>= 8)
		sum = (sum ^ (unsigned long)(v & 255)) * 16777619UL;
}

int main(void)
{
	long i;
	for (i = 0; i < 200000; i++) {
		volatile u64 a = rnd(), b = rnd() >> (rnd() & 63);
		volatile int n = (int)(rnd() & 63);
		if (b == 0)
			b = 1;
		fold(a * b);
		fold(a / b);
		fold(a % b);
		fold((u64)((s64)a / (s64)b));
		fold((u64)((s64)a % (s64)b));
		fold(a << n);
		fold(a >> n);
		fold((u64)((s64)a >> n));
		fold((u64)(a >> 32) / (u64)(b | 1));	/* (a 32-bit pair: ll64's short way) */
	}
#ifdef HOST
	printf("ll64: sum %08lx\n", sum & 0xffffffffUL);
#else
	out("ll64: sum "); out_hex(sum); out("\n");
#endif
	return 0;
}
