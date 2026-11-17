/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_rows.c - ui/text.c's rows_xor, rows_set and rows_copy_mid2 (their
 * C on a ColdFire, their 68000 assembly elsewhere) on random boxes: one
 * sum of every byte, printed alike by the ColdFire build (qemu-m68k -cpu
 * cfv4e) and the 68030 one */
#include "rt.h"
#include "rows.c"		/* (the three, as text.c has them: make's sed) */

static unsigned long seed = 99;
static unsigned rnd(unsigned n) { seed = seed * 1103515245UL + 12345UL; return (unsigned)((seed >> 16) % n); }
static unsigned char a[16384], b[16384];

int main(void)
{
	unsigned long sum = 2166136261UL;
	int t, i;
	for (i = 0; i < (int)sizeof a; i++)
		a[i] = (unsigned char)rnd(256), b[i] = (unsigned char)rnd(256);
	for (t = 0; t < 3000; t++) {
		short n = (short)(2 * (1 + rnd(60))), rows = (short)(1 + rnd(20));
		long line = n + 2 * (long)rnd(40);
		unsigned off = 2 * rnd(2000);
		switch (rnd(3)) {
		case 0: rows_xor(a + off, rows, line, n); break;
		case 1: rows_set(a + off, rows, line, n, (unsigned long)rnd(65536) << 16 | rnd(65536)); break;
		default: rows_copy_mid2(a + off, line, b + 2 * rnd(2000), n + 2 * (long)rnd(40), rows, n); break;
		}
	}
	for (i = 0; i < (int)sizeof a; i++)
		sum = (sum ^ a[i]) * 16777619UL;
	out("rows: sum "); out_hex(sum); out("\n");
	return 0;
}
