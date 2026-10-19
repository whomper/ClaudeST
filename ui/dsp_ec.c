/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_ec.c - Falcon mode's elliptic curves on the DSP56001 (src/ecdsp.c,
 * atari/dspec.c, dsp/ec.a56): the key exchange and the ECDSA signatures
 * of a TLS handshake, alongside dsp.c's RSA. An app takes it by building
 * these files and calling falcon_dsp_ec_init() after falcon_dsp_init();
 * without them nothing changes.
 *
 * The first time, the DSP works out RFC 7748's first X25519 example; if
 * it gets it wrong, BearSSL does the curves on the 68030 for the rest of
 * the session.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "../src/tls.h"
#include "../src/conn.h"
#include "../src/plat.h"
#include "../src/util.h"
#include "../src/ecdsp.h"
#include "../atari/dspec.h"

static int have_dsp, checked, trusted;
static unsigned long test_ms;		/* the self-test's, left out of the first job's time */
static ECDSP_PORT checked_port;		/* dspec_port, with the self-test first */

static const unsigned char st_k[32] = {
	0xa5,0x46,0xe3,0x6b,0xf0,0x52,0x7c,0x9d,0x3b,0x16,0x15,0x4b,0x82,0x46,0x5e,0xdd,
	0x62,0x14,0x4c,0x0a,0xc1,0xfc,0x5a,0x18,0x50,0x6a,0x22,0x44,0xba,0x44,0x9a,0xc4
};
static const unsigned char st_u[32] = {
	0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,
	0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c
};
static const unsigned char st_out[32] = {
	0xc3,0xda,0x55,0x37,0x9d,0xe9,0xc6,0x90,0x8e,0x94,0xea,0x4d,0xf2,0x8d,0x08,0x4f,
	0x32,0xec,0xcf,0x03,0x49,0x1c,0x71,0xf7,0x54,0xb4,0x07,0x55,0x77,0xa2,0x85,0x52
};

static void note(const char *what, int on_dsp, unsigned long ms)
{
	char msg[80];
	if (test_ms && ms >= test_ms)
		ms -= test_ms;
	test_ms = 0;
	snprintf(msg, sizeof(msg), "%s on the %s, %lu ms", what, on_dsp ? "DSP" : "68030 (DSP busy)", ms);
	conn_log("TLS", " -- ", msg, strlen(msg));
}

static int self_test(void)
{
	unsigned char u[32];
	memcpy(u, st_u, sizeof(u));
	ecdsp_note = 0;
	ecdsp_port = &dspec_port;
	return ecdsp_impl.mul(u, 32, st_k, 32, BR_EC_curve25519) && !ecdsp_failed &&
	       !memcmp(u, st_out, sizeof(u));
}

static int run(void (*fn)(void *), void *ctx)
{
	if (!checked) {
		char m[80];
		unsigned long t0 = pf_ms();
		checked = 1;
		trusted = self_test();
		test_ms = pf_ms() - t0;
		if (trusted)
			snprintf(m, sizeof(m), "the DSP passed its elliptic-curve self-test (%lu ms)", test_ms);
		else
			str_copy(m, "the DSP failed its elliptic-curve self-test: curves on the 68030", sizeof(m));
		conn_log("TLS", " -- ", m, strlen(m));
		ecdsp_port = &checked_port;
		ecdsp_note = note;
		ecdsp_failed = 0;
	}
	return trusted && dspec_port.run(fn, ctx);
}

void falcon_dsp_ec_init(void)
{
	have_dsp = opt.falcon && opt.dsp && dspec_present();
	if (!have_dsp) {
		ecdsp_port = 0;
		tls_set_ec(0, 0);
		return;
	}
	checked_port = dspec_port;
	checked_port.run = run;
	ecdsp_port = &checked_port;
	tls_set_ec(&ecdsp_impl, ecdsp_vrfy_asn1);
}

const char *falcon_dsp_ec_state(void)
{
	return have_dsp && (!checked || trusted) ? "DSP" : "68030";
}
