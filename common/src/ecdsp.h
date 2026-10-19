/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ecdsp.h - BearSSL's elliptic-curve interface with the arithmetic done
 * by a DSP56001 (dsp/ec.a56): the key exchange and the ECDSA signatures
 * of a TLS handshake, for P-256, P-384, P-521 and Curve25519.
 */
#ifndef ECDSP_H
#define ECDSP_H

#include "bearssl.h"

/* Use it where BearSSL takes a br_ec_impl. Without a port, with a curve
   it doesn't know, or when the DSP can't be had, it hands the work to
   BearSSL's own br_ec_all_m31 */
extern const br_ec_impl ecdsp_impl;

/* ECDSA signature checks (br_ecdsa_vrfy), the whole of them on the DSP:
   BearSSL's own would work out 1/s modulo the order on the 68030 (2 to
   6 million instructions). impl is ignored */
uint32_t ecdsp_vrfy_asn1(const br_ec_impl *impl, const void *hash, size_t hash_len,
			 const br_ec_public_key *pk, const void *sig, size_t sig_len);
uint32_t ecdsp_vrfy_raw(const br_ec_impl *impl, const void *hash, size_t hash_len,
			const br_ec_public_key *pk, const void *sig, size_t sig_len);

/* tls.c's: every handshake from now on does its elliptic curves with
   impl and vrfy (ecdsp_impl, ecdsp_vrfy_asn1); NULL for BearSSL's own */
void tls_set_ec(const br_ec_impl *impl, br_ecdsa_vrfy vrfy);

/* how the DSP is reached: atari/dspec.c on the Falcon, a model or a
   simulator in the tests */
typedef struct {
	/* get the DSP and start ec.a56 on it, call fn(ctx), let it go;
	   0 if the DSP couldn't be had. On the Atari fn runs in supervisor
	   mode, where the host port is */
	int (*run)(void (*fn)(void *), void *ctx);
	void (*put)(unsigned long w);		/* a 24-bit word to the DSP */
	unsigned long (*get)(void);		/* and one from it */
} ECDSP_PORT;
extern const ECDSP_PORT *ecdsp_port;

/* put and get set it when the DSP stops answering, and then do nothing;
   the operation goes to BearSSL */
extern int ecdsp_failed;

/* told after each operation: what it was ("ECDSA P-384", "X25519"),
   whether the DSP did it, and how long it took */
extern void (*ecdsp_note)(const char *what, int on_dsp, unsigned long ms);

#endif
