/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bearssl.h"
#include "plat.h"
#include "util.h"
#include "tls.h"

/* from BearSSL's inner.h */
#define BR_MAX_RSA_SIZE 4096
uint32_t br_rsa_pkcs1_sig_unpad(const unsigned char *sig, size_t sig_len,
				const unsigned char *hash_oid, size_t hash_len, unsigned char *hash_out);

const int tls_available = 1;
int tls_tz_minutes;
char tls_seed_path[200];
char tls_roots_cache[200];
void (*tls_note)(const char *msg);
int (*tls_rsa_accel)(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		     const unsigned char *e, size_t elen);

struct TLS {
	br_ssl_client_context cc;
	br_x509_minimal_context xc;
	int sock;
	char desc[80];
	int offered, resumed;		/* a kept session offered; taken up */
	unsigned char offered_id[32];
	unsigned char iobuf[BR_SSL_BUFSIZE_MONO];
};

/* ---------------- root certificates ---------------- */

static br_x509_trust_anchor *anchors;
static size_t nanchors, maxanchors;

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
#define CACHE_MAGIC "MAILROOTS1"

static unsigned long file_sum(const char *path, long *size)
{
	char chunk[4096];
	unsigned long h = 2166136261UL;		/* FNV-1a */
	long got;
	int f = pf_open(path, PF_READ);
	*size = -1;
	if (f < 0)
		return 0;
	*size = 0;
	while ((got = pf_read(f, chunk, sizeof(chunk))) > 0) {
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

static void save_cache(long size, unsigned long sum)
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
	out_bytes(&o, CACHE_MAGIC, 10);
	put32(&o, (unsigned long)size);
	put32(&o, sum);
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

static int load_cache(long size, unsigned long sum)
{
	long len;
	unsigned char *buf = tls_roots_cache[0] ? (unsigned char *)pf_load(tls_roots_cache, &len) : 0;
	const unsigned char *p, *end;
	unsigned long count, i;
	int bad = 0;
	if (!buf)
		return 0;
	p = buf;
	end = buf + len;
	if (len < 22 || memcmp(p, CACHE_MAGIC, 10)) {
		free(buf);
		return 0;
	}
	p += 10;
	if (get32(&p, end, &bad) != (unsigned long)size || get32(&p, end, &bad) != sum || bad) {
		free(buf);
		return 0;
	}
	count = get32(&p, end, &bad);
	anchors = calloc(count ? count : 1, sizeof(*anchors));
	if (!anchors) {
		free(buf);
		return 0;
	}
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
	free(buf);
	return !bad && nanchors == count;
}

int tls_load_anchors(const char *path, char *err, int errlen)
{
	br_pem_decoder_context pc;
	GROW der;
	char chunk[4096];
	long got, size;
	unsigned long sum;
	int h, in_cert = 0;
	if (nanchors)
		return (int)nanchors;
	sum = file_sum(path, &size);
	if (size < 0) {
		snprintf(err, errlen, "no root certificates: %s is missing", path);
		return 0;
	}
	if (load_cache(size, sum))
		return (int)nanchors;
	nanchors = 0;
	if (tls_note)
		tls_note("Reading the root certificates (only this once, it takes a while)...");
	/* read in pieces: the whole list is about 190 KB, more than a
	   1 MB Falcon wants to hold at once next to the program */
	h = pf_open(path, PF_READ);
	if (h < 0) {
		snprintf(err, errlen, "no root certificates: can't open %s", path);
		return 0;
	}
	der.max = 4096;
	der.len = 0;
	der.p = malloc(der.max);
	br_pem_decoder_init(&pc);
	while (der.p && (got = pf_read(h, chunk, sizeof(chunk))) > 0) {
		long pos = 0;
		while (pos < got) {
			pos += (long)br_pem_decoder_push(&pc, chunk + pos, (size_t)(got - pos));
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
	pf_close(h);
	if (!der.p)
		snprintf(err, errlen, "not enough memory to read %s", path);
	free(der.p);
	if (!nanchors && der.p)
		snprintf(err, errlen, "no root certificates found in %s", path);
	if (nanchors)
		save_cache(size, sum);
	return (int)nanchors;
}

/* ---------------- RSA through the accelerator ---------------- */

static uint32_t rsa_public(unsigned char *x, size_t xlen, const br_rsa_public_key *pk)
{
	if (tls_rsa_accel && tls_rsa_accel(x, xlen, pk->n, pk->nlen, pk->e, pk->elen))
		return 1;
	return br_rsa_i31_public(x, xlen, pk);
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

/* ---------------- randomness ----------------
 * Fresh timer jitter (pf_entropy) hashed together with a seed kept on
 * disk, so what each session gathers adds up. The seed is replaced
 * every time, with a value that can't be traced back to what was used. */
static void get_seed(unsigned char out[48])
{
	static unsigned long count;
	unsigned char raw[128], old[32], h[32];
	br_sha256_context sc;
	long len = 0;
	char *f = tls_seed_path[0] ? pf_load(tls_seed_path, &len) : 0;
	memset(old, 0, sizeof(old));
	if (f) {
		memcpy(old, f, len < 32 ? len : 32);
		free(f);
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
	if (tls_seed_path[0])
		pf_save(tls_seed_path, h, sizeof(h));
	memset(raw, 0, sizeof(raw));
	memset(old, 0, sizeof(old));
	memset(h, 0, sizeof(h));
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
				continue;
			}
			if (!blocking)
				return 0;
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
	int r, rsa_only = flags & TLS_RSA_ONLY, chacha_only = flags & TLS_CHACHA_ONLY;
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
	if (rsa_only)
		br_ssl_engine_set_ecdsa(&t->cc.eng, 0);
	/* the fastest code for a 32-bit CPU without fast 64-bit products */
	br_ssl_engine_set_ec(&t->cc.eng, &br_ec_all_m31);
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
	br_x509_minimal_set_rsa(&t->xc, rsa_vrfy);
	br_x509_minimal_set_ecdsa(&t->xc, &br_ec_all_m31, br_ecdsa_i31_vrfy_asn1);
	set_time(&t->xc);
	br_ssl_engine_set_buffer(&t->cc.eng, t->iobuf, sizeof(t->iobuf), 0);
	get_seed(seed);
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
	r = pump(t, BR_SSL_SENDAPP, 1);
	if (r <= 0) {
		int e = br_ssl_engine_last_error(&t->cc.eng);
		if (!e)
			e = BR_ERR_IO;
		/* refused with ChaCha20 only (an alert, or a closed connection
		   from servers that don't bother): try again with every cipher.
		   40 handshake_failure, 71 insufficient_security with all of
		   them: nothing in common with an RSA-only hello */
		if (chacha_only && retry &&
		    (e == BR_ERR_IO || (e > BR_ERR_RECV_FATAL_ALERT && e < BR_ERR_RECV_FATAL_ALERT + 256)))
			*retry = TLS_RETRY_ANY_CIPHER;
		else if (rsa_only && retry &&
			 (e == BR_ERR_RECV_FATAL_ALERT + 40 || e == BR_ERR_RECV_FATAL_ALERT + 71))
			*retry = TLS_RETRY_ECDSA;
		explain(e, host, err, errlen);
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
