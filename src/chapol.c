/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * chapol.c - ChaCha20-Poly1305 for a TLS record, a piece at a time
 * (included by tls.c; tests/chapol tests it on its own).
 *
 * BearSSL seals or opens a record of up to 16 KB in one call: 370 ms on
 * a stock Falcon, longer than a key may wait (the guidelines' Speed).
 * chapol_run is a br_poly1305_run, as BearSSL's poly1305_ctmul (the same
 * 26-bit words, Poly1305 as in floodyberry's poly1305-donna), that runs
 * ChaCha20 and the MAC over CHAPOL_PIECE bytes at a time and calls
 * chapol_turn between pieces; the result is the same bytes and tag.
 */
#include <stdint.h>
#include <string.h>
#include "bearssl.h"

#define CHAPOL_PIECE 2048		/* a multiple of ChaCha20's 64 and Poly1305's 16 */

static void (*chapol_turn)(void);	/* between pieces; 0: none */

static uint32_t cp_le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void cp_put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* blocks of 16 bytes into h, the last one padded with zeros, as the AEAD
   construction pads the data and the AAD */
static void cp_blocks(uint32_t *h, const uint32_t *r, const unsigned char *d, size_t len)
{
	uint32_t r0 = r[0], r1 = r[1], r2 = r[2], r3 = r[3], r4 = r[4];
	uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
	uint32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4], c;
	uint64_t d0, d1, d2, d3, d4;
	unsigned char tmp[16];
	while (len > 0) {
		const unsigned char *m = d;
		if (len < 16) {
			memset(tmp, 0, sizeof(tmp));
			memcpy(tmp, d, len);
			m = tmp;
		}
		h0 += cp_le32(m) & 0x3ffffff;
		h1 += (cp_le32(m + 3) >> 2) & 0x3ffffff;
		h2 += (cp_le32(m + 6) >> 4) & 0x3ffffff;
		h3 += (cp_le32(m + 9) >> 6) & 0x3ffffff;
		h4 += (cp_le32(m + 12) >> 8) | (1UL << 24);
		d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
		d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
		d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
		d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
		d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
		c = (uint32_t)(d0 >> 26);
		h0 = (uint32_t)d0 & 0x3ffffff;
		d1 += c;
		c = (uint32_t)(d1 >> 26);
		h1 = (uint32_t)d1 & 0x3ffffff;
		d2 += c;
		c = (uint32_t)(d2 >> 26);
		h2 = (uint32_t)d2 & 0x3ffffff;
		d3 += c;
		c = (uint32_t)(d3 >> 26);
		h3 = (uint32_t)d3 & 0x3ffffff;
		d4 += c;
		c = (uint32_t)(d4 >> 26);
		h4 = (uint32_t)d4 & 0x3ffffff;
		h0 += c * 5;
		c = h0 >> 26;
		h0 &= 0x3ffffff;
		h1 += c;
		if (len < 16)
			break;
		d += 16;
		len -= 16;
	}
	h[0] = h0;
	h[1] = h1;
	h[2] = h2;
	h[3] = h3;
	h[4] = h4;
}

static void cp_mac(uint32_t *h, const uint32_t *r, const unsigned char *d, size_t len)
{
	while (len > CHAPOL_PIECE) {
		cp_blocks(h, r, d, CHAPOL_PIECE);
		d += CHAPOL_PIECE;
		len -= CHAPOL_PIECE;
		if (chapol_turn)
			chapol_turn();
	}
	cp_blocks(h, r, d, len);
}

static void cp_chacha(br_chacha20_run ic, const void *key, const void *iv, unsigned char *d, size_t len)
{
	uint32_t cc = 1;
	while (len > CHAPOL_PIECE) {
		cc = ic(key, iv, cc, d, CHAPOL_PIECE);
		d += CHAPOL_PIECE;
		len -= CHAPOL_PIECE;
		if (chapol_turn)
			chapol_turn();
	}
	if (len)
		ic(key, iv, cc, d, len);
}

/* r, the first half of the one-time key, clamped, in 26-bit words */
static void cp_r(const unsigned char *pkey, uint32_t *r)
{
	r[0] = cp_le32(pkey) & 0x3ffffff;
	r[1] = (cp_le32(pkey + 3) >> 2) & 0x3ffff03;
	r[2] = (cp_le32(pkey + 6) >> 4) & 0x3ffc0ff;
	r[3] = (cp_le32(pkey + 9) >> 6) & 0x3f03fff;
	r[4] = (cp_le32(pkey + 12) >> 8) & 0x00fffff;
}

/* the tag: h fully reduced mod 2^130 - 5, plus s (pkey's second half),
   mod 2^128 */
static void cp_finish(uint32_t *h, const unsigned char *pkey, unsigned char *t)
{
	uint32_t g[5], c, mask;
	uint64_t f;
	int i;
	/* the full carry, then h - p where that is positive */
	c = h[1] >> 26;
	h[1] &= 0x3ffffff;
	for (i = 2; i < 5; i++) {
		h[i] += c;
		c = h[i] >> 26;
		h[i] &= 0x3ffffff;
	}
	h[0] += c * 5;
	c = h[0] >> 26;
	h[0] &= 0x3ffffff;
	h[1] += c;
	g[0] = h[0] + 5;
	c = g[0] >> 26;
	g[0] &= 0x3ffffff;
	for (i = 1; i < 5; i++) {
		g[i] = h[i] + c;
		c = g[i] >> 26;
		g[i] &= 0x3ffffff;
	}
	g[4] = h[4] + c - (1UL << 26);
	mask = (g[4] >> 31) - 1;		/* all ones when h >= p */
	for (i = 0; i < 5; i++)
		h[i] = (h[i] & ~mask) | (g[i] & mask);
	/* h mod 2^128, plus s */
	f = (uint64_t)(h[0] | (h[1] << 26)) + cp_le32(pkey + 16);
	cp_put32(t, (uint32_t)f);
	f = (uint64_t)((h[1] >> 6) | (h[2] << 20)) + cp_le32(pkey + 20) + (f >> 32);
	cp_put32(t + 4, (uint32_t)f);
	f = (uint64_t)((h[2] >> 12) | (h[3] << 14)) + cp_le32(pkey + 24) + (f >> 32);
	cp_put32(t + 8, (uint32_t)f);
	f = (uint64_t)((h[3] >> 18) | (h[4] << 8)) + cp_le32(pkey + 28) + (f >> 32);
	cp_put32(t + 12, (uint32_t)f);
}

static void chapol_run(const void *key, const void *iv, void *data, size_t len, const void *aad,
		       size_t aad_len, void *tag, br_chacha20_run ichacha, int encrypt)
{
	unsigned char pkey[32], foot[16];
	uint32_t r[5], h[5];
	memset(pkey, 0, sizeof(pkey));
	ichacha(key, iv, 0, pkey, sizeof(pkey));
	if (encrypt)
		cp_chacha(ichacha, key, iv, data, len);
	cp_r(pkey, r);
	memset(h, 0, sizeof(h));
	cp_put32(foot, (uint32_t)aad_len);
	cp_put32(foot + 4, 0);
	cp_put32(foot + 8, (uint32_t)len);
	cp_put32(foot + 12, 0);
	cp_mac(h, r, aad, aad_len);
	cp_mac(h, r, data, len);
	cp_blocks(h, r, foot, sizeof(foot));
	cp_finish(h, pkey, tag);
	if (!encrypt)
		cp_chacha(ichacha, key, iv, data, len);
}
