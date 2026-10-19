/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ecdsp.c - the elliptic curves of a TLS handshake worked out on the
 * Falcon's DSP56001. The DSP (dsp/ec.a56) keeps 128 numbers modulo the
 * curve's prime and multiplies, adds and subtracts them as it is told,
 * a command word each; this file is the 68030's side: the curve formulas,
 * as streams of those commands.
 *
 * On a 50 MHz 68030 BearSSL takes about 120 million instructions to
 * check one P-384 signature (its P-384 code is generic); the DSP's
 * multiplier does a product of two 23-bit digits and adds it up in one
 * instruction, so a whole signature takes it under a second.
 *
 *  - P-256, P-384 and P-521 (y^2 = x^3 - 3x + b): Jacobian coordinates,
 *    doubling dbl-2001-b and addition add-1998-cmo-2 (the Explicit-
 *    Formulas Database); fixed 4-bit windows, the two multiplications of
 *    a signature check interleaved (Shamir). Numbers stay in Montgomery
 *    form on the DSP, converted on the way in and out.
 *  - Curve25519: RFC 7748's Montgomery ladder, BearSSL's sequence.
 *  - The secret scalars (the key exchange) give the DSP the same
 *    commands whatever their bits: a window of 0 still adds, into a
 *    point thrown away, and the ladder swaps register numbers, not
 *    numbers. Only leading zero windows are skipped.
 *  - Every point that comes in is checked to be on its curve.
 */
#include <string.h>
#include "bearssl.h"
#include "plat.h"
#include "ecdsp.h"

const ECDSP_PORT *ecdsp_port;
int ecdsp_failed;
void (*ecdsp_note)(const char *what, int on_dsp, unsigned long ms);

#define DMASK 0x7fffffUL
#define MAXN  23		/* digits: P-521 */
#define MAXB  66		/* bytes of a number: P-521 */

enum { CFG = 1, LD, MUL, ADD, SUB, OUT, ISZ };

/* the DSP's registers */
#define RR    0			/* R^2 mod p, for the way into Montgomery form */
#define ONE   1			/* 1 as it is: a product with it is the way out */
#define MONE  2			/* 1 in Montgomery form */
#define CB    3			/* b, or Curve25519's a24 */
#define ZERO  4			/* never written */
#define T(i)  (5 + (i))		/* temporaries T(0) .. T(11) */
#define ACC   20		/* the point being worked out: X, Y, Z */
#define SCR   23		/* a point thrown away */
#define TA    26		/* windows: k * a point, k = 1..15, at TA + 3(k - 1) */
#define TB    71		/* .. 115: the same for the second point */
#define W(k)  (TA + (k) - 1)	/* z^k for an inversion, in TA's place */

/* a point's three coordinates */
#define PX(r) (r)
#define PY(r) ((r) + 1)
#define PZ(r) ((r) + 2)

static const unsigned char p256_p[32] = {
	0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const unsigned char p256_b[32] = {
	0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd, 0x55,
	0x76, 0x98, 0x86, 0xbc, 0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
	0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b
};
static const unsigned char p384_p[48] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
};
static const unsigned char p384_b[48] = {
	0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4, 0x98, 0x8e, 0x05, 0x6b,
	0xe3, 0xf8, 0x2d, 0x19, 0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12,
	0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a, 0xc6, 0x56, 0x39, 0x8d,
	0x8a, 0x2e, 0xd1, 0x9d, 0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef
};
static const unsigned char p521_p[66] = {
	0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static const unsigned char p521_b[66] = {
	0x00, 0x51, 0x95, 0x3e, 0xb9, 0x61, 0x8e, 0x1c, 0x9a, 0x1f, 0x92, 0x9a,
	0x21, 0xa0, 0xb6, 0x85, 0x40, 0xee, 0xa2, 0xda, 0x72, 0x5b, 0x99, 0xb3,
	0x15, 0xf3, 0xb8, 0xb4, 0x89, 0x91, 0x8e, 0xf1, 0x09, 0xe1, 0x56, 0x19,
	0x39, 0x51, 0xec, 0x7e, 0x93, 0x7b, 0x16, 0x52, 0xc0, 0xbd, 0x3b, 0xb1,
	0xbf, 0x07, 0x35, 0x73, 0xdf, 0x88, 0x3d, 0x2c, 0x34, 0xf1, 0xef, 0x45,
	0x1f, 0xd4, 0x6b, 0x50, 0x3f, 0x00
};
static const unsigned char c25519_p[32] = {
	0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xed
};

/* numbers modulo m: a curve's prime, or its order */
typedef struct {
	const unsigned char *m;		/* big-endian */
	int bytes, bits;
	/* worked out the first time */
	int n;				/* 23-bit digits */
	unsigned long mp;		/* -m^-1 mod 2^23 */
	unsigned long md[MAXN], rr[MAXN];	/* m; R^2 mod m */
} FIELD;

typedef struct {
	int id;				/* BR_EC_... */
	const char *name;
	const unsigned char *b;		/* big-endian; 0 for Curve25519 */
	FIELD fp, fn;			/* the prime's numbers, the order's */
} CURVE;

static CURVE curves[] = {
	{ BR_EC_secp256r1, "P-256", p256_b, { p256_p, 32, 256, 0, 0, {0}, {0} }, { 0, 0, 0, 0, 0, {0}, {0} } },
	{ BR_EC_secp384r1, "P-384", p384_b, { p384_p, 48, 384, 0, 0, {0}, {0} }, { 0, 0, 0, 0, 0, {0}, {0} } },
	{ BR_EC_secp521r1, "P-521", p521_b, { p521_p, 66, 521, 0, 0, {0}, {0} }, { 0, 0, 0, 0, 0, {0}, {0} } },
	{ BR_EC_curve25519, "Curve25519", 0, { c25519_p, 32, 255, 0, 0, {0}, {0} }, { 0, 0, 0, 0, 0, {0}, {0} } },
};

/* ---------------- numbers on the 68030 ---------------- */

/* big-endian bytes -> n 23-bit digits, little-endian */
static void be_digits(unsigned long *d, int n, const unsigned char *b, int len)
{
	unsigned long acc = 0;
	int have = 0, k = 0;
	memset(d, 0, n * sizeof(*d));
	while (len-- && k < n) {
		acc |= (unsigned long)b[len] << have;
		have += 8;
		if (have >= 23) {
			d[k++] = acc & DMASK;
			acc >>= 23;
			have -= 23;
		}
	}
	if (k < n)
		d[k] = acc & DMASK;
}

static void digits_be(unsigned char *b, int len, const unsigned long *d, int n)
{
	unsigned long acc = 0;
	int have = 0, k = 0;
	while (len--) {
		if (have < 8 && k < n) {
			acc |= d[k++] << have;
			have += 23;
		}
		b[len] = (unsigned char)acc;
		acc >>= 8;
		have -= 8;
	}
}

/* x = 2x mod p, x < p */
static void dbl_mod(unsigned long *x, const unsigned long *p, int n)
{
	unsigned long t[MAXN], c = 0;
	long br = 0;
	int i;
	for (i = 0; i < n; i++) {
		unsigned long v = (x[i] << 1) | c;
		c = v >> 23;
		x[i] = v & DMASK;
	}
	for (i = 0; i < n; i++) {
		long v = (long)x[i] - (long)p[i] + br;
		br = v < 0 ? -1 : 0;
		t[i] = (unsigned long)v & DMASK;
	}
	if (c || !br)		/* 2x >= p */
		memcpy(x, t, n * sizeof(*x));
}

static void field_init(FIELD *f)
{
	unsigned long inv;
	int i;
	if (f->n)
		return;
	f->n = (f->bits + 2 + 22) / 23;		/* R = 2^(23n) >= 4m */
	be_digits(f->md, f->n, f->m, f->bytes);
	/* Newton: each step doubles the right bits (3 to start with) */
	inv = f->md[0];
	for (i = 0; i < 4; i++)
		inv = inv * (2 - f->md[0] * inv);
	f->mp = (0UL - inv) & DMASK;
	/* R^2 = 2^(46n) mod m, by doubling 1 */
	memset(f->rr, 0, sizeof(f->rr));
	f->rr[0] = 1;
	for (i = 0; i < 46 * f->n; i++)
		dbl_mod(f->rr, f->md, f->n);
}

static CURVE *curve_of(int id)
{
	CURVE *c = 0;
	int i;
	for (i = 0; i < (int)(sizeof(curves) / sizeof(curves[0])); i++)
		if (curves[i].id == id)
			c = &curves[i];
	if (!c || c->fp.n)
		return c;
	field_init(&c->fp);
	if (c->b) {
		size_t len;
		c->fn.m = br_ec_all_m31.order(id, &len);
		c->fn.bytes = (int)len;
		c->fn.bits = c->fn.bytes * 8;
		for (i = 0; i < 8 && !(c->fn.m[0] & (0x80 >> i)); i++)
			c->fn.bits--;
		field_init(&c->fn);
	}
	return c;
}

/* a < p, both big-endian, len bytes */
static int below(const unsigned char *a, const unsigned char *p, int len)
{
	int i;
	for (i = 0; i < len; i++)
		if (a[i] != p[i])
			return a[i] < p[i];
	return 0;
}

static int all_zero(const unsigned char *x, size_t len)
{
	unsigned char z = 0;
	while (len--)
		z |= *x++;
	return !z;
}

/* ---------------- commands ---------------- */

static const ECDSP_PORT *port;
static const FIELD *fd;		/* the numbers the DSP has */
static const CURVE *cur;

static void op(int o, int d, int a, int b)
{
	port->put(((unsigned long)o << 21) | ((unsigned long)d << 14) | ((unsigned long)a << 7) |
		  (unsigned long)b);
}

#define fmul(d, a, b) op(MUL, d, a, b)
#define fadd(d, a, b) op(ADD, d, a, b)
#define fsub(d, a, b) op(SUB, d, a, b)
#define fcopy(d, a)   op(ADD, d, a, ZERO)

static void ld(int r, const unsigned long *d)
{
	int i;
	op(LD, r, 0, 0);
	for (i = 0; i < fd->n; i++)
		port->put(d[i]);
}

/* a big-endian number below 2p into r, in Montgomery form */
static void ld_be(int r, const unsigned char *b, int len)
{
	unsigned long d[MAXN];
	be_digits(d, fd->n, b, len);
	ld(r, d);
	fmul(r, r, RR);
}

/* r reduced, as len big-endian bytes */
static void out_raw(unsigned char *b, int len, int r)
{
	unsigned long d[MAXN];
	int i;
	op(OUT, 0, r, 0);
	for (i = 0; i < fd->n; i++)
		d[i] = port->get();
	digits_be(b, len, d, fd->n);
}

/* the same, out of Montgomery form */
static void out_be(unsigned char *b, int len, int r)
{
	fmul(r, r, ONE);
	out_raw(b, len, r);
}

static int isz(int r)
{
	op(ISZ, 0, r, 0);
	return port->get() == 1;
}

/* the DSP's numbers modulo f's; every register 0, then the constants */
static void setup(const FIELD *f)
{
	unsigned long d[MAXN];
	int i;
	fd = f;
	port->put((unsigned long)CFG << 21);
	port->put((unsigned long)f->n);
	port->put(f->mp);
	for (i = 0; i < f->n; i++)
		port->put(f->md[i]);
	ld(RR, f->rr);
	memset(d, 0, sizeof(d));
	d[0] = 1;
	ld(ONE, d);
	fmul(MONE, ONE, RR);
	if (f != &cur->fp)
		return;
	if (cur->b)
		ld_be(CB, cur->b, f->bytes);
	else {
		d[0] = 121665;
		ld(CB, d);
		fmul(CB, CB, RR);
	}
}

/* o = z^(m-2) = 1/z: 4-bit windows of m - 2 (it doesn't depend on any
   secret, so zero windows are simply skipped) */
static void inv(int o, int z)
{
	unsigned char e[MAXB];
	int k, i, started = 0;
	memcpy(e, fd->m, fd->bytes);
	e[fd->bytes - 1] -= 2;		/* no borrow: every prime and order here ends in 0x09 or more */
	fcopy(W(1), z);
	for (k = 2; k < 16; k++)
		fmul(W(k), W(k - 1), W(1));
	for (i = 0; i < 2 * fd->bytes; i++) {
		int nib = (e[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
		if (started) {
			fmul(o, o, o);
			fmul(o, o, o);
			fmul(o, o, o);
			fmul(o, o, o);
			if (nib)
				fmul(o, o, W(nib));
		} else if (nib) {
			fcopy(o, W(nib));
			started = 1;
		}
	}
}

/* ---------------- P-256, P-384, P-521 ---------------- */

/* o = 2i (o may be i). dbl-2001-b, for a = -3: 3M + 5S */
static void pdbl(int o, int i)
{
	fmul(T(0), PZ(i), PZ(i));	/* delta = Z^2 */
	fmul(T(1), PY(i), PY(i));	/* gamma = Y^2 */
	fmul(T(2), PX(i), T(1));	/* beta = X gamma */
	fsub(T(3), PX(i), T(0));
	fadd(T(4), PX(i), T(0));
	fmul(T(3), T(3), T(4));
	fadd(T(4), T(3), T(3));
	fadd(T(3), T(4), T(3));		/* alpha = 3 (X - delta)(X + delta) */
	fadd(T(5), PY(i), PZ(i));
	fmul(T(5), T(5), T(5));
	fsub(T(5), T(5), T(1));
	fsub(PZ(o), T(5), T(0));	/* Z3 = (Y + Z)^2 - gamma - delta */
	fmul(T(6), T(3), T(3));
	fadd(T(2), T(2), T(2));
	fadd(T(2), T(2), T(2));		/* 4 beta */
	fadd(T(7), T(2), T(2));
	fsub(PX(o), T(6), T(7));	/* X3 = alpha^2 - 8 beta */
	fsub(T(2), T(2), PX(o));
	fmul(T(2), T(3), T(2));
	fmul(T(1), T(1), T(1));
	fadd(T(1), T(1), T(1));
	fadd(T(1), T(1), T(1));
	fadd(T(1), T(1), T(1));
	fsub(PY(o), T(2), T(1));	/* Y3 = alpha (4 beta - X3) - 8 gamma^2 */
}

/* o = i + j (o may be i, not j). add-1998-cmo-2: 12M + 4S. check: the
   points may be equal or opposite (a signature check, where nothing is
   secret): 1 if the sum is the point at infinity, and when i = j the
   doubling is done instead */
static int padd(int o, int i, int j, int check)
{
	fmul(T(0), PZ(i), PZ(i));	/* Z1Z1 */
	fmul(T(1), PZ(j), PZ(j));	/* Z2Z2 */
	fmul(T(2), PX(i), T(1));	/* U1 */
	fmul(T(3), PX(j), T(0));	/* U2 */
	fmul(T(4), PY(i), PZ(j));
	fmul(T(4), T(4), T(1));		/* S1 */
	fmul(T(5), PY(j), PZ(i));
	fmul(T(5), T(5), T(0));		/* S2 */
	fsub(T(3), T(3), T(2));		/* H */
	fsub(T(5), T(5), T(4));		/* R */
	if (check && isz(T(3))) {
		if (!isz(T(5)))
			return 1;
		pdbl(o, i);
		return 0;
	}
	fmul(T(6), PZ(i), PZ(j));
	fmul(T(0), T(3), T(3));		/* HH */
	fmul(T(1), T(3), T(0));		/* HHH */
	fmul(T(2), T(2), T(0));		/* V = U1 HH */
	fmul(T(7), T(5), T(5));
	fsub(T(7), T(7), T(1));
	fadd(T(8), T(2), T(2));
	fsub(PX(o), T(7), T(8));	/* X3 = R^2 - HHH - 2V */
	fsub(T(2), T(2), PX(o));
	fmul(T(2), T(5), T(2));
	fmul(T(4), T(4), T(1));
	fsub(PY(o), T(2), T(4));	/* Y3 = R (V - X3) - S1 HHH */
	fmul(PZ(o), T(6), T(3));	/* Z3 = Z1 Z2 H */
	return 0;
}

static void pcopy(int o, int i)
{
	fcopy(PX(o), PX(i));
	fcopy(PY(o), PY(i));
	fcopy(PZ(o), PZ(i));
}

/* an encoded point (04 X Y) into r: 0 unless it is on the curve */
static int pload(int r, const unsigned char *G, size_t Glen)
{
	int len = fd->bytes;
	if (Glen != (size_t)(1 + 2 * len) || G[0] != 4 || !below(G + 1, fd->m, len) ||
	    !below(G + 1 + len, fd->m, len))
		return 0;
	ld_be(PX(r), G + 1, len);
	ld_be(PY(r), G + 1 + len, len);
	fcopy(PZ(r), MONE);
	fmul(T(0), PY(r), PY(r));
	fmul(T(1), PX(r), PX(r));
	fmul(T(1), T(1), PX(r));
	fsub(T(1), T(1), PX(r));
	fsub(T(1), T(1), PX(r));
	fsub(T(1), T(1), PX(r));
	fadd(T(1), T(1), CB);
	fsub(T(0), T(0), T(1));
	return isz(T(0));
}

/* t + 3(k - 1) = k * the point at t (k = 1..15): from 2 on, nothing is
   the point at infinity, or equal to the point added (the curves have a
   prime order, far above 15) */
static void table(int t)
{
	int k;
	pdbl(t + 3, t);
	for (k = 3; k < 16; k++)
		padd(t + 3 * (k - 1), t + 3 * (k - 2), t, 0);
}

static int nibble(const unsigned char *x, size_t i)
{
	return (x[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
}

/* ACC = x * the point at TA. x is secret: every window adds */
static void smul(const unsigned char *x, size_t xlen)
{
	size_t i;
	int started = 0;
	for (i = 0; i < 2 * xlen; i++) {
		int nib = nibble(x, i);
		if (started) {
			int z = nib == 0;
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
			padd(z ? SCR : ACC, ACC, TA + 3 * (nib + z - 1), 0);
		} else if (nib) {
			pcopy(ACC, TA + 3 * (nib - 1));
			started = 1;
		}
	}
}

/* ACC as an encoded point into G */
static void pstore(unsigned char *G)
{
	int len = fd->bytes;
	inv(T(8), PZ(ACC));
	fmul(T(9), T(8), T(8));
	fmul(T(10), PX(ACC), T(9));
	fmul(T(9), T(9), T(8));
	fmul(T(11), PY(ACC), T(9));
	G[0] = 4;
	out_be(G + 1, len, T(10));
	out_be(G + 1 + len, len, T(11));
}

/* ---------------- Curve25519 ---------------- */

static void x25519(unsigned char *G, const unsigned char *x, size_t xlen)
{
	unsigned char k[32], u[32];
	unsigned long d[MAXN];
	int x1 = ACC, x2 = ACC + 1, z2 = ACC + 2, x3 = SCR, z3 = SCR + 1;
	int A = T(0), AA = T(1), B = T(2), BB = T(3), E = T(4), C = T(5), D = T(6), DA = T(7),
	    CBr = T(8), i, j;
	unsigned swap = 0;
	memcpy(k, x, xlen);
	memset(k + xlen, 0, sizeof(k) - xlen);
	k[0] &= 0xF8;
	k[31] &= 0x7F;
	k[31] |= 0x40;
	for (i = 0; i < 32; i++)		/* little-endian */
		u[i] = G[31 - i];
	u[0] &= 0x7F;				/* below 2^255 < 2p */
	be_digits(d, fd->n, u, 32);
	ld(x1, d);
	fmul(x1, x1, RR);
	fcopy(x2, MONE);
	fcopy(x3, x1);
	fcopy(z3, MONE);			/* z2 = 0, as every register starts */
	for (i = 254; i >= 0; i--) {
		unsigned kt = (k[i >> 3] >> (i & 7)) & 1, m;
		swap ^= kt;
		m = 0U - swap;
		j = (x2 ^ x3) & m;
		x2 ^= j;
		x3 ^= j;
		j = (z2 ^ z3) & m;
		z2 ^= j;
		z3 ^= j;
		swap = kt;
		fadd(A, x2, z2);
		fmul(AA, A, A);
		fsub(B, x2, z2);
		fmul(BB, B, B);
		fsub(E, AA, BB);
		fadd(C, x3, z3);
		fsub(D, x3, z3);
		fmul(DA, D, A);
		fmul(CBr, C, B);
		fadd(x3, DA, CBr);
		fmul(x3, x3, x3);
		fsub(z3, DA, CBr);
		fmul(z3, z3, z3);
		fmul(z3, z3, x1);
		fmul(x2, AA, BB);
		fmul(z2, CB, E);
		fadd(z2, z2, AA);
		fmul(z2, E, z2);
	}
	if (swap) {				/* the last swap: k[0] & 7 is 0, so it's public */
		j = x2;
		x2 = x3;
		x3 = j;
		j = z2;
		z2 = z3;
		z3 = j;
	}
	inv(T(9), z2);
	fmul(x2, x2, T(9));
	out_be(u, 32, x2);
	for (i = 0; i < 32; i++)
		G[i] = u[31 - i];
}

/* ---------------- the jobs, run with the DSP held ---------------- */

typedef struct {
	int what;			/* 0 mul, 1 mulgen, 2 muladd, 3 ECDSA */
	CURVE *c;
	unsigned char *G;		/* the point in and out */
	size_t Glen;
	const unsigned char *B;		/* muladd's second point, 0: the generator */
	const unsigned char *x, *y;
	size_t xlen, ylen;
	/* ECDSA: the key, and r, s and the hash, as long as the order */
	const unsigned char *q;
	unsigned char r[MAXB], s[MAXB], e[MAXB];
	uint32_t res;
} JOB;

/* ACC += the point at t, ACC being the point at infinity if inf; the
   answer: whether it is now */
static int add_window(int inf, int t)
{
	if (inf) {
		pcopy(ACC, t);
		return 0;
	}
	return padd(ACC, ACC, t, 1);
}

/* A = x A + y B (B 0: the generator), the points being public: the
   sums check for equal and opposite points */
static void muladd(JOB *j)
{
	size_t gl;
	const unsigned char *B = j->B ? j->B : br_ec_all_m31.generator(j->c->id, &gl);
	size_t i, n = j->xlen > j->ylen ? j->xlen : j->ylen;
	int inf = 1;
	if (!pload(TA, j->G, j->Glen) || !pload(TB, B, j->Glen))
		return;
	table(TA);
	table(TB);
	for (i = 0; i < 2 * n; i++) {
		size_t px = i - 2 * (n - j->xlen), py = i - 2 * (n - j->ylen);
		int dx = i >= 2 * (n - j->xlen) ? nibble(j->x, px) : 0;
		int dy = i >= 2 * (n - j->ylen) ? nibble(j->y, py) : 0;
		if (!inf) {
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
			pdbl(ACC, ACC);
		}
		if (dx)
			inf = add_window(inf, TA + 3 * (dx - 1));
		if (dy)
			inf = add_window(inf, TB + 3 * (dy - 1));
	}
	if (inf)
		return;
	pstore(j->G);
	j->res = 1;
}

/* ECDSA, the whole check on the DSP: 1/s, e/s and r/s modulo the order;
   then (r/s) Q + (e/s) G modulo the prime, whose x, modulo the order,
   must be r */
static void vrfy(JOB *j)
{
	const FIELD *fn = &cur->fn;
	int len = fn->bytes, i, br;
	unsigned char u1[MAXB], u2[MAXB], X[1 + 2 * MAXB], t[MAXB];
	unsigned long d[MAXN];
	j->res = 0;
	setup(fn);
	ld_be(T(0), j->s, len);			/* s R */
	inv(T(1), T(0));			/* R / s */
	be_digits(d, fn->n, j->e, len);
	ld(T(2), d);
	fmul(T(2), T(2), T(1));			/* e / s */
	be_digits(d, fn->n, j->r, len);
	ld(T(3), d);
	fmul(T(3), T(3), T(1));			/* r / s */
	out_raw(u1, len, T(2));
	out_raw(u2, len, T(3));
	if (all_zero(u1, len))
		return;
	setup(&cur->fp);
	memcpy(X, j->q, j->Glen);
	j->G = X;
	j->B = 0;
	j->x = u2;
	j->xlen = len;
	j->y = u1;
	j->ylen = len;
	muladd(j);
	if (!j->res)
		return;
	/* x mod n: x - n if that doesn't borrow (x < p < 2n) */
	for (i = len - 1, br = 0; i >= 0; i--) {
		int v = X[1 + i] - fn->m[i] - br;
		br = v < 0;
		t[i] = (unsigned char)v;
	}
	j->res = !memcmp(br ? X + 1 : t, j->r, len);
}

static void work(void *ctx)
{
	JOB *j = ctx;
	port = ecdsp_port;
	cur = j->c;
	if (j->what == 3) {
		vrfy(j);
		return;
	}
	setup(&cur->fp);
	if (j->c->id == BR_EC_curve25519) {
		x25519(j->G, j->x, j->xlen);
		j->res = 1;
		return;
	}
	j->res = 0;
	if (j->what == 2)
		muladd(j);
	else if (pload(TA, j->G, j->Glen)) {
		table(TA);
		smul(j->x, j->xlen);
		pstore(j->G);
		j->res = 1;
	}
}

/* 1 if the DSP did it (j->res is the answer) */
static int on_dsp(JOB *j, const char *what)
{
	unsigned long t0 = pf_ms();
	int ok;
	if (!ecdsp_port || !j->c)
		return 0;
	ecdsp_failed = 0;
	ok = ecdsp_port->run(work, j) && !ecdsp_failed;
	if (ecdsp_note) {
		char msg[40];
		strcpy(msg, what);
		strcat(msg, " ");
		strcat(msg, j->c->name);
		ecdsp_note(j->c->id == BR_EC_curve25519 ? (j->what ? "X25519 key" : "X25519") : msg, ok,
			   pf_ms() - t0);
	}
	return ok;
}

static const unsigned char *api_generator(int curve, size_t *len)
{
	return br_ec_all_m31.generator(curve, len);
}

static const unsigned char *api_order(int curve, size_t *len)
{
	return br_ec_all_m31.order(curve, len);
}

static size_t api_xoff(int curve, size_t *len)
{
	return br_ec_all_m31.xoff(curve, len);
}

static uint32_t api_mul(unsigned char *G, size_t Glen, const unsigned char *x, size_t xlen, int curve)
{
	JOB j;
	memset(&j, 0, sizeof(j));
	j.c = curve_of(curve);
	j.G = G;
	j.Glen = Glen;
	j.x = x;
	j.xlen = xlen;
	if (j.c && curve == BR_EC_curve25519 && (Glen != 32 || xlen > 32))
		return 0;
	if (j.c && xlen > (size_t)j.c->fp.bytes)
		j.c = 0;
	{
		unsigned char keep[1 + 2 * MAXB];
		if (Glen <= sizeof(keep))
			memcpy(keep, G, Glen);
		if (Glen <= sizeof(keep) && on_dsp(&j, "ECDH"))
			return j.res;
		if (Glen <= sizeof(keep))
			memcpy(G, keep, Glen);
	}
	return br_ec_all_m31.mul(G, Glen, x, xlen, curve);
}

static size_t api_mulgen(unsigned char *R, const unsigned char *x, size_t xlen, int curve)
{
	JOB j;
	const unsigned char *G;
	size_t Glen;
	memset(&j, 0, sizeof(j));
	j.c = curve_of(curve);
	G = br_ec_all_m31.generator(curve, &Glen);
	if (j.c && xlen <= (size_t)j.c->fp.bytes) {
		memcpy(R, G, Glen);
		j.what = 1;
		j.G = R;
		j.Glen = Glen;
		j.x = x;
		j.xlen = xlen;
		if (on_dsp(&j, "ECDH key"))
			return Glen;
	}
	return br_ec_all_m31.mulgen(R, x, xlen, curve);
}

static uint32_t api_muladd(unsigned char *A, const unsigned char *B, size_t len, const unsigned char *x,
			   size_t xlen, const unsigned char *y, size_t ylen, int curve)
{
	JOB j;
	memset(&j, 0, sizeof(j));
	j.c = curve_of(curve);
	if (curve == BR_EC_curve25519)
		return 0;
	if (j.c && (xlen > (size_t)j.c->fp.bytes || ylen > (size_t)j.c->fp.bytes || len > 1 + 2 * MAXB))
		j.c = 0;
	if (j.c) {
		unsigned char keep[1 + 2 * MAXB];
		if (all_zero(x, xlen) || all_zero(y, ylen))
			return 0;
		memcpy(keep, A, len);
		j.what = 2;
		j.G = A;
		j.Glen = len;
		j.B = B;
		j.x = x;
		j.xlen = xlen;
		j.y = y;
		j.ylen = ylen;
		if (on_dsp(&j, "ECDSA"))
			return j.res;
		memcpy(A, keep, len);
	}
	return br_ec_all_m31.muladd(A, B, len, x, xlen, y, ylen, curve);
}

/* len bytes of v (vlen of them) into o; 0 unless 0 < v < m */
static int scalar_in(unsigned char *o, const unsigned char *v, size_t vlen, const FIELD *f)
{
	size_t len = (size_t)f->bytes;
	while (vlen > len) {
		if (*v++)
			return 0;
		vlen--;
	}
	memset(o, 0, len - vlen);
	memcpy(o + len - vlen, v, vlen);
	return !all_zero(o, len) && below(o, f->m, (int)len);
}

uint32_t ecdsp_vrfy_raw(const br_ec_impl *impl, const void *hash, size_t hash_len, const br_ec_public_key *pk,
			const void *sig, size_t sig_len)
{
	JOB j;
	const unsigned char *h = hash;
	size_t rl = sig_len >> 1, len, i;
	int bits;
	(void)impl;
	memset(&j, 0, sizeof(j));
	j.c = pk->curve == BR_EC_curve25519 ? 0 : curve_of(pk->curve);
	if (!j.c || !ecdsp_port || (sig_len & 1) || pk->qlen != (size_t)(1 + 2 * j.c->fp.bytes))
		return br_ecdsa_i31_vrfy_raw(&br_ec_all_m31, hash, hash_len, pk, sig, sig_len);
	if (!scalar_in(j.r, sig, rl, &j.c->fn) || !scalar_in(j.s, (const unsigned char *)sig + rl, rl, &j.c->fn))
		return 0;
	/* the hash's leftmost bits, as many as the order has (bits2int) */
	len = (size_t)j.c->fn.bytes;
	bits = j.c->fn.bits;
	if (hash_len * 8 > (size_t)bits) {
		int sh = (int)(len * 8) - bits;
		memcpy(j.e, h, len);
		for (i = len; sh && i-- > 0;)
			j.e[i] = (unsigned char)((j.e[i] >> sh) | (i ? j.e[i - 1] << (8 - sh) : 0));
	} else {
		memset(j.e, 0, len - hash_len);
		memcpy(j.e + len - hash_len, h, hash_len);
	}
	j.what = 3;
	j.q = pk->q;
	j.Glen = pk->qlen;
	if (on_dsp(&j, "ECDSA"))
		return j.res;
	return br_ecdsa_i31_vrfy_raw(&br_ec_all_m31, hash, hash_len, pk, sig, sig_len);
}

uint32_t ecdsp_vrfy_asn1(const br_ec_impl *impl, const void *hash, size_t hash_len, const br_ec_public_key *pk,
			 const void *sig, size_t sig_len)
{
	unsigned char raw[2 * MAXB + 16];
	if (sig_len > sizeof(raw))
		return 0;
	memcpy(raw, sig, sig_len);
	sig_len = br_ecdsa_asn1_to_raw(raw, sig_len);
	if (!sig_len)
		return 0;
	return ecdsp_vrfy_raw(impl, hash, hash_len, pk, raw, sig_len);
}

const br_ec_impl ecdsp_impl = {
	(uint32_t)0x23800000,		/* P-256, P-384, P-521, Curve25519: br_ec_all_m31's */
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
