/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsprsa.h - RSA public-key operation on the Falcon's DSP56001.
 */
#ifndef DSPRSA_H
#define DSPRSA_H

#include <stddef.h>

int dsp_rsa_present(void);	/* this machine has a DSP56001 */
/* x = x^e mod n (big-endian bytes, x is xlen bytes); 1 if the DSP did
   it, 0 if it couldn't (busy, missing, key too big) */
int dsp_rsa_public(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		   const unsigned char *e, size_t elen);

#endif
