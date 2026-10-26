/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * src/chapol.c, ChaCha20-Poly1305 a piece at a time, against BearSSL's
 * br_poly1305_ctmul_run: the same bytes and the same tag, sealing and
 * opening, for records of every size up to 16 KB and more, AADs of 0 to
 * 40 bytes, keys and nonces at random; and a turn between pieces. Here
 * and as 68030 code under qemu-m68k:
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/chapol.c"

static long checks, failed, turns;

static void check(int ok, const char *what, long n)
{
	checks++;
	if (!ok && failed++ < 10)
		printf("FAIL %s (%ld)\n", what, n);
}

static void turn(void)
{
	turns++;
}

static unsigned long seed = 12345;
static unsigned rnd(void)
{
	seed = seed * 1103515245UL + 12345;
	return (unsigned)(seed >> 16) & 0x7fff;
}

int main(void)
{
	static unsigned char a[17000], b[17000];
	unsigned char key[32], iv[12], aad[40], ta[16], tb[16];
	long n, i, k;
	chapol_turn = turn;
	for (k = 0; k < 3000; k++) {
		size_t len = k < 2100 ? (size_t)k : (size_t)(rnd() * 2 % 16900);
		size_t alen = rnd() % 41;
		int enc = k & 1;
		for (i = 0; i < 32; i++)
			key[i] = (unsigned char)rnd();
		for (i = 0; i < 12; i++)
			iv[i] = (unsigned char)rnd();
		for (i = 0; i < (long)alen; i++)
			aad[i] = (unsigned char)rnd();
		for (n = 0; n < (long)len; n++)
			a[n] = b[n] = (unsigned char)rnd();
		turns = 0;
		chapol_run(key, iv, a, len, aad, alen, ta, br_chacha20_ct_run, enc);
		br_poly1305_ctmul_run(key, iv, b, len, aad, alen, tb, br_chacha20_ct_run, enc);
		check(!memcmp(a, b, len), enc ? "sealed bytes" : "opened bytes", k);
		check(!memcmp(ta, tb, 16), "tag", k);
		check(turns == (long)(len > CHAPOL_PIECE ? 2 * ((len - 1) / CHAPOL_PIECE) : 0), "turns between pieces", k);
	}
	/* a tag near p: all-ones data, many lengths (h close to 2^130 - 5) */
	for (k = 0; k < 64; k++) {
		memset(key, 0xff, 32);
		memset(iv, k, 12);
		memset(a, 0xff, 1024);
		memset(b, 0xff, 1024);
		chapol_run(key, iv, a, k * 16, aad, 0, ta, br_chacha20_ct_run, 0);
		br_poly1305_ctmul_run(key, iv, b, k * 16, aad, 0, tb, br_chacha20_ct_run, 0);
		check(!memcmp(ta, tb, 16), "tag, all ones", k);
	}
	/* RFC 8439 A.3's vectors 5 to 9: h at p and past it, s carried */
	{
		static const struct {
			unsigned char r0, sb;		/* r = r0 then zeros; s all sb */
			int n;				/* the message: n blocks of 16 */
			unsigned char m[48], tag[16];
		} v[] = {
			{ 2, 0, 1, { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff },
			  { 3 } },
			{ 2, 0xff, 1, { 2 }, { 3 } },
			{ 1, 0, 3, { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				     0xf0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				     0x11 }, { 5 } },
			{ 1, 0, 3, { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
				     0xfb,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,
				     1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, { 0 } },
			{ 2, 0, 1, { 0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff },
			  { 0xfa,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff } },
		};
		for (k = 0; k < (long)(sizeof(v) / sizeof(v[0])); k++) {
			unsigned char pk[32];
			uint32_t r[5], h[5] = { 0 };
			memset(pk, 0, 16);
			pk[0] = v[k].r0;
			memset(pk + 16, v[k].sb, 16);
			cp_r(pk, r);
			cp_blocks(h, r, v[k].m, 16 * v[k].n);
			cp_finish(h, pk, ta);
			check(!memcmp(ta, v[k].tag, 16), "RFC 8439 A.3 vector", k + 5);
		}
	}
	printf("%ld checks, %ld failed\n", checks, failed);
	return failed != 0;
}
