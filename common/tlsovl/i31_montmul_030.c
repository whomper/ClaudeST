/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * i31_montmul_030.c - BearSSL's br_i31_montymul with its loop in 68030
 * assembly (mont31_030.S), in place of src/int/i31_montmul.c: what RSA
 * (i31), the prime curves (ec_prime_i31: P-256, P-384, P-521), ECDSA and
 * X25519 (ec_c25519_i31) all multiply with.
 */
#include "inner.h"

uint32_t mont31_030(uint32_t *d, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t m0i,
		    long len);

void
br_i31_montymul(uint32_t *d, const uint32_t *x, const uint32_t *y,
	const uint32_t *m, uint32_t m0i)
{
	size_t len = (m[0] + 31) >> 5;
	uint32_t dh;

	br_i31_zero(d, m[0]);
	dh = mont31_030(d, x, y, m, m0i, (long)len);
	d[0] = m[0];
	br_i31_sub(d, m, NEQ(dh, 0) | NOT(br_i31_sub(d, m, 0)));
}
