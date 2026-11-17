/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_tlsfb.c - the shipped tlsfb.ovl (TLS.OVL's ColdFire build) run as a
 * program runs it, under qemu-m68k -cpu cfv4e: loaded and relocated by
 * src/ovl.c's ovl_load (ColdFire code too), its TLS_OVL table checked,
 * init() given a TLS_HOST of Linux calls (files, the clock, /dev/urandom,
 * TCP sockets), then CACERT.PEM with the test's two authorities added
 * (decoded, then again from the ROOTS.DAT it made) and TLS 1.2 to
 * tlsfb_run.py's servers on 127.0.0.1: RSA and ECDSA certificates,
 * X25519, P-256 and P-384, ChaCha20, AES-GCM, AES-CBC and the RSA key
 * exchange, a session resumed and forgotten, and the refusals (a wrong
 * name, an authority not loaded, ECDSA to a hello that asks for RSA).
 * Each answer is 40,000 bytes the server made from the request's seed,
 * checked byte for byte.
 *
 *   test_tlsfb TLSFB.OVL CAFILE WORKDIR PORT0 ... PORT7
 *
 * What qemu-user can't do is the host's: ovl_flush (cpushl, supervisor
 * only) isn't called, and Supexec calls its function in user mode.
 */
#include <stddef.h>
#include <stdarg.h>
#include "rt.h"
#include "tlsovl.h"
#include "ovl.h"

#ifndef WANT_VERSION
#define WANT_VERSION "?"
#endif

#define TLS_RSA_ONLY     1		/* tls.h's */
#define TLS_CHACHA_ONLY  2

static int fails;

static void check(int ok, const char *what)
{
	out(ok ? "ok    " : "FAIL  ");
	out(what);
	out("\n");
	if (!ok)
		fails++;
}

/* ---- snprintf for ovl.c's messages (%s, %d, %ld, %lu) ---- */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
	size_t k = 0;
	char num[12];
	const char *s;
	unsigned long v;
	int neg, i, lng;
#define PUT(c) do { if (k + 1 < n) buf[k] = (c); k++; } while (0)
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			PUT(*fmt);
			continue;
		}
		fmt++;
		lng = 0;
		if (*fmt == 'l')
			lng = 1, fmt++;
		if (*fmt == 's') {
			for (s = va_arg(ap, const char *); *s; s++)
				PUT(*s);
			continue;
		}
		if (*fmt == 'd' || *fmt == 'u') {
			long x = lng ? va_arg(ap, long) : va_arg(ap, int);
			neg = *fmt == 'd' && x < 0;
			v = neg ? (unsigned long)-x : (unsigned long)x;
			i = 0;
			do num[i++] = (char)('0' + v % 10); while ((v /= 10) != 0);
			if (neg)
				PUT('-');
			while (i)
				PUT(num[--i]);
			continue;
		}
		PUT('%');
		PUT(*fmt);
	}
	if (n)
		buf[k < n ? k : n - 1] = 0;
#undef PUT
	return (int)k;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- the program's side: what TLS_HOST lends, on Linux calls ---- */
void (*pf_idle)(void);

#define PF_READ   0
#define PF_WRITE  1
#define PF_APPEND 2
#define PF_UPDATE 3

static long opens, supexecs, cookies;

int pf_open(const char *path, int mode)
{
	static const long flags[4] = { 0, 01 | 0100 | 01000, 01 | 0100 | 02000, 01 };
	long fd;
	if (mode < 0 || mode > 3)
		return -1;
	fd = sys_open(path, flags[mode], 0644);
	opens++;
	return fd < 0 ? -1 : (int)fd;
}
long pf_read(int h, void *buf, long n) { long r = sys_read(h, buf, n); return r < 0 ? -1 : r; }
long pf_write(int h, const void *buf, long n) { long r = sys_write(h, buf, n); return r < 0 ? -1 : r; }
long pf_seek(int h, long off) { long r = sys_call4(19, h, off, 0, 0); return r < 0 ? -1 : r; }
void pf_close(int h) { if (h >= 0) sys_close(h); }
long pf_size(const char *path)
{
	long fd = sys_open(path, 0, 0), n;
	if (fd < 0)
		return -1;
	n = sys_call4(19, fd, 0, 2, 0);		/* lseek to the end */
	sys_close(fd);
	return n < 0 ? -1 : n;
}
char *pf_load(const char *path, long *len)
{
	long size = pf_size(path), got = 0, r;
	char *buf;
	int h;
	*len = 0;
	if (size < 0 || (h = pf_open(path, PF_READ)) < 0)
		return 0;
	buf = malloc((size_t)size + 1);
	while (buf && got < size && (r = pf_read(h, buf + got, size - got)) > 0)
		got += r;
	pf_close(h);
	if (buf && got != size) {
		free(buf);
		return 0;
	}
	if (buf) {
		buf[size] = 0;
		*len = size;
	}
	return buf;
}
int pf_save(const char *path, const void *data, long len)
{
	int h = pf_open(path, PF_WRITE);
	long w;
	if (h < 0)
		return -1;
	w = pf_write(h, data, len);
	pf_close(h);
	return w == len ? 0 : -1;
}
int pf_remove(const char *path) { return sys_call4(10, (long)path, 0, 0, 0) ? -1 : 0; }

static void entropy(unsigned char *buf, int n)
{
	long fd = sys_open("/dev/urandom", 0, 0);
	if (fd < 0 || sys_read(fd, buf, n) != n) {
		out("FAIL  /dev/urandom\n");
		sys_exit(1);
	}
	sys_close(fd);
}

static unsigned long ms(void)
{
	long ts[2];
	sys_call4(260, 1, (long)ts, 0, 0);	/* clock_gettime(CLOCK_MONOTONIC) */
	return (unsigned long)ts[0] * 1000UL + (unsigned long)ts[1] / 1000000UL;
}

/* UTC now, as a PFTIME (year, mon, day, hour, min, sec) */
static void now(void *p)
{
	short *t = p;
	long tv[2];
	unsigned long secs, days, z, era, doe, yoe, doy, mp, y;
	sys_call4(78, (long)tv, 0, 0, 0);	/* gettimeofday */
	secs = (unsigned long)tv[0];
	days = secs / 86400UL;
	secs %= 86400UL;
	/* days since 1970 to the civil date (H. Hinnant's) */
	z = days + 719468UL;
	era = z / 146097UL;
	doe = z - era * 146097UL;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	t[2] = (short)(doy - (153 * mp + 2) / 5 + 1);
	t[1] = (short)(mp < 10 ? mp + 3 : mp - 9);
	t[0] = (short)(y + (t[1] <= 2));
	t[3] = (short)(secs / 3600);
	t[4] = (short)(secs / 60 % 60);
	t[5] = (short)(secs % 60);
}

static long net_read(int h, void *buf, long max)
{
	long r = sys_read(h, buf, max);
	return r > 0 ? r : -1;		/* a blocking socket: 0 is the end */
}
static long net_write(int h, const void *buf, long n)
{
	long r = sys_write(h, buf, n);
	return r > 0 ? r : -1;
}

static int verbose;
static void conn_log(const char *who, const char *dir, const char *text, long n)
{
	if (!verbose)
		return;
	out("      log ");
	out(who);
	out(dir);
	sys_write(1, text, n);
	out("\n");
}

static long supexec(void *f)
{
	supexecs++;
	return ((long (*)(void))f)();	/* user mode here: QEMU has no supervisor */
}

static long get_cookie(long id)
{
	cookies++;
	if (id == 0x5F43465FL)		/* _CF_ */
		return 1;
	if (id == 0x5F435055L)		/* _CPU: FireTOS says 60 */
		return 60;
	return 0;
}


static const TLS_HOST host = {
	sizeof(TLS_HOST),
	malloc, calloc, realloc, free,
	net_read, net_write,
	pf_open, pf_read, pf_write, pf_seek, pf_close, pf_size, pf_load, pf_save, pf_remove,
	entropy, ms, now,
	conn_log,
	supexec, get_cookie
};

/* the overlay's hooks (TLS_VARS) */
static int notes;
static void note(const char *msg) { notes++; out("      note "); out(msg); out("\n"); }
static void step_log(const char *msg) { out("      tls  "); out(msg); out("\n"); }

/* ---- strings ---- */
static void cat(char *d, const char *s, int n)
{
	int k = (int)strlen(d);
	while (*s && k + 1 < n)
		d[k++] = *s++;
	d[k] = 0;
}
static int has(const char *s, const char *w)
{
	size_t n = strlen(w);
	for (; *s; s++)
		if (!memcmp(s, w, n))
			return 1;
	return 0;
}
static long num_of(const char *s)
{
	long v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + *s++ - '0';
	return v;
}
static void num_str(char *b, unsigned long v)
{
	char t[12];
	int i = 0, k = 0;
	do t[i++] = (char)('0' + v % 10); while ((v /= 10) != 0);
	while (i)
		b[k++] = t[--i];
	b[k] = 0;
}

/* ---- a TCP connection to 127.0.0.1 ---- */
static long tcp(int port)
{
	struct { unsigned short family, port; unsigned long addr; unsigned char zero[8]; } sa;
	long fd, a[3];
	memset(&sa, 0, sizeof(sa));
	sa.family = 2;			/* AF_INET; m68k is big-endian: port and */
	sa.port = (unsigned short)port;	/* address are in the network's order */
	sa.addr = 0x7F000001UL;
	fd = sys_call4(356, 2, 1, 0, 0);	/* socket(AF_INET, SOCK_STREAM, 0) */
	if (fd == -38) {			/* ENOSYS: socketcall */
		a[0] = 2, a[1] = 1, a[2] = 0;
		fd = sys_call4(102, 1, (long)a, 0, 0);
	}
	if (fd < 0)
		return -1;
	if (sys_call4(359, fd, (long)&sa, sizeof(sa), 0) < 0) {
		sys_close(fd);
		return -1;
	}
	return fd;
}

/* the server's bytes: an LCG from the request's seed (tlsfb_run.py's) */
static unsigned char lcg(unsigned long *x)
{
	*x = *x * 1103515245UL + 12345UL;
	return (unsigned char)(*x >> 24);
}

#define BODY 40000L
static unsigned char got[BODY + 1024];

static const TLS_OVL *ov;
static int nports, port[8];

typedef struct {
	const char *name;
	int server;			/* tlsfb_run.py's SERVERS */
	int flags;
	const char *host;
	const char *want;		/* tls_describe's, or NULL: refused */
	int retry;			/* refused: *retry wanted */
	int forget;			/* forget_sessions first */
} CASE;

static const CASE cases[] = {
	{ "RSA, X25519, ChaCha20 only", 0, TLS_CHACHA_ONLY, "localhost", "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305", 0, 0 },
	{ "the same server again", 0, TLS_CHACHA_ONLY, "localhost", "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305, resumed", 0, 0 },
	{ "again after forget_sessions", 0, TLS_CHACHA_ONLY, "localhost", "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305", 0, 1 },
	{ "RSA, P-256, AES-128-GCM", 1, 0, "localhost", "TLS 1.2, ECDHE-RSA, AES-128-GCM", 0, 0 },
	{ "RSA, P-384, AES-256-GCM, RSA only", 2, TLS_RSA_ONLY, "localhost", "TLS 1.2, ECDHE-RSA, AES-256-GCM", 0, 0 },
	{ "RSA, AES-128-CBC-SHA256", 3, 0, "localhost", "TLS 1.2, ECDHE-RSA, AES-128-CBC", 0, 0 },
	{ "RSA key exchange, AES-128-GCM", 4, TLS_RSA_ONLY, "localhost", "TLS 1.2, RSA, AES-128-GCM", 0, 0 },
	{ "ECDSA P-256, ChaCha20", 5, 0, "localhost", "TLS 1.2, ECDHE-ECDSA, ChaCha20-Poly1305", 0, 0 },
	{ "ECDSA P-384, P-384, AES-128-GCM", 6, 0, "localhost", "TLS 1.2, ECDHE-ECDSA, AES-128-GCM", 0, 0 },
	{ "refused: ECDSA only, the hello asks for RSA", 5, TLS_RSA_ONLY, "localhost", 0, 1, 0 },
	{ "refused: the certificate is for another name", 0, 0, "atari.example.org", 0, 0, 0 },
	{ "refused: an authority not loaded", 7, 0, "localhost", 0, 0, 0 },
};

static void run(const CASE *c, unsigned long seed)
{
	char err[160], req[120], line[200], s[12];
	struct TLS *t;
	long fd, n = 0, r, hdr = -1, i;
	int retry = -1;
	unsigned long x = seed, t0;
	out("== ");
	out(c->name);
	out("\n");
	if (c->forget)
		ov->forget_sessions();
	if ((fd = tcp(port[c->server])) < 0) {
		check(0, "connected");
		return;
	}
	err[0] = 0;
	t0 = ms();
	t = ov->start((int)fd, c->host, c->flags, &retry, err, sizeof(err));
	if (!c->want) {
		line[0] = 0;
		cat(line, "refused: ", sizeof(line));
		cat(line, err, sizeof(line));
		check(!t && err[0], line);
		if (c->retry)
			check(retry == c->retry, "and *retry says what to leave out");
		if (t)
			ov->close(t);
		sys_close(fd);
		return;
	}
	if (!t) {
		line[0] = 0;
		cat(line, "handshake: ", sizeof(line));
		cat(line, err, sizeof(line));
		check(0, line);
		sys_close(fd);
		return;
	}
	line[0] = 0;
	cat(line, "handshake in ", sizeof(line));
	num_str(s, ms() - t0);
	cat(line, s, sizeof(line));
	cat(line, " ms: ", sizeof(line));
	cat(line, ov->describe(t), sizeof(line));
	check(!strcmp(ov->describe(t), c->want), line);
	req[0] = 0;
	cat(req, "GET /", sizeof(req));
	num_str(s, seed);
	cat(req, s, sizeof(req));
	cat(req, " HTTP/1.0\r\nHost: localhost\r\n\r\n", sizeof(req));
	check(ov->write(t, req, (long)strlen(req)) == (long)strlen(req), "the request written");
	while (n < (long)sizeof(got) && (r = ov->read(t, got + n, (long)sizeof(got) - n)) >= 0)
		n += r;
	for (i = 0; i + 3 < n; i++)
		if (!memcmp(got + i, "\r\n\r\n", 4)) {
			hdr = i + 4;
			break;
		}
	check(hdr > 0 && !memcmp(got, "HTTP/1.0 200 OK", 15) && has((char *)got, "Content-Length: 40000"),
	      "the answer's head");
	if (hdr > 0) {
		for (i = 0; i < BODY && hdr + i < n; i++)
			if (got[hdr + i] != lcg(&x))
				break;
		line[0] = 0;
		cat(line, "the answer's ", sizeof(line));
		num_str(s, (unsigned long)(n - hdr));
		cat(line, s, sizeof(line));
		cat(line, " bytes are the server's, byte for byte", sizeof(line));
		check(i == BODY && n - hdr == BODY, line);
	}
	ov->close(t);
	sys_close(fd);
}

static void ignore_sigpipe(void)
{
	struct { void *handler; unsigned long flags; void *restorer; unsigned long mask[2]; } sa;
	memset(&sa, 0, sizeof(sa));
	sa.handler = (void *)1;		/* SIG_IGN: a write to a closed socket gives EPIPE */
	sys_call4(174, 13, (long)&sa, 0, 8);
}

int main(int argc, char **argv)
{
	char err[160], line[200], s[12], seed_path[200], sigs_path[200], roots[200];
	long taken, text, i;
	unsigned char hdr[28];
	const TLS_VARS *v;
	unsigned long *fn;
	int nanch, ok;
	if (argc < 12) {
		out("test_tlsfb TLSFB.OVL CAFILE WORKDIR PORT0 ... PORT7 [-v]\n");
		return 2;
	}
	verbose = argc > 12;
	ignore_sigpipe();
	for (nports = 0; nports < 8; nports++)
		port[nports] = (int)num_of(argv[4 + nports]);

	out("== tlsfb.ovl loaded by ovl.c (ColdFire code), as a program loads it\n");
	if (read_file(argv[1], hdr, sizeof(hdr)) != sizeof(hdr)) {
		check(0, "tlsfb.ovl read");
		return 1;
	}
	text = (long)hdr[2] << 24 | (long)hdr[3] << 16 | hdr[4] << 8 | hdr[5];
	ov = ovl_load(argv[1], &taken, err, sizeof(err));
	if (!ov) {
		check(0, err);
		return 1;
	}
	line[0] = 0;
	cat(line, "loaded and relocated: ", sizeof(line));
	num_str(s, (unsigned long)((taken + 1023) / 1024));
	cat(line, s, sizeof(line));
	cat(line, " KB (text ", sizeof(line));
	num_str(s, (unsigned long)text);
	cat(line, s, sizeof(line));
	cat(line, ")", sizeof(line));
	check(1, line);
	check(ov->magic == TLSOVL_MAGIC, "TLS_OVL's magic, \"TLSO\"");
	check(ov->major == TLSOVL_MAJOR, "its major is this program's");
	check(ov->size >= (short)sizeof(TLS_OVL), "its table is as long as this program knows it");
	line[0] = 0;
	cat(line, "its version is VERSION's, ", sizeof(line));
	cat(line, ov->version, sizeof(line));
	check(!strcmp(ov->version, WANT_VERSION), line);
	/* every entry relocated into its text (vars too: a const table, in
	   the rodata ovl.ld puts there) */
	ok = 1;
	for (fn = (unsigned long *)&ov->init; fn <= (unsigned long *)&ov->dsp_ec_state; fn++)
		if (*fn < (unsigned long)ov || *fn >= (unsigned long)ov + (unsigned long)text)
			ok = 0;
	check(ok, "every entry relocated into its text");
	/* (ovl_flush not called: cpushl is supervisor-only, and QEMU keeps
	   its translations right by itself) */

	out("== init with this program's TLS_HOST\n");
	check(ov->init(&host) == 1, "init accepts it");
	{
		TLS_HOST small = host;
		small.size = 8;
		check(ov->init(&small) == 0, "and refuses a table too short");
		check(ov->init(&host) == 1, "then takes the whole one again");
	}
	check(!strcmp(ov->dsp_rsa_state(), "no DSP (a ColdFire)") && !strcmp(ov->dsp_ec_state(), "no DSP (a ColdFire)"),
	      "its DSP entries say there is none");
	ov->dsp_rsa(1);
	ov->dsp_ec(1);
	check(!ov->ec_fast(), "the curves are not fast (no DSP), so RSA is asked for first");

	v = ov->vars;
	seed_path[0] = sigs_path[0] = roots[0] = 0;
	cat(seed_path, argv[3], 200), cat(seed_path, "/seed.dat", 200);
	cat(sigs_path, argv[3], 200), cat(sigs_path, "/sigs.dat", 200);
	cat(roots, argv[3], 200), cat(roots, "/roots.dat", 200);
	*v->tz_minutes = 0;
	memcpy(v->seed_path, seed_path, 200);
	memcpy(v->sigs_path, sigs_path, 200);
	memcpy(v->roots_cache, roots, 200);
	*v->file_stamp = 0;
	*v->read_at = 0;
	*v->file_info = 0;
	*v->note = note;
	*v->log = verbose ? step_log : 0;
	*v->idle = 0;

	out("== the root certificates: CACERT.PEM and the test's two authorities\n");
	pf_remove(roots);
	nanch = ov->load_anchors(argv[2], err, sizeof(err));
	line[0] = 0;
	cat(line, "decoded: ", sizeof(line));
	num_str(s, (unsigned long)nanch);
	cat(line, s, sizeof(line));
	cat(line, " anchors", sizeof(line));
	check(nanch > 100 && ov->anchor_count() == nanch && notes == 1, line);
	check(pf_size(roots) > 0, "roots.dat made");
	ov->free_anchors();
	check(ov->anchor_count() == 0, "free_anchors forgets them");
	notes = 0;
	check(ov->load_anchors(argv[2], err, sizeof(err)) == nanch && notes == 0,
	      "loaded again from roots.dat, not decoded");

	for (i = 0; i < (long)(sizeof(cases) / sizeof(cases[0])); i++)
		run(&cases[i], 1000UL + (unsigned long)i * 7919UL);

	out("== the files TLS.OVL keeps\n");
	check(pf_size(seed_path) == 32, "seed.dat, 32 bytes");
	check(pf_size(sigs_path) > 0, "sigs.dat");
	ov->free_anchors();
	ov->forget_sessions();
	line[0] = 0;
	cat(line, "files opened ", sizeof(line));
	num_str(s, (unsigned long)opens);
	cat(line, s, sizeof(line));
	cat(line, ", Supexec ", sizeof(line));
	num_str(s, (unsigned long)supexecs);
	cat(line, s, sizeof(line));
	cat(line, ", cookies asked ", sizeof(line));
	num_str(s, (unsigned long)cookies);
	cat(line, s, sizeof(line));
	check(1, line);

	if (fails) {
		out("tlsfb: ");
		out_num((unsigned long)fails);
		out(" FAILED\n");
		return 1;
	}
	out("tlsfb: the shipped tlsfb.ovl, as ColdFire code, makes TLS 1.2 connections and refuses the bad ones\n");
	return 0;
}
