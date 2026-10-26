/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* rsa030.h - the RSA public operation on the 68020 and up (rsa030.c) */
#include <stddef.h>
#include <stdint.h>
uint32_t rsa030_public(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		       const unsigned char *e, size_t elen);
