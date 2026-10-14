/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * conn.c - buffered TCP connection on top of plat.h's net_*, with TLS
 * when Falcon mode talks to the server itself: EMail's cipher retries
 * (ChaCha20 alone first, then every cipher; RSA certificates first, then
 * ECDSA), ECal's handshake timing in the log, EFTP's Stop and raw reads.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "plat.h"
#include "conn.h"
#include "tls.h"

char conn_logfile[200];
char conn_cacert[200];
int conn_tls_retry;

/* servers that only have ECDSA certificates, and servers that refused
   ChaCha20 alone: later connections offer that at once */
static char ecdsa_hosts[8][64], aes_hosts[8][64];

static int listed(char list[8][64], const char *host)
{
	int i;
	for (i = 0; i < 8; i++)
		if (!strcmp(list[i], host))
			return 1;
	return 0;
}

static void note_host(char list[8][64], int *next, const char *host)
{
	if (listed(list, host))
		return;
	str_copy(list[*next], host, 64);
	*next = (*next + 1) % 8;
}

/* TLS on the open socket of c; host names the server for its
   certificate and for the session kept to resume */
static int start_tls(CONN *c, const char *host, char *err, int errlen)
{
	static int next_ecdsa, next_aes;
	TLS *t;
	int retry = 0;
	char msg[200];
	unsigned long t0;
	conn_tls_retry = 0;
	if (!tls_anchor_count()) {
		int n;
		t0 = pf_ms();
		n = tls_load_anchors(conn_cacert, err, errlen);
		if (!n) {
			conn_log(c->name, " !! ", err, strlen(err));
			return 0;
		}
		snprintf(msg, sizeof(msg), "%d root certificates read in %lu ms", n, pf_ms() - t0);
		conn_log("TLS", " -- ", msg, strlen(msg));
	}
	t0 = pf_ms();
	t = tls_start(c->h, host, (listed(ecdsa_hosts, host) ? 0 : TLS_RSA_ONLY) |
		      (listed(aes_hosts, host) ? 0 : TLS_CHACHA_ONLY), &retry, err, errlen);
	c->tls_ms = pf_ms() - t0;
	if (!t) {
		if (retry == TLS_RETRY_ANY_CIPHER) {
			conn_log(c->name, " -- ", "no ChaCha20 here: again with every cipher", 41);
			note_host(aes_hosts, &next_aes, host);
			conn_tls_retry = 1;
		} else {
			conn_log(c->name, " !! ", err, strlen(err));
			if (retry) {
				note_host(ecdsa_hosts, &next_ecdsa, host);
				conn_tls_retry = 1;
			}
		}
		return 0;
	}
	c->tls = t;
	snprintf(msg, sizeof(msg), "secure: %s (%lu ms)", tls_describe(t), c->tls_ms);
	conn_log(c->name, " -- ", msg, strlen(msg));
	return 1;
}

int conn_starttls(CONN *c, const char *host, char *err, int errlen)
{
	return start_tls(c, host, err, errlen);
}

const char *conn_security(CONN *c)
{
	return c && c->tls ? tls_describe(c->tls) : "";
}

long conn_bytes, conn_bytes_out;
int conn_stop;
void (*conn_tick)(void);

/* the log is written in blocks: opening the file for every line made a
   big download crawl on an Atari disk. Milestones (" -- ", " !! ")
   write it out at once, so a crash loses little */
static char logbuf[4096];
static int loglen;

void conn_log_flush(void)
{
	int h;
	if (!loglen)
		return;
	if (conn_logfile[0] && (h = pf_open(conn_logfile, PF_APPEND)) >= 0) {
		pf_write(h, logbuf, loglen);
		pf_close(h);
	}
	loglen = 0;
}

void conn_log_append(const char *text, long n)
{
	int h;
	conn_log_flush();
	if (conn_logfile[0] && (h = pf_open(conn_logfile, PF_APPEND)) >= 0) {
		pf_write(h, text, n);
		pf_close(h);
	}
}

static void log_add(const char *s, long n)
{
	while (n > 0) {
		long k = (long)sizeof(logbuf) - loglen;
		if (k > n)
			k = n;
		memcpy(logbuf + loglen, s, k);
		loglen += (int)k;
		s += k;
		n -= k;
		if (loglen == (int)sizeof(logbuf))
			conn_log_flush();
	}
}

/* "14:05:31.27 ": the time of day once (the Atari's clock counts in
   2-second steps), the 200 Hz timer from there on */
static void log_stamp(void)
{
	static unsigned long base_ms, base_s;
	static int have;
	unsigned long t;
	char st[16];
	if (!have) {
		PFTIME now;
		pf_now(&now);
		base_s = now.hour * 3600UL + now.min * 60UL + now.sec;
		base_ms = pf_ms();
		have = 1;
	}
	t = base_s * 100UL + (pf_ms() - base_ms) / 10;
	snprintf(st, sizeof(st), "%02lu:%02lu:%02lu.%02lu ", t / 360000UL % 24, t / 6000UL % 60,
		 t / 100UL % 60, t % 100UL);
	log_add(st, strlen(st));
}

void conn_log(const char *who, const char *dir, const char *text, long n)
{
	if (!conn_logfile[0])
		return;
	log_stamp();
	log_add(who, strlen(who));
	log_add(dir, strlen(dir));
	if (n > 400) {
		char tmp[40];
		log_add(text, 200);
		snprintf(tmp, sizeof(tmp), " [... %ld bytes]", n);
		log_add(tmp, strlen(tmp));
	} else {
		log_add(text, n);
	}
	log_add("\r\n", 2);
	if (dir[1] == '-' || dir[1] == '!')
		conn_log_flush();
}

/* the receive buffer when conn_open is given 0: 16 KB, or less where
   memory is short (EMail sets 4 KB on a small Atari) */
long conn_bufsize = 16384;

static CONN *open_plain(const char *name, const char *host, unsigned short port, long bufsize,
			char *err, int errlen)
{
	CONN *c;
	int h;
	char msg[200];
	if (bufsize <= 0)
		bufsize = conn_bufsize;
	snprintf(msg, sizeof(msg), "connecting to %s:%u", host, port);
	conn_log(name, " -- ", msg, strlen(msg));
	h = net_open(host, port, err, errlen);
	if (h < 0) {
		conn_log(name, " !! ", err, strlen(err));
		return 0;
	}
	c = calloc(1, sizeof(CONN) + bufsize);
	if (!c) {
		net_close(h);
		str_copy(err, "out of memory", errlen);
		return 0;
	}
	c->h = h;
	c->buf = (char *)(c + 1);
	c->bufsize = bufsize;
	c->timeout_ms = 60000;
	c->name = name;
	return c;
}

CONN *conn_open(const char *name, const char *host, unsigned short port, long bufsize, int sec,
		char *err, int errlen)
{
	int attempt;
	for (attempt = 0; attempt < 3; attempt++) {	/* ChaCha20 alone, then ECDSA */
		CONN *c = open_plain(name, host, port, bufsize, err, errlen);
		if (!c || sec != SEC_TLS)
			return c;
		if (start_tls(c, host, err, errlen))
			return c;
		conn_close(c);
		if (!conn_tls_retry)
			return 0;
	}
	conn_tls_retry = 0;	/* tried every way */
	return 0;
}

void conn_close(CONN *c)
{
	if (!c)
		return;
	conn_log(c->name, " -- ", "closed", 6);
	if (c->tls)
		tls_close(c->tls);
	net_close(c->h);
	free(c);
}

void conn_log_login(const char *who, const char *user, const char *pass)
{
	char line[160];
	const char *p;
	int dashes = 0, spaces = 0, high = 0;
	if (!conn_logfile[0])
		return;
	for (p = pass; *p; p++) {
		dashes += *p == '-';
		spaces += *p == ' ';
		high += (unsigned char)*p >= 0x80;
	}
	snprintf(line, sizeof(line), "logging in: user name %d characters%s, password %d characters"
		 " (%d '-', %d spaces, %d not ASCII)", (int)strlen(user),
		 strchr(user, '@') ? " with @" : " without @", (int)strlen(pass), dashes, spaces, high);
	conn_log(who, " -- ", line, (long)strlen(line));
}

int conn_write(CONN *c, const char *data, long n)
{
	/* in pieces, so a long upload can show how far it got */
	while (n > 0) {
		long k = n > 4096 ? 4096 : n;
		if (conn_stop || (c->tls ? tls_write(c->tls, data, k) : net_write(c->h, data, k)) != k) {
			str_copy(c->err, "connection lost while sending", sizeof(c->err));
			return 0;
		}
		data += k;
		n -= k;
		conn_bytes_out += k;
		if (conn_tick)
			conn_tick();
	}
	return 1;
}

int conn_cmd(CONN *c, int secret, const char *fmt, ...)
{
	va_list ap;
	SBUF b;
	char tmp[1024];
	int n, ok;
	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp) - 2, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof(tmp) - 3)
		n = sizeof(tmp) - 3;
	if (secret) {
		/* log the command word(s) only */
		char *sp = strchr(tmp, ' ');
		long keep = sp ? sp - tmp : n;
		if (sp && secret > 1) {
			char *sp2 = strchr(sp + 1, ' ');
			if (sp2)
				keep = sp2 - tmp;
		}
		sb_init(&b);
		sb_add(&b, tmp, keep);
		sb_adds(&b, " ********");
		conn_log(c->name, " >> ", b.s, b.len);
		sb_free(&b);
	} else {
		conn_log(c->name, " >> ", tmp, n);
	}
	tmp[n++] = '\r';
	tmp[n++] = '\n';
	ok = conn_write(c, tmp, n);
	return ok;
}

/* make sure there is buffered data; 0 on timeout or close */
static int fill(CONN *c)
{
	unsigned long start = pf_ms();
	if (c->pos < c->len)
		return 1;
	c->pos = c->len = 0;
	for (;;) {
		long r = c->tls ? tls_read(c->tls, c->buf, c->bufsize) : net_read(c->h, c->buf, c->bufsize);
		if (r > 0) {
			c->len = r;
			conn_bytes += r;
			if (conn_tick)
				conn_tick();
			return 1;
		}
		if (r < 0) {
			str_copy(c->err, "the server closed the connection", sizeof(c->err));
			return 0;
		}
		if (conn_stop) {
			str_copy(c->err, "stopped", sizeof(c->err));
			return 0;
		}
		if (pf_ms() - start > (unsigned long)c->timeout_ms) {
			str_copy(c->err, "timed out waiting for the server", sizeof(c->err));
			return 0;
		}
		if (pf_idle)
			pf_idle();
	}
}

long conn_getline(CONN *c, SBUF *b)
{
	sb_reset(b);
	for (;;) {
		char *nl;
		long n;
		if (!fill(c))
			return -1;
		nl = memchr(c->buf + c->pos, '\n', c->len - c->pos);
		n = nl ? nl - (c->buf + c->pos) : c->len - c->pos;
		if (!sb_add(b, c->buf + c->pos, n)) {
			str_copy(c->err, "out of memory", sizeof(c->err));
			return -1;
		}
		c->pos += n;
		if (nl) {
			c->pos++;
			if (b->len && b->s[b->len - 1] == '\r')
				b->s[--b->len] = 0;
			if (!b->s)
				sb_add(b, "", 0);
			conn_log(c->name, " << ", b->s, b->len);
			return b->len;
		}
	}
}

int conn_getbytes(CONN *c, SBUF *b, long n)
{
	while (n > 0) {
		long k;
		if (!fill(c))
			return 0;
		k = c->len - c->pos;
		if (k > n)
			k = n;
		if (!sb_add(b, c->buf + c->pos, k)) {
			str_copy(c->err, "out of memory", sizeof(c->err));
			return 0;
		}
		c->pos += k;
		n -= k;
	}
	return 1;
}

/* the raw bytes of a data connection: what is buffered, or wait for
   more. > 0 bytes, 0 when the other side has closed (the end of a
   download), -1 on a timeout or when the wait was stopped */
long conn_read(CONN *c, char *buf, long max)
{
	unsigned long start = pf_ms();
	long k;
	if (conn_stop) {
		str_copy(c->err, "stopped", sizeof(c->err));
		return -1;
	}
	while (c->pos >= c->len) {
		long r = c->tls ? tls_read(c->tls, c->buf, c->bufsize) : net_read(c->h, c->buf, c->bufsize);
		if (r > 0) {
			c->pos = 0;
			c->len = r;
			conn_bytes += r;
			break;
		}
		if (r < 0)
			return 0;
		if (conn_stop) {
			str_copy(c->err, "stopped", sizeof(c->err));
			return -1;
		}
		if (pf_ms() - start > (unsigned long)c->timeout_ms) {
			str_copy(c->err, "timed out waiting for the server", sizeof(c->err));
			return -1;
		}
		if (pf_idle)
			pf_idle();
	}
	k = c->len - c->pos;
	if (k > max)
		k = max;
	memcpy(buf, c->buf + c->pos, k);
	c->pos += k;
	if (conn_tick)
		conn_tick();
	return k;
}

/* is a line (or anything) waiting, without waiting for it */
int conn_pending(CONN *c)
{
	long r;
	if (c->pos < c->len)
		return 1;
	r = c->tls ? tls_read(c->tls, c->buf, c->bufsize) : net_read(c->h, c->buf, c->bufsize);
	if (r > 0) {
		c->pos = 0;
		c->len = r;
		conn_bytes += r;
		return 1;
	}
	return r < 0;
}

/* a graceful close for an upload: what is still queued goes out first */
void conn_close_flush(CONN *c)
{
	if (!c)
		return;
	conn_log(c->name, " -- ", "closed", 6);
	if (c->tls)
		tls_close(c->tls);	/* close_notify: the server knows the file is whole */
	net_close_wait(c->h, 30);
	free(c);
}
