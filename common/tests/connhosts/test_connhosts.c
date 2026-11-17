/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_connhosts.c - conn.c keeps the hosts that need ECDSA or every
 * cipher across sessions in conn_hosts_path: a server with an ECDSA
 * certificate only (atari-home.de) refused the first session's first two
 * hellos (RSA with ChaCha20 alone, then RSA with every cipher), 2.0 s on a
 * stock Falcon (EWeb, 14 Nov 2026); the next session's first hello must
 * offer ECDSA and every cipher at once. Run twice: "1" (a new session, no
 * file) and "2" (the next session, the file from run 1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "plat.h"
#include "conn.h"
#include "tls.h"

static int fails, starts, last_flags;

static void check(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* the files, on this computer */
int pf_open(const char *path, int mode)
{
	return open(path, mode == PF_READ ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC, 0644);
}
long pf_read(int h, void *buf, long n) { return read(h, buf, (size_t)n); }
long pf_write(int h, const void *buf, long n) { return write(h, buf, (size_t)n); }
void pf_close(int h) { close(h); }
long pf_size(const char *path) { return -1; }
int pf_remove(const char *path) { return unlink(path); }
int pf_rename(const char *from, const char *to) { return rename(from, to); }
long pf_seek(int h, long off) { return lseek(h, off, SEEK_SET); }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return 0; }
void (*pf_idle)(void);
int net_open(const char *host, unsigned short port, char *err, int errlen) { return 5; }
long net_write(int h, const void *buf, long n) { return n; }
long net_read(int h, void *buf, long max) { return 0; }
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

/* TLS as a server with ChaCha20 and an ECDSA certificate only sees it */
const int tls_available = 1;
int tls_tz_minutes;
char tls_seed_path[200], tls_sigs_path[200], tls_roots_cache[200];
unsigned long (*tls_file_stamp)(const char *path);
long (*tls_read_at)(const char *path, long off, void *buf, long n);
unsigned long (*tls_file_info)(const char *path, long *size);
void (*tls_note)(const char *msg);
void (*tls_log)(const char *msg);
int (*tls_rsa_accel)(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		     const unsigned char *e, size_t elen);
int tls_load_anchors(const char *path, char *err, int errlen) { return 1; }
int tls_anchor_count(void) { return 1; }
void tls_free_anchors(void) { }
void tls_forget_sessions(void) { }
int tls_ec_fast(void) { return 0; }
static int a_tls;
TLS *tls_start(int sock, const char *host, int flags, int *retry, char *err, int errlen)
{
	starts++;
	last_flags = flags;
	*retry = 0;
	if ((flags & TLS_RSA_ONLY) && (flags & TLS_CHACHA_ONLY)) {
		*retry = TLS_RETRY_ANY_CIPHER;	/* refused before any certificate */
		str_copy(err, "alert 40", errlen);
		return 0;
	}
	if (flags & TLS_RSA_ONLY) {
		*retry = TLS_RETRY_ECDSA;
		str_copy(err, "alert 40", errlen);
		return 0;
	}
	return (TLS *)&a_tls;
}
long tls_read(TLS *t, void *buf, long max) { return -1; }
long tls_write(TLS *t, const void *buf, long n) { return n; }
void tls_close(TLS *t) { }
const char *tls_describe(TLS *t) { return "TLS 1.2, ECDHE-ECDSA"; }

/* conn.c's way: a refused try sets conn_tls_retry, the program tries again */
static int connect_once(const char *host)
{
	CONN c;
	char err[160];
	int tries = 0;
	do {
		memset(&c, 0, sizeof(c));
		c.name = "HTTP";
		tries++;
		if (conn_starttls(&c, host, err, sizeof(err)))
			return tries;
	} while (conn_tls_retry && tries < 5);
	return -1;
}

int main(int argc, char **argv)
{
	const char *path = "build/tlshosts.dat";
	int run = argc > 1 ? atoi(argv[1]) : 1;
	str_copy(conn_hosts_path, path, sizeof(conn_hosts_path));
	if (run == 1) {
		unlink(path);
		printf("== the first session: no file\n");
		check(connect_once("ecdsa.example") == 3, "three hellos: RSA with ChaCha20, RSA with every cipher, then ECDSA");
		check(connect_once("ecdsa.example") == 1, "the session's next connection: one");
		check(access(path, F_OK) == 0, "the file written");
	} else if (run == 2) {
		printf("== the next session: the file from the first\n");
		check(connect_once("ecdsa.example") == 1, "one hello");
		check(!(last_flags & TLS_RSA_ONLY) && !(last_flags & TLS_CHACHA_ONLY), "and it offered ECDSA and every cipher");
		check(connect_once("other.example") == 3, "another host still learns its way");
	} else if (run == 3) {
		printf("== no conn_hosts_path: tlshosts.dat beside SIGS.DAT\n");
		conn_hosts_path[0] = 0;
		str_copy(tls_sigs_path, "build/sigs.dat", sizeof(tls_sigs_path));
		check(connect_once("ecdsa.example") == 1, "one hello, from the first session's file");
	} else {
		printf("== neither path: nothing kept\n");
		conn_hosts_path[0] = 0;
		check(connect_once("ecdsa.example") == 3, "three hellos, as before the change");
	}
	return fails != 0;
}
