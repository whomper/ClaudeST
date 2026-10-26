/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tls.c - TLS 1.2 client on BearSSL 0.6 (third_party/bearssl), for
 * Falcon mode: the program talks TLS to the server itself, without the
 * Raspberry Pi gateway. ECal's session resumption, table-based AES and
 * fastest GHASH, with EMail's ChaCha20-first handshake.
 *
 * Choices for a 68030 with a DSP56001 next to it:
 *  - Servers are first asked for an RSA certificate (no ECDSA in the
 *    hello): checking RSA signatures is cheap with e = 65537 and the
 *    DSP does it in about 0.1 s. Only if a server has nothing but ECDSA
 *    does the caller connect again and accept it (tls_start's retry).
 *  - X25519 for the key exchange (0.5 s on a 50 MHz 68030), then
 *    ChaCha20-Poly1305 or AES-GCM.
 *  - One 16 KB buffer per connection (half duplex): mail protocols
 *    wait for the answer before they send again.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bearssl.h"
#include "plat.h"
#include "util.h"
#include "tls.h"
#ifdef TLS_030
#include "rsa030.h"
#endif
#include "tlsovl.h"

/* from BearSSL's inner.h */
#define BR_MAX_RSA_SIZE 4096
uint32_t br_rsa_pkcs1_sig_unpad(const unsigned char *sig, size_t sig_len,
				const unsigned char *hash_oid, size_t hash_len, unsigned char *hash_out);

const int tls_available = 1;
int tls_tz_minutes;
char tls_seed_path[200];
char tls_sigs_path[200];
char tls_roots_cache[200];
unsigned long (*tls_file_stamp)(const char *path);
long (*tls_read_at)(const char *path, long off, void *buf, long n);
unsigned long (*tls_file_info)(const char *path, long *size);
void (*tls_note)(const char *msg);
void (*tls_log)(const char *msg);
int (*tls_rsa_accel)(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		     const unsigned char *e, size_t elen);

#ifndef TLSOVL_BUILD
/* tlsovl.h's calls in a program with the TLS built in (EWeb, the tests
   on the build machine): nothing to load */
char tls_ovl_path[200];
int tls_ovl_load(char *err, int errlen) { (void)err; (void)errlen; return 1; }
int tls_ovl_loaded(void) { return 1; }
const char *tls_ovl_version(void) { return ""; }
void tls_ovl_unload(void) { }
#endif

/* the elliptic curves: BearSSL's own, or ecdsp.c's on the DSP */
static const br_ec_impl *ec_impl = &br_ec_all_m31;
static br_ecdsa_vrfy ec_vrfy;

void tls_set_ec(const br_ec_impl *impl, br_ecdsa_vrfy vrfy)
{
	ec_impl = impl ? impl : &br_ec_all_m31;
	ec_vrfy = vrfy;
}

int tls_ec_fast(void)
{
	return ec_vrfy != 0;
}

/* the handshake's sums, for the log: the curves' work (the key
   exchange), the server's signature (RSA or ECDSA) and RSA otherwise */
static unsigned long ms_ec, ms_ecdsa, ms_rsa;
static const br_ec_impl *ec_real;

static const br_ec_impl *ec_for(int curve);
static const unsigned char *ect_generator(int curve, size_t *len) { return ec_for(curve)->generator(curve, len); }
static const unsigned char *ect_order(int curve, size_t *len) { return ec_for(curve)->order(curve, len); }
static size_t ect_xoff(int curve, size_t *len) { return ec_for(curve)->xoff(curve, len); }

static uint32_t ect_mul(unsigned char *G, size_t Glen, const unsigned char *x, size_t xlen, int curve)
{
	unsigned long t0 = pf_ms();
	uint32_t r = ec_for(curve)->mul(G, Glen, x, xlen, curve);
	ms_ec += pf_ms() - t0;
	return r;
}

static size_t ect_mulgen(unsigned char *R, const unsigned char *x, size_t xlen, int curve)
{
	unsigned long t0 = pf_ms();
	size_t r = ec_for(curve)->mulgen(R, x, xlen, curve);
	ms_ec += pf_ms() - t0;
	return r;
}

static uint32_t ect_muladd(unsigned char *A, const unsigned char *B, size_t len, const unsigned char *x,
			   size_t xlen, const unsigned char *y, size_t ylen, int curve)
{
	unsigned long t0 = pf_ms();
	uint32_t r = ec_for(curve)->muladd(A, B, len, x, xlen, y, ylen, curve);
	ms_ec += pf_ms() - t0;
	return r;
}

static br_ec_impl ec_timed = { 0, ect_generator, ect_order, ect_xoff, ect_mul, ect_mulgen, ect_muladd };
static br_ecdsa_vrfy ecdsa_real;

static uint32_t ecdsa_timed(const br_ec_impl *impl, const void *hash, size_t hash_len,
			    const br_ec_public_key *pk, const void *sig, size_t sig_len)
{
	unsigned long t0 = pf_ms(), ec0 = ms_ec;
	uint32_t r = ecdsa_real(impl, hash, hash_len, pk, sig, sig_len);
	ms_ec = ec0;		/* its curve work is the signature's */
	ms_ecdsa += pf_ms() - t0;
	return r;
}

/* the curve's code: on the DSP when it is there (ec_real), else
   BearSSL's, with TLS.OVL's br_i31_montymul in 68030 assembly the prime
   curves are faster on i31 than P-256's own m31 code (EMail's figures on
   a TT: a P-256 product 1.5 s against 2.0 s); X25519 stays m31's */
static const br_ec_impl *ec_for(int curve)
{
#ifdef TLS_030
	if (ec_real == &br_ec_all_m31)
		return curve == BR_EC_curve25519 ? &br_ec_c25519_m31 : &br_ec_prime_i31;
#endif
	(void)curve;
	return ec_real;
}

static const char *curve_name(int c)
{
	return c == BR_EC_secp256r1 ? "P-256" : c == BR_EC_secp384r1 ? "P-384" : c == BR_EC_secp521r1 ? "P-521" :
	       c == BR_EC_curve25519 ? "x25519" : "?";
}

/* the server's certificates, checked after the handshake rather than
   during it (EMail's TT, 25 Oct 2026: without the DSP the check took 17 s
   of a 22 s handshake on a TT, and Gmail closed the connection first).
   During the handshake the chain is only kept, and the first
   certificate's key decoded and given to BearSSL, which checks the
   server's handshake signature with it; once the handshake is over, and
   before any data is sent or read, br_x509_minimal checks the whole chain
   as it would have (the roots, the name, the dates), and its key must be
   the one used. A chain that fails ends the connection then, as it ended
   the handshake before: nothing of the user's has gone out. Also the time
   it takes and the bytes it sees, for the log */
#define XT_MAX 8			/* certificates kept */
#define XT_BYTES 24576			/* their bytes, at most */
typedef struct {
	const br_x509_class *vtable;
	br_x509_minimal_context *in;
	unsigned long t0, busy, bytes;
	int certs;
	const char *name;		/* the server's name */
	unsigned char *der;		/* the chain, its certificates one after another */
	size_t len, cap, at[XT_MAX + 1];
	int err;			/* while it was kept: too long, the leaf undecodable */
	br_x509_decoder_context dc;	/* the first certificate's key */
} XTIME;

struct TLS {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
	XTIME xt;
	int hs;				/* the handshake: its times are logged */
	unsigned long sent, waited, wait0;	/* the hello sent; waiting for data */
	unsigned long asked;		/* a request sent: when */
	int answer;			/* 1 waiting for its first bytes, 2 for them readable */
	int sock;
	char desc[80];
	int offered, resumed;		/* a kept session offered; taken up */
	unsigned char offered_id[32];
	unsigned char iobuf[BR_SSL_BUFSIZE_MONO];
};

/* ---------------- root certificates ---------------- */

static br_x509_trust_anchor *anchors;
static size_t nanchors, maxanchors;

/* ---------------- the root certificates' names, hashed once ----------------
 * To find a certificate's issuer among the root certificates, BearSSL
 * hashes the name of every one of them, for each certificate a server
 * sends: 363 SHA-256 hashes for a chain like Wikipedia's, 3.8 million
 * 68030 instructions. Which hash it uses for names is ours to say, so
 * this one is SHA-256 that remembers a root's name: the name comes in one
 * piece, straight from anchors[], and its hash is worked out the first
 * time only. */
typedef struct {
	const br_hash_class *vtable;
	br_sha256_context sc;
	long ta;		/* the root whose name it is, not hashed yet; or -1 */
	int parts;
} DNHASH;
typedef char dnhash_fits[sizeof(DNHASH) <= sizeof(br_hash_compat_context) ? 1 : -1];

static const br_hash_class dn_vtable;
static unsigned char (*dn_memo)[32];
static unsigned char *dn_known;
static const br_x509_trust_anchor *dn_memo_of;	/* the anchors they are for */
static size_t dn_memo_n;

static void dn_forget(void)
{
	free(dn_memo);
	free(dn_known);
	dn_memo = 0;
	dn_known = 0;
	dn_memo_of = 0;
	dn_memo_n = 0;
}

static long dn_root(const void *data, size_t len)
{
	size_t i;
	if (dn_memo_of != anchors || dn_memo_n != nanchors) {
		dn_forget();
		dn_memo = malloc(nanchors * 32);
		dn_known = calloc(nanchors, 1);
		if (!dn_memo || !dn_known) {
			dn_forget();
			return -1;
		}
		dn_memo_of = anchors;
		dn_memo_n = nanchors;
	}
	for (i = 0; i < nanchors; i++)
		if (anchors[i].dn.data == data && anchors[i].dn.len == len)
			return (long)i;
	return -1;
}

static void dn_init(const br_hash_class **ctx)
{
	DNHASH *d = (DNHASH *)(void *)ctx;
	d->vtable = &dn_vtable;
	br_sha256_init(&d->sc);
	d->ta = -1;
	d->parts = 0;
}

static void dn_update(const br_hash_class **ctx, const void *data, size_t len)
{
	DNHASH *d = (DNHASH *)(void *)ctx;
	long i = d->parts++ ? -1 : dn_root(data, len);
	if (d->ta >= 0) {		/* more than a root's name after all */
		br_sha256_update(&d->sc, anchors[d->ta].dn.data, anchors[d->ta].dn.len);
		d->ta = -1;
	}
	if (i >= 0)
		d->ta = i;
	else
		br_sha256_update(&d->sc, data, len);
}

static void dn_out(const br_hash_class *const *ctx, void *dst)
{
	const DNHASH *d = (const DNHASH *)(const void *)ctx;
	if (d->ta < 0) {
		br_sha256_out(&d->sc, dst);
		return;
	}
	if (!dn_known[d->ta]) {
		br_sha256_context sc;
		br_sha256_init(&sc);
		br_sha256_update(&sc, anchors[d->ta].dn.data, anchors[d->ta].dn.len);
		br_sha256_out(&sc, dn_memo[d->ta]);
		dn_known[d->ta] = 1;
	}
	memcpy(dst, dn_memo[d->ta], 32);
}

/* not used by the X.509 engine; right all the same */
static uint64_t dn_state(const br_hash_class *const *ctx, void *dst)
{
	const DNHASH *d = (const DNHASH *)(const void *)ctx;
	br_sha256_context sc = d->sc;
	if (d->ta >= 0)
		br_sha256_update(&sc, anchors[d->ta].dn.data, anchors[d->ta].dn.len);
	return br_sha256_state(&sc, dst);
}

static void dn_set_state(const br_hash_class **ctx, const void *stb, uint64_t count)
{
	DNHASH *d = (DNHASH *)(void *)ctx;
	d->ta = -1;
	d->parts = 1;
	br_sha256_set_state(&d->sc, stb, count);
}

static const br_hash_class dn_vtable = {
	sizeof(DNHASH), BR_HASHDESC_ID(br_sha256_ID) | BR_HASHDESC_OUT(32) | BR_HASHDESC_STATE(32) |
	BR_HASHDESC_LBLEN(6) | BR_HASHDESC_MD_PADDING | BR_HASHDESC_MD_PADDING_BE,
	dn_init, dn_update, dn_out, dn_state, dn_set_state
};

int tls_anchor_count(void)
{
	return (int)nanchors;
}

void tls_free_anchors(void)
{
	size_t i;
	for (i = 0; i < nanchors; i++) {
		br_x509_trust_anchor *ta = &anchors[i];
		free(ta->dn.data);
		if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
			free(ta->pkey.key.rsa.n);
			free(ta->pkey.key.rsa.e);
		} else {
			free(ta->pkey.key.ec.q);
		}
	}
	free(anchors);
	anchors = 0;
	nanchors = maxanchors = 0;
	dn_forget();
}

typedef struct {
	unsigned char *p;
	size_t len, max;
} GROW;

static void grow_add(void *ctx, const void *data, size_t len)
{
	GROW *g = ctx;
	if (!g->p)
		return;
	if (g->len + len > g->max) {
		size_t nmax = (g->len + len) * 2;
		unsigned char *np = realloc(g->p, nmax);
		if (!np) {
			free(g->p);
			g->p = 0;
			return;
		}
		g->p = np;
		g->max = nmax;
	}
	memcpy(g->p + g->len, data, len);
	g->len += len;
}

static unsigned char *dup_bytes(const unsigned char *p, size_t n)
{
	unsigned char *d = malloc(n ? n : 1);
	if (d)
		memcpy(d, p, n);
	return d;
}

/* one certificate (DER) -> a trust anchor */
static void add_anchor(const unsigned char *der, size_t len)
{
	br_x509_decoder_context dc;
	br_x509_pkey *pk;
	br_x509_trust_anchor *ta;
	GROW dn;
	dn.max = 256;
	dn.len = 0;
	dn.p = malloc(dn.max);
	if (!dn.p)
		return;
	br_x509_decoder_init(&dc, grow_add, &dn);
	br_x509_decoder_push(&dc, der, len);
	pk = br_x509_decoder_get_pkey(&dc);
	if (!pk || !dn.p) {
		free(dn.p);
		return;
	}
	if (nanchors == maxanchors) {
		size_t nmax = maxanchors ? maxanchors * 2 : 64;
		br_x509_trust_anchor *na = realloc(anchors, nmax * sizeof(*na));
		if (!na) {
			free(dn.p);
			return;
		}
		anchors = na;
		maxanchors = nmax;
	}
	ta = &anchors[nanchors];
	memset(ta, 0, sizeof(*ta));
	ta->dn.data = dn.p;
	ta->dn.len = dn.len;
	ta->flags = BR_X509_TA_CA;	/* everything in CACERT.PEM is trusted to sign */
	switch (pk->key_type) {
	case BR_KEYTYPE_RSA:
		ta->pkey.key_type = BR_KEYTYPE_RSA;
		ta->pkey.key.rsa.n = dup_bytes(pk->key.rsa.n, pk->key.rsa.nlen);
		ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
		ta->pkey.key.rsa.e = dup_bytes(pk->key.rsa.e, pk->key.rsa.elen);
		ta->pkey.key.rsa.elen = pk->key.rsa.elen;
		if (!ta->pkey.key.rsa.n || !ta->pkey.key.rsa.e)
			return;
		break;
	case BR_KEYTYPE_EC:
		ta->pkey.key_type = BR_KEYTYPE_EC;
		ta->pkey.key.ec.curve = pk->key.ec.curve;
		ta->pkey.key.ec.q = dup_bytes(pk->key.ec.q, pk->key.ec.qlen);
		ta->pkey.key.ec.qlen = pk->key.ec.qlen;
		if (!ta->pkey.key.ec.q)
			return;
		break;
	default:
		free(dn.p);
		return;
	}
	nanchors++;
}

/* ---- the decoded list, cached: decoding 120 certificates takes a
 * 68030 a minute, reading them back a moment. The cache remembers the
 * PEM file's size and checksum and is made again when they change. */
/* 1: size and checksum of CACERT.PEM; 2: and its date, once a check of
   the checksum on this machine said the file is the same */
#define CACHE_MAGIC "MAILROOTS1"
#define CACHE_MAGIC2 "MAILROOTS2"

/* CACERT.PEM is read in pieces this big when the memory is there (the
   guidelines, 6.5: 186 KB in 6 reads, not 47), else in 4 KB ones */
#define BIG_PIECE (32L * 1024)

static char *piece_buf(char *small, long *n)
{
	char *p = malloc(BIG_PIECE);
	*n = p ? BIG_PIECE : 4096;
	return p ? p : small;
}

static unsigned long file_sum(const char *path, long *size)
{
	char small[4096], *chunk;
	unsigned long h = 2166136261UL;		/* FNV-1a */
	long got, n;
	int f = pf_open(path, PF_READ);
	*size = -1;
	if (f < 0)
		return 0;
	*size = 0;
	chunk = piece_buf(small, &n);
	while ((got = pf_read(f, chunk, n)) > 0) {
		long i;
		for (i = 0; i < got; i++) {
			/* times the FNV prime 2^24 + 403 in shifts: the 68000 has
			   no 32-bit multiply */
			h ^= (unsigned char)chunk[i];
			h += (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
		}
		*size += got;
	}
	pf_close(f);
	if (chunk != small)
		free(chunk);
	return h & 0xffffffffUL;
}

/* written straight to the file in 4 KB pieces: little memory needed */
typedef struct {
	int h, ok;
	unsigned char buf[4096];
	int n;
} OUTF;

static void out_bytes(OUTF *o, const void *data, size_t len)
{
	const unsigned char *p = data;
	while (len && o->ok) {
		size_t k = sizeof(o->buf) - (size_t)o->n;
		if (k > len)
			k = len;
		memcpy(o->buf + o->n, p, k);
		o->n += (int)k;
		p += k;
		len -= k;
		if (o->n == (int)sizeof(o->buf)) {
			o->ok = pf_write(o->h, o->buf, o->n) == o->n;
			o->n = 0;
		}
	}
}

static void put32(OUTF *o, unsigned long v)
{
	unsigned char b[4];
	b[0] = (unsigned char)(v >> 24);
	b[1] = (unsigned char)(v >> 16);
	b[2] = (unsigned char)(v >> 8);
	b[3] = (unsigned char)v;
	out_bytes(o, b, 4);
}

static void put_blob(OUTF *o, const unsigned char *p, size_t n)
{
	put32(o, (unsigned long)n);
	out_bytes(o, p, n);
}

static void save_cache(long size, unsigned long sum, unsigned long stamp)
{
	static OUTF o;
	size_t i;
	if (!tls_roots_cache[0])
		return;
	o.h = pf_open(tls_roots_cache, PF_WRITE);
	if (o.h < 0)
		return;
	o.ok = 1;
	o.n = 0;
	out_bytes(&o, CACHE_MAGIC2, 10);
	put32(&o, (unsigned long)size);
	put32(&o, sum);
	put32(&o, stamp);
	put32(&o, (unsigned long)nanchors);
	for (i = 0; i < nanchors; i++) {
		br_x509_trust_anchor *ta = &anchors[i];
		put_blob(&o, ta->dn.data, ta->dn.len);
		put32(&o, ta->pkey.key_type);
		if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
			put_blob(&o, ta->pkey.key.rsa.n, ta->pkey.key.rsa.nlen);
			put_blob(&o, ta->pkey.key.rsa.e, ta->pkey.key.rsa.elen);
		} else {
			put32(&o, (unsigned long)ta->pkey.key.ec.curve);
			put_blob(&o, ta->pkey.key.ec.q, ta->pkey.key.ec.qlen);
		}
	}
	if (o.ok && o.n)
		o.ok = pf_write(o.h, o.buf, o.n) == o.n;
	pf_close(o.h);
	if (!o.ok)
		pf_remove(tls_roots_cache);	/* half a cache is worse than none */
}

static unsigned long get32(const unsigned char **p, const unsigned char *end, int *bad)
{
	unsigned long v;
	if (end - *p < 4) {
		*bad = 1;
		return 0;
	}
	v = ((unsigned long)(*p)[0] << 24) | ((unsigned long)(*p)[1] << 16) | ((unsigned long)(*p)[2] << 8) | (*p)[3];
	*p += 4;
	return v;
}

static unsigned char *get_blob(const unsigned char **p, const unsigned char *end, size_t *n, int *bad)
{
	unsigned long len = get32(p, end, bad);
	unsigned char *d;
	if (*bad || (unsigned long)(end - *p) < len) {
		*bad = 1;
		return 0;
	}
	d = dup_bytes(*p, len);
	*p += len;
	*n = len;
	if (!d)
		*bad = 1;
	return d;
}

/* the cache's anchors, from ROOTS.DAT's bytes (read once, for both
   checks), if it was made from a CACERT.PEM of this size and date
   (stamp, not 0: the checksum isn't looked at) or of this size and
   checksum (stamp 0). Returns its version, or 0 */
static int load_cache(const unsigned char *buf, long len, long size, unsigned long sum,
		      unsigned long stamp)
{
	const unsigned char *p, *end;
	unsigned long count, i, csize, csum, cstamp = 0;
	int bad = 0, ver;
	if (!buf)
		return 0;
	p = buf;
	end = buf + len;
	ver = len < 22 ? 0 : !memcmp(p, CACHE_MAGIC, 10) ? 1 : !memcmp(p, CACHE_MAGIC2, 10) ? 2 : 0;
	if (!ver)
		return 0;
	p += 10;
	csize = get32(&p, end, &bad);
	csum = get32(&p, end, &bad);
	if (ver == 2)
		cstamp = get32(&p, end, &bad);
	if (bad || csize != (unsigned long)size || (stamp ? cstamp != stamp : csum != sum))
		return 0;
	count = get32(&p, end, &bad);
	anchors = calloc(count ? count : 1, sizeof(*anchors));
	if (!anchors)
		return 0;
	maxanchors = count;
	for (i = 0; i < count && !bad; i++) {
		br_x509_trust_anchor *ta = &anchors[i];
		size_t n;
		ta->flags = BR_X509_TA_CA;
		ta->dn.data = get_blob(&p, end, &ta->dn.len, &bad);
		ta->pkey.key_type = (unsigned char)get32(&p, end, &bad);
		if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
			ta->pkey.key.rsa.n = get_blob(&p, end, &n, &bad);
			ta->pkey.key.rsa.nlen = n;
			ta->pkey.key.rsa.e = get_blob(&p, end, &n, &bad);
			ta->pkey.key.rsa.elen = n;
		} else {
			ta->pkey.key.ec.curve = (int)get32(&p, end, &bad);
			ta->pkey.key.ec.q = get_blob(&p, end, &n, &bad);
			ta->pkey.key.ec.qlen = n;
		}
		if (!bad)
			nanchors++;
	}
	return !bad && nanchors == count ? ver : 0;
}

/* this machine checked the cache against CACERT.PEM's checksum: put the
   file's date in it, so the next time a look at the directory will do */
static void cache_stamp(int ver, long size, unsigned long sum, unsigned long stamp)
{
	unsigned char b[4];
	int h;
	if (ver == 1) {
		save_cache(size, sum, stamp);	/* the old layout: once */
		return;
	}
#ifdef PF_UPDATE
	b[0] = (unsigned char)(stamp >> 24);
	b[1] = (unsigned char)(stamp >> 16);
	b[2] = (unsigned char)(stamp >> 8);
	b[3] = (unsigned char)stamp;
	if ((h = pf_open(tls_roots_cache, PF_UPDATE)) >= 0) {
		if (pf_seek(h, 18) == 18)
			pf_write(h, b, 4);
		pf_close(h);
	}
#else
	(void)b;
	(void)h;
#endif
}

int tls_load_anchors(const char *path, char *err, int errlen)
{
	br_pem_decoder_context pc;
	GROW der;
	char small[4096], *chunk;
	long got, size, off = 0, cachelen = 0, piece;
	unsigned long sum, stamp = 0;
	unsigned char *cache;
	int h = -1, ver, in_cert = 0;
	if (nanchors)
		return (int)nanchors;
	/* the cache is good for this CACERT.PEM if it says the same size and
	   date; else if the checksum of the whole file (186 KB, a second on
	   a DFB1) is the one it says. ROOTS.DAT is read once for both, and
	   CACERT.PEM looked up once where the app gives tls_file_info */
	if (tls_file_info) {
		stamp = tls_file_info(path, &size);
	} else {
		stamp = tls_file_stamp ? tls_file_stamp(path) : 0;
		size = pf_size(path);
	}
	cache = size >= 0 && tls_roots_cache[0] ? (unsigned char *)pf_load(tls_roots_cache, &cachelen) : 0;
	if (stamp && size >= 0 && load_cache(cache, cachelen, size, 0, stamp)) {
		free(cache);
		return (int)nanchors;
	}
	tls_free_anchors();
	sum = file_sum(path, &size);
	if (size < 0) {
		free(cache);
		snprintf(err, errlen, "no root certificates: %s is missing", path);
		return 0;
	}
	ver = load_cache(cache, cachelen, size, sum, 0);
	free(cache);
	if (ver) {
		if (stamp)
			cache_stamp(ver, size, sum, stamp);
		return (int)nanchors;
	}
	tls_free_anchors();
	if (tls_note)
		tls_note("Reading the root certificates (only this once, it takes a while)...");
	/* read in pieces: the whole list is about 190 KB, more than a
	   1 MB Falcon wants to hold at once next to the program. 32 KB
	   pieces where the memory is there (6.5), decoded 4 KB at a time
	   with pf_idle between (2.2's 50 ms) */
	if (!tls_read_at && (h = pf_open(path, PF_READ)) < 0) {
		snprintf(err, errlen, "no root certificates: can't open %s", path);
		return 0;
	}
	der.max = 4096;
	der.len = 0;
	der.p = malloc(der.max);
	chunk = piece_buf(small, &piece);
	br_pem_decoder_init(&pc);
	while (der.p && (got = tls_read_at ? tls_read_at(path, off, chunk, piece)
				       : pf_read(h, chunk, piece)) > 0) {
		long pos = 0, slice = 0;
		off += got;
		while (pos < got) {
			long n = got - pos;
			if (pos - slice >= 4096) {
				slice = pos;
				if (pf_idle)
					pf_idle();
			}
			if (n > slice + 4096 - pos)
				n = slice + 4096 - pos;
			pos += (long)br_pem_decoder_push(&pc, chunk + pos, (size_t)n);
			switch (br_pem_decoder_event(&pc)) {
			case BR_PEM_BEGIN_OBJ:
				in_cert = !strcmp(br_pem_decoder_name(&pc), "CERTIFICATE");
				der.len = 0;
				br_pem_decoder_setdest(&pc, in_cert ? grow_add : 0, &der);
				break;
			case BR_PEM_END_OBJ:
				if (in_cert && der.p)
					add_anchor(der.p, der.len);
				in_cert = 0;
				break;
			case BR_PEM_ERROR:
				/* not PEM here: start over at the next BEGIN */
				br_pem_decoder_init(&pc);
				in_cert = 0;
				break;
			}
		}
		if (pf_idle)
			pf_idle();
	}
	if (h >= 0)
		pf_close(h);
	if (chunk != small)
		free(chunk);
	if (!der.p)
		snprintf(err, errlen, "not enough memory to read %s", path);
	free(der.p);
	if (!nanchors && der.p)
		snprintf(err, errlen, "no root certificates found in %s", path);
	if (nanchors)
		save_cache(size, sum, stamp);
	return (int)nanchors;
}

/* ---------------- RSA through the accelerator ---------------- */

/* the DSP's (tls_rsa_accel), else TLS.OVL's 68030 code (rsa030.c: seven
   to ten times BearSSL's C there, EMail's, 25 Oct 2026), else BearSSL's.
   rsa_by: which, for the log */
static const char *rsa_by = "";

static uint32_t rsa_public(unsigned char *x, size_t xlen, const br_rsa_public_key *pk)
{
	unsigned long t0 = pf_ms();
	uint32_t r = tls_rsa_accel && tls_rsa_accel(x, xlen, pk->n, pk->nlen, pk->e, pk->elen);
	rsa_by = " on the DSP";
#ifdef TLS_030
	if (!r) {
		r = rsa030_public(x, xlen, pk->n, pk->nlen, pk->e, pk->elen);
		rsa_by = " on the 68030";
	}
#endif
	if (!r) {
		r = br_rsa_i31_public(x, xlen, pk);
		rsa_by = "";
	}
	ms_rsa += pf_ms() - t0;
	return r;
}

static uint32_t rsa_vrfy(const unsigned char *x, size_t xlen, const unsigned char *hash_oid,
			 size_t hash_len, const br_rsa_public_key *pk, unsigned char *hash_out)
{
	unsigned char sig[BR_MAX_RSA_SIZE >> 3];
	if (xlen > sizeof(sig))
		return 0;
	memcpy(sig, x, xlen);
	if (!rsa_public(sig, xlen, pk))
		return 0;
	return br_rsa_pkcs1_sig_unpad(sig, xlen, hash_oid, hash_len, hash_out);
}

/* ---------------- time ---------------- */

/* days from 1 January of year 0 (proleptic Gregorian), as BearSSL counts */
static uint32_t days_from_civil(int y, int m, int d)
{
	uint32_t era, yoe, doy, doe;
	y -= m <= 2;
	era = (uint32_t)y / 400;
	yoe = (uint32_t)y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe + 60;	/* 1 March year 0 is day 60 */
}

static void set_time(br_x509_minimal_context *xc)
{
	PFTIME t;
	long secs;
	uint32_t days;
	pf_now(&t);
	days = days_from_civil(t.year, t.mon, t.day);
	secs = t.hour * 3600L + t.min * 60L + t.sec - tls_tz_minutes * 60L;
	while (secs < 0) {
		secs += 86400L;
		days--;
	}
	while (secs >= 86400L) {
		secs -= 86400L;
		days++;
	}
	br_x509_minimal_set_time(xc, days, (uint32_t)secs);
}

/* a small file of ours written again: over the old one, in place, where
   the app's plat.h has PF_UPDATE. On TOS a new file (or a renamed one)
   means searching the FAT and changing directory entries, half a second
   or more each on a big partition; the same number of bytes written over
   an old file changes neither. pf_save, safe and slow, when there is
   none yet */
static void put_file(const char *path, const void *data, long len)
{
#ifdef PF_UPDATE
	int h;
	if (pf_size(path) == len && (h = pf_open(path, PF_UPDATE)) >= 0) {
		long w = pf_write(h, data, len);
		pf_close(h);
		if (w == len)
			return;
	}
#endif
	pf_save(path, data, len);
}

/* ---------------- randomness ----------------
 * Fresh timer jitter (pf_entropy) hashed together with a seed kept on
 * disk, so what each session gathers adds up. The seed on disk is
 * replaced once a session, the first time, with a value that can't be
 * traced back to what was used (writing it took 2.9 s of a handshake on
 * a DFB1); later handshakes go on from one kept in memory. */
static unsigned char seed_kept[32];
static int seed_have;

static void get_seed(unsigned char out[48])
{
	static unsigned long count;
	unsigned char raw[128], old[32], h[32];
	br_sha256_context sc;
	int on_disk = 0;
	memset(old, 0, sizeof(old));
	if (seed_have) {
		memcpy(old, seed_kept, sizeof(old));
	} else if (tls_seed_path[0]) {
		/* opened, not looked up first (6.5); PF_UPDATE is write-only in
		   the apps' plat, so the reading is an open of its own */
		int rh = pf_open(tls_seed_path, PF_READ);
		if (rh >= 0) {
			on_disk = pf_read(rh, old, sizeof(old)) == (long)sizeof(old);
			pf_close(rh);
			if (!on_disk)
				memset(old, 0, sizeof(old));
		}
	}
	pf_entropy(raw, sizeof(raw));
	count++;
	br_sha256_init(&sc);
	br_sha256_update(&sc, "use", 3);
	br_sha256_update(&sc, old, sizeof(old));
	br_sha256_update(&sc, raw, sizeof(raw));
	br_sha256_update(&sc, &count, sizeof(count));
	br_sha256_out(&sc, out);
	memcpy(out + 32, raw, 16);
	br_sha256_init(&sc);
	br_sha256_update(&sc, "next", 4);
	br_sha256_update(&sc, old, sizeof(old));
	br_sha256_update(&sc, raw, sizeof(raw));
	br_sha256_update(&sc, &count, sizeof(count));
	br_sha256_out(&sc, h);
	if (!seed_have && tls_seed_path[0]) {
		/* written over in place when it was there (its 32 bytes were just
		   read), else made with pf_save: no look at the directory */
		int done = 0;
#ifdef PF_UPDATE
		int wh = on_disk ? pf_open(tls_seed_path, PF_UPDATE) : -1;
		if (wh >= 0) {
			done = pf_write(wh, h, sizeof(h)) == (long)sizeof(h);
			pf_close(wh);
		}
#endif
		if (!done)
			pf_save(tls_seed_path, h, sizeof(h));
	}
	/* the next handshake's, not the next session's */
	br_sha256_init(&sc);
	br_sha256_update(&sc, "kept", 4);
	br_sha256_update(&sc, h, sizeof(h));
	br_sha256_out(&sc, seed_kept);
	seed_have = 1;
	memset(raw, 0, sizeof(raw));
	memset(old, 0, sizeof(old));
	memset(h, 0, sizeof(h));
}

/* ---------------- signatures already checked ----------------
 * A certificate's signature is the same bytes every time a server sends
 * it, and once the arithmetic said yes it would say yes again: so an
 * authority's signature on its intermediate certificate, or on a site's
 * until the site renews it, is checked once (0.94 s for P-384 on the DSP,
 * 40 s and more on the 68030). What is remembered is a SHA-256 of all the
 * check depends on (the curve, the key, the hash of the signed data, the
 * signature), and only after a check that succeeded; BearSSL still
 * decides everything else (names, dates, uses, the chain up to a root
 * certificate). The server's signature on its key exchange is new every
 * time, and isn't looked up. RSA with SHA-256 too (an RSA-4096 check is
 * 8 s on a TT in C, EUpdate's 25 Oct 2026): BearSSL compares the hash the
 * signature gives with the signed data's, so an RSA entry is two slots,
 * that hash and a SHA-256 of the check's inputs and the hash; one half
 * written over by the ring leaves a pair that doesn't match. Kept in
 * tls_sigs_path (SIGS.DAT) between sessions, written over in place. */
#define NSIGS 64
#define SIGS_LEN (8 + NSIGS * 32)
static unsigned char sig_file[SIGS_LEN];	/* "TLSG", next, 3 spare, the keys */
static int sigs_read, sigs_changed, sigs_known, sigs_checked, rsa_known, rsa_checked;
static br_ecdsa_vrfy sig_vrfy;			/* the check itself */

static void sigs_load(void)
{
	long len = 0;
	char *f;
	if (sigs_read)
		return;
	sigs_read = 1;
	memcpy(sig_file, "TLSG", 4);
	f = tls_sigs_path[0] ? pf_load(tls_sigs_path, &len) : 0;
	if (f && len == SIGS_LEN && !memcmp(f, "TLSG", 4) && (unsigned char)f[4] < NSIGS)
		memcpy(sig_file, f, SIGS_LEN);
	free(f);
}

static void sigs_save(void)
{
	if (sigs_changed && tls_sigs_path[0])
		put_file(tls_sigs_path, sig_file, SIGS_LEN);
	sigs_changed = 0;
}

/* a certificate's RSA signature (x509_minimal's): looked up, or checked
   and kept */
static uint32_t rsa_check(const unsigned char *x, size_t xlen, const unsigned char *hash_oid,
			  size_t hash_len, const br_rsa_public_key *pk, unsigned char *hash_out)
{
	br_sha256_context sc, c;
	unsigned char key[32], n[4], *slot;
	int i;
	uint32_t r;
	if (hash_len != 32)
		return rsa_vrfy(x, xlen, hash_oid, hash_len, pk, hash_out);
	sigs_load();
	rsa_checked++;
	br_sha256_init(&sc);
	br_sha256_update(&sc, "RSA", 3);
	n[0] = (unsigned char)(pk->nlen >> 8);
	n[1] = (unsigned char)pk->nlen;
	n[2] = (unsigned char)pk->elen;
	br_sha256_update(&sc, n, 3);
	br_sha256_update(&sc, pk->n, pk->nlen);
	br_sha256_update(&sc, pk->e, pk->elen);
	n[0] = hash_oid ? hash_oid[0] : 0;
	br_sha256_update(&sc, n, 1);
	if (hash_oid)
		br_sha256_update(&sc, hash_oid + 1, hash_oid[0]);
	n[0] = (unsigned char)(xlen >> 8);
	n[1] = (unsigned char)xlen;
	br_sha256_update(&sc, n, 2);
	br_sha256_update(&sc, x, xlen);
	for (i = 0; i < NSIGS; i++) {
		slot = sig_file + 8 + ((i + 1) % NSIGS) * 32;	/* the hash, after its key */
		c = sc;
		br_sha256_update(&c, slot, 32);
		br_sha256_out(&c, key);
		if (!memcmp(sig_file + 8 + i * 32, key, 32)) {
			memcpy(hash_out, slot, 32);
			rsa_known++;
			return 1;
		}
	}
	r = rsa_vrfy(x, xlen, hash_oid, hash_len, pk, hash_out);
	if (r) {
		i = sig_file[4];
		slot = sig_file + 8 + ((i + 1) % NSIGS) * 32;
		memcpy(slot, hash_out, 32);
		br_sha256_update(&sc, slot, 32);
		br_sha256_out(&sc, sig_file + 8 + i * 32);
		sig_file[4] = (unsigned char)((i + 2) % NSIGS);
		sigs_changed = 1;
	}
	return r;
}

static uint32_t sig_check(const br_ec_impl *impl, const void *hash, size_t hash_len,
			  const br_ec_public_key *pk, const void *sig, size_t sig_len)
{
	br_sha256_context sc;
	unsigned char key[32], n[4];
	int i;
	uint32_t r;
	sigs_load();
	sigs_checked++;
	br_sha256_init(&sc);
	br_sha256_update(&sc, "ECDSA", 5);
	n[0] = (unsigned char)pk->curve;
	n[1] = (unsigned char)(pk->qlen >> 8);
	n[2] = (unsigned char)pk->qlen;
	br_sha256_update(&sc, n, 3);
	br_sha256_update(&sc, pk->q, pk->qlen);
	n[0] = (unsigned char)hash_len;
	br_sha256_update(&sc, n, 1);
	br_sha256_update(&sc, hash, hash_len);
	n[0] = (unsigned char)(sig_len >> 8);
	n[1] = (unsigned char)sig_len;
	br_sha256_update(&sc, n, 2);
	br_sha256_update(&sc, sig, sig_len);
	br_sha256_out(&sc, key);
	for (i = 0; i < NSIGS; i++)
		if (!memcmp(sig_file + 8 + i * 32, key, 32)) {
			sigs_known++;
			return 1;
		}
	r = sig_vrfy(impl, hash, hash_len, pk, sig, sig_len);
	if (r) {
		i = sig_file[4];
		memcpy(sig_file + 8 + i * 32, key, 32);
		sig_file[4] = (unsigned char)((i + 1) % NSIGS);
		sigs_changed = 1;
	}
	return r;
}

/* ---------------- the connection ---------------- */

/* the last sessions, by server: a new connection to the same server
   resumes one (TLS session ID) and skips the key exchange and the
   certificate checks, seconds of arithmetic on a 68030 */
#define NSESS 4
static struct {
	char host[64];
	br_ssl_session_parameters sp;
} sess[NSESS];
static int sess_next;

static br_ssl_session_parameters *sess_of(const char *host)
{
	int i;
	for (i = 0; i < NSESS; i++)
		if (sess[i].host[0] && !strcmp(sess[i].host, host))
			return &sess[i].sp;
	return 0;
}

static void sess_keep(const char *host, const br_ssl_session_parameters *sp)
{
	br_ssl_session_parameters *old = sess_of(host);
	if (!sp->session_id_len)
		return;			/* the server doesn't offer resumption */
	if (!old) {
		str_copy(sess[sess_next].host, host, sizeof(sess[0].host));
		old = &sess[sess_next].sp;
		sess_next = (sess_next + 1) % NSESS;
	}
	*old = *sp;
}

void tls_forget_sessions(void)
{
	memset(sess, 0, sizeof(sess));
}

static const uint16_t suites_rsa[] = {
	BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
	BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA,
};

/* ChaCha20 alone, RSA certificates first: the first try with every
   server (EMail's measurements: twice as fast as AES-GCM on a 68030) */
static const uint16_t suites_chacha[] = {
	BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
};

static const uint16_t suites_all[] = {
	BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
	BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
	BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
	BR_TLS_RSA_WITH_AES_128_CBC_SHA,
};

/* what an error means, in words a user can act on */
static void explain(int e, const char *host, char *err, int errlen)
{
	const char *m;
	switch (e) {
	case BR_ERR_X509_NOT_TRUSTED:
		m = "its certificate isn't signed by any authority in CACERT.PEM";
		break;
	case BR_ERR_X509_EXPIRED:
		m = "its certificate is out of date (or the Atari's clock is wrong)";
		break;
	case BR_ERR_X509_BAD_SERVER_NAME:
		m = "its certificate is for a different server name";
		break;
	case BR_ERR_X509_UNSUPPORTED:
	case BR_ERR_X509_WEAK_PUBLIC_KEY:
		m = "its certificate uses a kind of key this program can't check";
		break;
	case BR_ERR_BAD_VERSION:
	case BR_ERR_UNSUPPORTED_VERSION:
		m = "it doesn't speak TLS 1.2";
		break;
	case BR_ERR_BAD_SIGNATURE:
		m = "its signature didn't check out";
		break;
	case BR_ERR_IO:
		m = "the connection was lost during the TLS handshake";
		break;
	default:
		if (e >= BR_ERR_RECV_FATAL_ALERT && e < BR_ERR_RECV_FATAL_ALERT + 256) {
			snprintf(err, errlen, "%s refused the secure connection (TLS alert %d)", host,
				 e - BR_ERR_RECV_FATAL_ALERT);
			return;
		}
		snprintf(err, errlen, "secure connection to %s failed (TLS error %d)", host, e);
		return;
	}
	snprintf(err, errlen, "%s: %s", host, m);
}

static void step(const char *fmt, ...)
{
	char msg[160];
	va_list ap;
	if (!tls_log)
		return;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	tls_log(msg);
}

#define XT(ctx) ((XTIME *)(void *)(ctx))
#define XIN(ctx) (&XT(ctx)->in->vtable)
#define XBEGIN(ctx) unsigned long xt_t = pf_ms()
#define XEND(ctx) (XT(ctx)->busy += pf_ms() - xt_t)

static void xt_start_chain(const br_x509_class **ctx, const char *server_name)
{
	XTIME *x = XT(ctx);
	x->t0 = pf_ms();
	x->name = server_name;
	x->len = 0;
	x->err = 0;
	x->certs = 0;
	x->bytes = 0;
	x->busy = 0;
}

static void xt_start_cert(const br_x509_class **ctx, uint32_t length)
{
	XTIME *x = XT(ctx);
	XBEGIN(ctx);
	if (x->certs < XT_MAX && x->len + length <= XT_BYTES) {
		if (x->len + length > x->cap) {
			size_t want = x->len + length + 1024;
			unsigned char *n = realloc(x->der, want);
			if (n) {
				x->der = n;
				x->cap = want;
			}
		}
		if (x->len + length <= x->cap)
			x->at[x->certs] = x->len;
		else
			x->err = BR_ERR_X509_LIMIT_EXCEEDED;
	} else if (!x->err) {
		x->err = BR_ERR_X509_LIMIT_EXCEEDED;
	}
	if (x->certs == 0)
		br_x509_decoder_init(&x->dc, 0, 0);
	x->certs++;
	x->bytes += length;
	XEND(ctx);
}

static void xt_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
	XTIME *x = XT(ctx);
	XBEGIN(ctx);
	if (!x->err && x->len + len <= x->cap) {
		memcpy(x->der + x->len, buf, len);
		x->len += len;
	}
	if (x->certs == 1)
		br_x509_decoder_push(&x->dc, buf, len);
	XEND(ctx);
}

static void xt_end_cert(const br_x509_class **ctx)
{
	XTIME *x = XT(ctx);
	if (!x->err && x->certs <= XT_MAX)
		x->at[x->certs] = x->len;
}

static unsigned xt_end_chain(const br_x509_class **ctx)
{
	XTIME *x = XT(ctx);
	if (!x->certs)
		return BR_ERR_X509_EMPTY_CHAIN;
	if (!x->err)
		x->err = br_x509_decoder_last_error(&x->dc);
	step("the server's %d certificate%s, %lu bytes: kept, to be checked after the handshake",
	     x->certs, x->certs == 1 ? "" : "s", x->bytes);
	return (unsigned)x->err;
}

static const br_x509_pkey *xt_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
	XTIME *x = (XTIME *)(void *)ctx;
	if (usages)
		*usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;	/* checked after (xt_check) */
	return br_x509_decoder_get_pkey(&x->dc);
}

/* the same keys: the one the handshake used and the one the check gives */
static int same_key(const br_x509_pkey *a, const br_x509_pkey *b)
{
	if (!a || !b || a->key_type != b->key_type)
		return 0;
	if (a->key_type == BR_KEYTYPE_RSA)
		return a->key.rsa.nlen == b->key.rsa.nlen && a->key.rsa.elen == b->key.rsa.elen &&
		       !memcmp(a->key.rsa.n, b->key.rsa.n, a->key.rsa.nlen) &&
		       !memcmp(a->key.rsa.e, b->key.rsa.e, a->key.rsa.elen);
	return a->key.ec.curve == b->key.ec.curve && a->key.ec.qlen == b->key.ec.qlen &&
	       !memcmp(a->key.ec.q, b->key.ec.q, a->key.ec.qlen);
}

/* after the handshake, before any data: the chain kept, through
   br_x509_minimal as the handshake would have given it; need is the use
   of the key the cipher suite took (BR_KEYTYPE_SIGN or _KEYX). 0, or
   BearSSL's error */
static int xt_check(XTIME *x, unsigned need)
{
	const br_x509_class **in = &x->in->vtable;
	const br_x509_pkey *pk;
	unsigned usages = 0, r;
	int i;
	unsigned long t0 = pf_ms();
	if (x->err)
		return x->err;
	(*in)->start_chain(in, x->name);
	for (i = 0; i < x->certs; i++) {
		size_t n = x->at[i + 1] - x->at[i];
		(*in)->start_cert(in, (uint32_t)n);
		(*in)->append(in, x->der + x->at[i], n);
		(*in)->end_cert(in);
	}
	r = (*in)->end_chain(in);
	pk = r ? 0 : (*in)->get_pkey(in, &usages);
	if (!r && !same_key(pk, br_x509_decoder_get_pkey(&x->dc)))
		r = BR_ERR_X509_NOT_TRUSTED;	/* can't be: the same bytes */
	if (!r && (usages & need) != need)
		r = BR_ERR_X509_FORBIDDEN_KEY_USAGE;
	{
		char known[100];
		int k = 0;
		known[0] = 0;
		if (sigs_checked)
			k = snprintf(known, sizeof(known), "; %d of %d ECDSA signatures checked before", sigs_known,
				     sigs_checked);
		if (rsa_checked)
			snprintf(known + k, sizeof(known) - k, "; %d of %d RSA signatures checked before", rsa_known,
				 rsa_checked);
		step("the server's %d certificate%s checked after the handshake: %lu ms%s%s",
		     x->certs, x->certs == 1 ? "" : "s", pf_ms() - t0, known, r ? " (refused)" : "");
	}
	return (int)r;
}

static void xt_free(XTIME *x)
{
	free(x->der);
	x->der = 0;
	x->cap = x->len = 0;
}

static const br_x509_class xt_vtable = {
	sizeof(XTIME), xt_start_chain, xt_start_cert, xt_append, xt_end_cert, xt_end_chain, xt_get_pkey
};

/* move records between BearSSL and the socket until `want` (a state
   bit) is possible. blocking: wait for it (with a timeout); otherwise
   return after one look at the socket. 1 ok, 0 not yet, -1 closed */
static int pump(TLS *t, unsigned want, int blocking)
{
	br_ssl_engine_context *eng = &t->cc.eng;
	unsigned long start = pf_ms();
	for (;;) {
		unsigned st = br_ssl_engine_current_state(eng);
		if (st & BR_SSL_CLOSED)
			return -1;
		if (st & BR_SSL_SENDREC) {
			size_t len;
			unsigned char *buf = br_ssl_engine_sendrec_buf(eng, &len);
			long w = net_write(t->sock, buf, (long)len);
			if (w <= 0) {
				br_ssl_engine_close(eng);
				return -1;
			}
			br_ssl_engine_sendrec_ack(eng, (size_t)w);
			start = pf_ms();
			t->sent = start;
			continue;
		}
		if (st & want)
			return 1;
		if (st & BR_SSL_RECVREC) {
			size_t len;
			unsigned char *buf = br_ssl_engine_recvrec_buf(eng, &len);
			long r = net_read(t->sock, buf, (long)len);
			if (r < 0) {
				br_ssl_engine_close(eng);
				return -1;
			}
			if (r > 0) {
				br_ssl_engine_recvrec_ack(eng, (size_t)r);
				start = pf_ms();
				if (t->wait0) {
					t->waited += start - t->wait0;
					t->wait0 = 0;
				}
				if (t->hs == 1) {
					step("the server's first answer, %lu ms after the hello", start - t->sent);
					t->hs = 2;
				}
				if (t->answer == 1) {
					step("the answer's first bytes came %lu ms after the request", start - t->asked);
					t->answer = 2;
				}
				continue;
			}
			if (!blocking)
				return 0;
			if (!t->wait0)
				t->wait0 = pf_ms();
			if (pf_ms() - start > 60000UL)
				return -1;
			if (pf_idle)
				pf_idle();
			continue;
		}
		/* neither sending nor receiving possible: wait for the app side */
		return (st & want) ? 1 : 0;
	}
}

TLS *tls_start(int sock, const char *host, int flags, int *retry, char *err, int errlen)
{
	TLS *t;
	unsigned char seed[48];
	unsigned long t0;
	int r, rsa_only = flags & TLS_RSA_ONLY, chacha_only = flags & TLS_CHACHA_ONLY, answered;
	if (retry)
		*retry = 0;
	if (!nanchors) {
		snprintf(err, errlen, "no root certificates loaded");
		return 0;
	}
	t = calloc(1, sizeof(TLS));
	if (!t) {
		str_copy(err, "out of memory for TLS", errlen);
		return 0;
	}
	t->sock = sock;
	br_ssl_client_init_full(&t->cc, &t->xc, anchors, nanchors);
	br_ssl_engine_set_versions(&t->cc.eng, BR_TLS12, BR_TLS12);
	if (chacha_only)
		br_ssl_engine_set_suites(&t->cc.eng, suites_chacha, rsa_only ? 1 : 2);
	else if (rsa_only)
		br_ssl_engine_set_suites(&t->cc.eng, suites_rsa, sizeof(suites_rsa) / sizeof(suites_rsa[0]));
	else
		br_ssl_engine_set_suites(&t->cc.eng, suites_all, sizeof(suites_all) / sizeof(suites_all[0]));
	/* timed (ms_ec, ms_ecdsa) for the handshake's log line */
	ecdsa_real = ec_vrfy ? ec_vrfy : br_ecdsa_vrfy_asn1_get_default();
	br_ssl_engine_set_ecdsa(&t->cc.eng, rsa_only ? 0 : ecdsa_timed);
	/* BearSSL's: the fastest code for a 32-bit CPU without fast 64-bit
	   products */
	ec_real = ec_impl;
	ec_timed.supported_curves = ec_impl->supported_curves;
	br_ssl_engine_set_ec(&t->cc.eng, &ec_timed);
	/* AES with tables: half the instructions of the constant-time code
	   on a 68030 (205 a byte for AES-256-GCM against 394), and nobody
	   times an Atari's cache */
	br_ssl_engine_set_aes_ctr(&t->cc.eng, &br_aes_big_ctr_vtable);
	br_ssl_engine_set_aes_cbc(&t->cc.eng, &br_aes_big_cbcenc_vtable, &br_aes_big_cbcdec_vtable);
	/* AES-GCM's GHASH, chosen rather than left to BearSSL: ctmul is the
	   fastest measured on the Falcon (50 KB/s, ctmul32 37) */
	br_ssl_engine_set_ghash(&t->cc.eng, &br_ghash_ctmul);
	br_ssl_engine_set_rsavrfy(&t->cc.eng, rsa_vrfy);
	br_ssl_client_set_rsapub(&t->cc, rsa_public);
	br_x509_minimal_set_rsa(&t->xc, rsa_check);
	sig_vrfy = ec_vrfy ? ec_vrfy : br_ecdsa_i31_vrfy_asn1;
	br_x509_minimal_set_ecdsa(&t->xc, &ec_timed, sig_check);
	t->xc.dn_hash_impl = &dn_vtable;
	set_time(&t->xc);
	br_ssl_engine_set_buffer(&t->cc.eng, t->iobuf, sizeof(t->iobuf), 0);
	t->xt.vtable = &xt_vtable;
	t->xt.in = &t->xc;
	br_ssl_engine_set_x509(&t->cc.eng, &t->xt.vtable);
	t0 = pf_ms();
	get_seed(seed);
	step("random seed ready in %lu ms", pf_ms() - t0);
	br_ssl_engine_inject_entropy(&t->cc.eng, seed, sizeof(seed));
	memset(seed, 0, sizeof(seed));
	{
		br_ssl_session_parameters *old = sess_of(host);
		if (old)
			br_ssl_engine_set_session_parameters(&t->cc.eng, old);
		t->offered = old != 0;
		if (old)
			memcpy(t->offered_id, old->session_id, sizeof(t->offered_id));
	}
	if (!br_ssl_client_reset(&t->cc, host, t->offered)) {
		explain(br_ssl_engine_last_error(&t->cc.eng), host, err, errlen);
		free(t);
		return 0;
	}
	sigs_known = sigs_checked = rsa_known = rsa_checked = 0;
	t->hs = 1;
	t->sent = t0 = pf_ms();
	ms_ec = ms_ecdsa = ms_rsa = 0;
	r = pump(t, BR_SSL_SENDAPP, 1);
	answered = t->hs == 2;
	t->hs = 0;
	t0 = pf_ms() - t0;
	step("the handshake: %lu ms waiting for the network, %lu ms working", t->waited, t0 - t->waited);
	if (r > 0 && t->xt.certs) {
		/* where the work went: the key exchange and the server's
		   signature over it (EMail: GMX's 9 s on a TT) */
		const br_x509_pkey *k = br_x509_decoder_get_pkey(&t->xt.dc);
		int c = br_ssl_engine_get_ecdhe_curve(&t->cc.eng);
		char kx[24];
		if (c)
			snprintf(kx, sizeof(kx), "%s, %lu ms", curve_name(c), ms_ec);
		else
			snprintf(kx, sizeof(kx), "RSA, %lu ms", ms_rsa);
		if (k && k->key_type == BR_KEYTYPE_RSA) {
			size_t n = k->key.rsa.nlen, bits;
			const unsigned char *p = k->key.rsa.n;
			unsigned top;
			while (n > 0 && !*p)
				p++, n--;
			bits = n * 8;
			for (top = n ? *p : 0x80; !(top & 0x80); top <<= 1)
				bits--;
			step("the key exchange: %s; the server's key: RSA %u bits, its signature %lu ms%s", kx,
			     (unsigned)bits, c ? ms_rsa : 0UL, rsa_by);
		} else if (k)
			step("the key exchange: %s; the server's key: %s, its signature %lu ms", kx,
			     curve_name(k->key.ec.curve), ms_ecdsa);
	}
	if (r > 0 && t->xt.certs) {
		/* the chain kept during the handshake, checked now, before any
		   data goes either way (a resumed session sends none: it was
		   checked when it was made) */
		br_ssl_session_parameters sp;
		unsigned s, need;
		int e;
		br_ssl_engine_get_session_parameters(&t->cc.eng, &sp);
		s = sp.cipher_suite;
		need = (s == BR_TLS_RSA_WITH_AES_128_GCM_SHA256 || s == BR_TLS_RSA_WITH_AES_128_CBC_SHA256 ||
			s == BR_TLS_RSA_WITH_AES_128_CBC_SHA) ? BR_KEYTYPE_KEYX : BR_KEYTYPE_SIGN;
		e = xt_check(&t->xt, need);
		if (e) {
			xt_free(&t->xt);
			br_ssl_engine_close(&t->cc.eng);
			explain(e, host, err, errlen);
			free(t);
			return 0;
		}
	}
	xt_free(&t->xt);
	if (r > 0)
		sigs_save();
	if (r <= 0) {
		int e = br_ssl_engine_last_error(&t->cc.eng);
		if (!e)
			e = BR_ERR_IO;
		/* refused with ChaCha20 only (an alert before any certificate,
		   or the connection closed with no answer at all, from servers
		   that don't bother): try again with every cipher. A server
		   that sent its certificates took ChaCha20: a close after them
		   is something else (Gmail hangs up on a slow 68030 then).
		   40 handshake_failure, 71 insufficient_security with all of
		   them: nothing in common with an RSA-only hello */
		if (chacha_only && retry && !t->xt.certs &&
		    ((e == BR_ERR_IO && !answered) ||
		     (e > BR_ERR_RECV_FATAL_ALERT && e < BR_ERR_RECV_FATAL_ALERT + 256)))
			*retry = TLS_RETRY_ANY_CIPHER;
		else if (rsa_only && retry &&
			 (e == BR_ERR_RECV_FATAL_ALERT + 40 || e == BR_ERR_RECV_FATAL_ALERT + 71))
			*retry = TLS_RETRY_ECDSA;
		explain(e, host, err, errlen);
		xt_free(&t->xt);
		free(t);
		return 0;
	}
	{
		br_ssl_session_parameters sp;
		const char *kx, *ciph;
		unsigned s;
		br_ssl_engine_get_session_parameters(&t->cc.eng, &sp);
		t->resumed = t->offered && !memcmp(sp.session_id, t->offered_id, sizeof(t->offered_id));
		sess_keep(host, &sp);
		s = sp.cipher_suite;
		kx = (s == BR_TLS_RSA_WITH_AES_128_GCM_SHA256 || s == BR_TLS_RSA_WITH_AES_128_CBC_SHA256 ||
		      s == BR_TLS_RSA_WITH_AES_128_CBC_SHA) ? "RSA" :
		     ((s & 0xff00) == 0xcc00 ? (s == BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 ? "ECDHE-RSA" : "ECDHE-ECDSA") :
		      ((s >= 0xc023 && s <= 0xc02c && !(s & 1)) || s == BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA ? "ECDHE-ECDSA" : "ECDHE-RSA"));
		ciph = (s & 0xff00) == 0xcc00 ? "ChaCha20-Poly1305" :
		       (s == BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 || s == BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384) ? "AES-256-GCM" :
		       (s == BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 || s == BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 ||
			s == BR_TLS_RSA_WITH_AES_128_GCM_SHA256) ? "AES-128-GCM" : "AES-128-CBC";
		snprintf(t->desc, sizeof(t->desc), "TLS 1.2, %s, %s%s", kx, ciph, t->resumed ? ", resumed" : "");
	}
	return t;
}

const char *tls_describe(TLS *t)
{
	return t ? t->desc : "";
}

long tls_read(TLS *t, void *buf, long max)
{
	br_ssl_engine_context *eng = &t->cc.eng;
	size_t len;
	unsigned char *p;
	int r = pump(t, BR_SSL_RECVAPP, 0);
	if (r < 0) {
		/* data may still be waiting even after close_notify */
		if (!(br_ssl_engine_current_state(eng) & BR_SSL_RECVAPP))
			return -1;
	} else if (r == 0) {
		return 0;
	}
	p = br_ssl_engine_recvapp_buf(eng, &len);
	if (!p)
		return r < 0 ? -1 : 0;
	if (t->answer) {
		step("its first %lu bytes decrypted %lu ms after the request", (unsigned long)len,
		     pf_ms() - t->asked);
		t->answer = 0;
	}
	if ((long)len > max)
		len = (size_t)max;
	memcpy(buf, p, len);
	br_ssl_engine_recvapp_ack(eng, len);
	return (long)len;
}

long tls_write(TLS *t, const void *buf, long n)
{
	br_ssl_engine_context *eng = &t->cc.eng;
	long done = 0;
	/* the times of the answer, for the log */
	t->asked = pf_ms();
	t->answer = 1;
	while (done < n) {
		size_t len;
		unsigned char *p;
		if (pump(t, BR_SSL_SENDAPP, 1) <= 0)
			return -1;
		p = br_ssl_engine_sendapp_buf(eng, &len);
		if ((long)len > n - done)
			len = (size_t)(n - done);
		memcpy(p, (const char *)buf + done, len);
		br_ssl_engine_sendapp_ack(eng, len);
		done += (long)len;
	}
	br_ssl_engine_flush(eng, 0);
	/* push the records out now */
	if (pump(t, BR_SSL_RECVAPP | BR_SSL_SENDAPP, 0) < 0)
		return -1;
	return n;
}

void tls_close(TLS *t)
{
	if (!t)
		return;
	br_ssl_engine_close(&t->cc.eng);
	pump(t, 0, 0);
	memset(t, 0, sizeof(*t));
	free(t);
}
