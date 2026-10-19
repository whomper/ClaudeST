/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_ec.c - src/ecdsp.c against BearSSL's own br_ec_all_m31: key
 * exchanges (mul, mulgen) and signature checks (muladd, and whole
 * ECDSA signatures, good and bad) on every curve, RFC 7748's vectors,
 * points that aren't on the curve. With the model of the DSP program
 * (model.c), or the program itself on Hatari's DSP core (sim.c):
 *
 *     test_ec [-s] [ROUNDS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bearssl.h"
#include "ecdsp.h"
#include "model.h"

unsigned long pf_ms(void)
{
	return (unsigned long)(clock() / (CLOCKS_PER_SEC / 1000));
}

static int mrun(void (*fn)(void *), void *ctx)
{
	model_reset();
	fn(ctx);
	return 1;
}
static const ECDSP_PORT model_port = { mrun, model_put, model_get };

#ifdef WITH_SIM
const ECDSP_PORT *sim_port(void);
extern unsigned long long sim_total;
#endif

static int fails, checks;
static unsigned long rs = 12345;

static unsigned rnd8(void)
{
	rs = rs * 1103515245UL + 12345;
	return (unsigned)(rs >> 16) & 255;
}

static void rnd(unsigned char *b, size_t n)
{
	while (n--)
		*b++ = (unsigned char)rnd8();
}

static void check(int ok, const char *what, int curve, int round)
{
	checks++;
	if (!ok) {
		fails++;
		if (fails < 20)
			printf("FAIL %s, curve %d, round %d\n", what, curve, round);
	}
}

/* a scalar below the order, not 0 */
static void scalar(unsigned char *x, int curve, size_t *len)
{
	size_t ol;
	const unsigned char *o = br_ec_all_m31.order(curve, &ol);
	do {
		rnd(x, ol);
		x[0] &= o[0];
	} while (memcmp(x, o, ol) >= 0 || x[ol - 1] == 0);
	*len = ol;
	if (rnd8() < 30) {			/* sometimes shorter, or with leading zeros */
		size_t k = 1 + rnd8() % (ol - 1);
		memset(x, 0, k);
		if (rnd8() & 1) {
			memmove(x, x + k, ol - k);
			*len = ol - k;
		}
	}
}

/* in the simulator: the DSP's time for each kind of operation, the
   first time it comes */
static int sim;
static void note(const char *what, int on_dsp, unsigned long ms)
{
	(void)ms;
	if (!on_dsp)
		printf("FAIL %s not on the DSP\n", what), fails++;
#ifdef WITH_SIM
	if (sim) {
		static char seen[16][40];
		static unsigned long long last;
		int i;
		for (i = 0; i < 16 && seen[i][0] && strcmp(seen[i], what); i++)
			;
		if (i < 16 && !seen[i][0]) {
			strcpy(seen[i], what);
			printf("  %-16s %5.0f ms on the DSP\n", what, (sim_total - last) / 32e3);
		}
		last = sim_total;
	}
#endif
}

int main(int argc, char **argv)
{
	static const int cs[] = { BR_EC_secp256r1, BR_EC_secp384r1, BR_EC_secp521r1, BR_EC_curve25519 };
	int rounds = 6, ci, r;
	if (argc > 1 && !strcmp(argv[1], "-s"))
		sim = 1, argc--, argv++;
	if (argc > 1)
		rounds = atoi(argv[1]);
	ecdsp_port = &model_port;
#ifdef WITH_SIM
	if (sim)
		ecdsp_port = sim_port();
#else
	if (sim) {
		puts("built without the simulator");
		return 1;
	}
#endif
	ecdsp_note = note;

	/* RFC 7748, 5.2 */
	{
		static const char *v[][3] = {
			{ "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",
			  "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",
			  "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552" },
			{ "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d",
			  "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493",
			  "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957" } };
		int i, k;
		for (i = 0; i < 2; i++) {
			unsigned char s[32], u[32], o[32];
			for (k = 0; k < 32; k++) {
				sscanf(v[i][0] + 2 * k, "%2hhx", &s[k]);
				sscanf(v[i][1] + 2 * k, "%2hhx", &u[k]);
				sscanf(v[i][2] + 2 * k, "%2hhx", &o[k]);
			}
			check(ecdsp_impl.mul(u, 32, s, 32, BR_EC_curve25519) && !memcmp(u, o, 32), "RFC 7748",
			      29, i);
		}
	}

	for (ci = 0; ci < 4; ci++) {
		int c = cs[ci];
		for (r = 0; r < rounds; r++) {
			unsigned char x[66], y[66], A[133], B[133], A2[133], B2[133];
			size_t xl, yl, gl, l1, l2;
			scalar(x, c, &xl);
			scalar(y, c, &yl);
			/* mulgen */
			l1 = ecdsp_impl.mulgen(A, x, xl, c);
			l2 = br_ec_all_m31.mulgen(A2, x, xl, c);
			check(l1 == l2 && !memcmp(A, A2, l1), "mulgen", c, r);
			gl = l1;
			/* mul, of a point that isn't the generator */
			memcpy(B, A, gl);
			memcpy(B2, A, gl);
			check(ecdsp_impl.mul(B, gl, y, yl, c) == br_ec_all_m31.mul(B2, gl, y, yl, c) &&
			      !memcmp(B, B2, gl), "mul", c, r);
			if (c == BR_EC_curve25519) {
				/* u at or above p: reduced */
				memset(B, 0xff, 32);
				B[0] = 0xee;
				memcpy(B2, B, 32);
				check(ecdsp_impl.mul(B, 32, y, yl, c) == br_ec_all_m31.mul(B2, 32, y, yl, c) &&
				      !memcmp(B, B2, 32), "mul u >= p", c, r);
				continue;
			}
			/* muladd: x A + y G, and x A + y B */
			memcpy(A2, A, gl);
			check(ecdsp_impl.muladd(A, 0, gl, x, xl, y, yl, c) ==
			      br_ec_all_m31.muladd(A2, 0, gl, x, xl, y, yl, c) && !memcmp(A, A2, gl),
			      "muladd G", c, r);
			memcpy(A2, A, gl);
			check(ecdsp_impl.muladd(A, B, gl, y, yl, x, xl, c) ==
			      br_ec_all_m31.muladd(A2, B, gl, y, yl, x, xl, c) && !memcmp(A, A2, gl),
			      "muladd B", c, r);
			/* the exceptional sums: x A + y A with x = y (a doubling) and
			   x = n - y (the point at infinity) */
			{
				size_t ol;
				const unsigned char *o = br_ec_all_m31.order(c, &ol);
				unsigned char ny[66], P[133];
				int k, br = 0;
				unsigned char yy[66];
				memset(yy, 0, ol - yl);
				memcpy(yy + ol - yl, y, yl);
				for (k = (int)ol - 1; k >= 0; k--) {
					int v = o[k] - yy[k] - br;
					br = v < 0;
					ny[k] = (unsigned char)v;
				}
				memcpy(P, B, gl);
				memcpy(A2, B, gl);
				check(ecdsp_impl.muladd(P, B, gl, yy, ol, yy, ol, c) ==
				      br_ec_all_m31.muladd(A2, B, gl, yy, ol, yy, ol, c) && !memcmp(P, A2, gl),
				      "muladd doubling", c, r);
				memcpy(P, B, gl);
				check(ecdsp_impl.muladd(P, B, gl, yy, ol, ny, ol, c) == 0, "muladd infinity", c, r);
			}
			/* a point off the curve */
			memcpy(B, A, gl);
			B[gl - 1] ^= 1;
			check(ecdsp_impl.mul(B, gl, y, yl, c) == 0, "off the curve", c, r);
			/* whole signatures: BearSSL's check with ecdsp_impl (muladd on
			   the DSP), and ecdsp_vrfy (all of it on the DSP), with hashes
			   shorter and longer than the order */
			{
				static const br_hash_class *const hf[] = { &br_sha256_vtable, &br_sha384_vtable,
									  &br_sha512_vtable };
				br_ec_private_key sk;
				br_ec_public_key pk;
				unsigned char kbuf[66], pub[133], hash[64], sig[200], der[200];
				size_t sl, dl, hl;
				const br_hash_class *h = hf[r % 3];
				hl = (h->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;
				memcpy(kbuf, x, xl);
				sk.curve = c;
				sk.x = kbuf;
				sk.xlen = xl;
				pk.curve = c;
				pk.q = pub;
				pk.qlen = br_ec_compute_pub(&br_ec_all_m31, 0, pub, &sk);
				rnd(hash, sizeof(hash));
				sl = br_ecdsa_i31_sign_raw(&br_ec_all_m31, h, hash, &sk, sig);
				check(sl && br_ecdsa_i31_vrfy_raw(&ecdsp_impl, hash, hl, &pk, sig, sl), "ECDSA good", c, r);
				check(ecdsp_vrfy_raw(0, hash, hl, &pk, sig, sl), "ecdsp_vrfy good", c, r);
				memcpy(der, sig, sl);
				dl = br_ecdsa_raw_to_asn1(der, sl);
				check(ecdsp_vrfy_asn1(0, hash, hl, &pk, der, dl), "ecdsp_vrfy_asn1 good", c, r);
				{
					/* a bit the check reads: the order's length at most */
					size_t ol;
					br_ec_all_m31.order(c, &ol);
					hash[rnd8() % (hl < ol ? hl : ol - 1)] ^= 1 << (rnd8() & 7);
				}
				check(!ecdsp_vrfy_asn1(0, hash, hl, &pk, der, dl), "ecdsp_vrfy_asn1 other hash", c, r);
				sig[rnd8() % sl] ^= 1 << (rnd8() & 7);
				check(!br_ecdsa_i31_vrfy_raw(&ecdsp_impl, hash, hl, &pk, sig, sl), "ECDSA bad", c, r);
				check(!ecdsp_vrfy_raw(0, hash, hl, &pk, sig, sl), "ecdsp_vrfy bad", c, r);
				memset(sig, 0, sl / 2);		/* r = 0 */
				check(!ecdsp_vrfy_raw(0, hash, hl, &pk, sig, sl), "ecdsp_vrfy r = 0", c, r);
			}
		}
	}
	printf("ecdsp: %d of %d checks passed (%s)\n", checks - fails, checks, sim ? "ec.a56 on Hatari's DSP core" : "model");
	if (!sim)
		printf("commands: %lu products, %lu sums, %lu differences\n", model_ops[3], model_ops[4], model_ops[5]);
	return fails != 0;
}
