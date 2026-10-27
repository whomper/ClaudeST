/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_tlsfiles.c - the files src/tls.c opens and looks up (the
 * guidelines, 6.5), counted on the build machine:
 *
 *   roots DIR      CACERT.PEM decoded the first time, ROOTS.DAT made; then
 *                  found by its date (ROOTS.DAT read once, CACERT.PEM not
 *                  opened); then by its checksum when the date changed
 *                  (ROOTS.DAT read once, not twice); one look at
 *                  CACERT.PEM with tls_file_info; 32 KB pieces through
 *                  tls_read_at; the anchors the same each way
 *   seed DIR N     a handshake's seed: run N of a session (1: SEED.DAT is
 *                  made; later: read, then written over in place; no looks)
 *   retry DIR      when a ChaCha20-only hello is tried again with every
 *                  cipher: no answer at all, or an alert first, yes; a
 *                  close after the server's certificates (openssl
 *                  s_server, which took ChaCha20), no; and a whole
 *                  handshake's log names the curve and the server's key
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include "plat.h"
#include "tls.h"

static int failed;
static int opens, looks, reads, read_ats, idles;
static char watch[300];		/* the file counted; others pass */

static void check(int ok, const char *what)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	failed += !ok;
}

static int is_watched(const char *path)
{
	return watch[0] && !strcmp(path, watch);
}

int pf_open(const char *path, int mode)
{
	/* PF_UPDATE write-only, as the apps' plat opens it (TOS mode 1) */
	int fl = mode == PF_READ ? O_RDONLY : mode == PF_WRITE ? O_WRONLY | O_CREAT | O_TRUNC
		: mode == PF_APPEND ? O_WRONLY | O_CREAT | O_APPEND : O_WRONLY;
	if (is_watched(path))
		opens++;
	return open(path, fl, 0644);
}

long pf_read(int h, void *buf, long n)
{
	reads++;
	return read(h, buf, (size_t)n);
}

long pf_write(int h, const void *buf, long n) { return write(h, buf, (size_t)n); }
void pf_close(int h) { close(h); }
long pf_seek(int h, long off) { return lseek(h, off, SEEK_SET); }

long pf_size(const char *path)
{
	struct stat st;
	if (is_watched(path))
		looks++;
	return stat(path, &st) ? -1 : (long)st.st_size;
}

int pf_remove(const char *path) { return unlink(path); }

/* as common's futil.c does it: a look for the size, then the open */
char *pf_load(const char *path, long *len)
{
	long size = pf_size(path), got = 0, r;
	char *buf;
	int h;
	*len = 0;
	if (size < 0 || (h = pf_open(path, PF_READ)) < 0)
		return 0;
	buf = malloc(size + 1);
	while (buf && got < size && (r = pf_read(h, buf + got, size - got)) > 0)
		got += r;
	pf_close(h);
	if (buf)
		buf[got] = 0;
	*len = got;
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

static int real_now;		/* retry: the build machine's clock, for openssl's certificates */

void pf_now(PFTIME *t)
{
	t->year = 2026, t->mon = 10, t->day = 18, t->hour = 12, t->min = t->sec = 0;
	if (real_now) {
		time_t now = time(0);
		struct tm *g = gmtime(&now);
		t->year = g->tm_year + 1900, t->mon = g->tm_mon + 1, t->day = g->tm_mday;
		t->hour = g->tm_hour, t->min = g->tm_min, t->sec = g->tm_sec;
	}
}

/* a clock that moves 10 ms a look: tls.c's turns come by the time */
unsigned long pf_ms(void) { static unsigned long t; return t += 10; }

void pf_entropy(unsigned char *buf, int n)
{
	int i;
	for (i = 0; i < n; i++)
		buf[i] = (unsigned char)(i * 7 + 1);
}

/* the network: refused (the roots and seed runs); in retry, the hello
   goes out and then nothing comes, or an alert, or (h >= 0) a real
   server until our second flight, after its certificates */
enum { NET_REFUSE, NET_QUIET, NET_ALERT, NET_SOCKET };
static int net_mode, net_writes, net_drop = 2;	/* hung up at that write */
static long net_at;

long net_write(int h, const void *buf, long n)
{
	if (net_mode == NET_REFUSE)
		return -1;
	net_writes++;
	if (net_mode == NET_SOCKET)
		return (long)write(h, buf, (size_t)n);
	return n;
}

long net_read(int h, void *buf, long max)
{
	static const unsigned char alert[] = { 21, 3, 3, 0, 2, 2, 40 };	/* fatal handshake_failure */
	long r;
	if (net_mode == NET_ALERT && net_at < (long)sizeof(alert)) {
		r = (long)sizeof(alert) - net_at < max ? (long)sizeof(alert) - net_at : max;
		memcpy(buf, alert + net_at, (size_t)r);
		net_at += r;
		return r;
	}
	if (net_mode != NET_SOCKET || (net_drop && net_writes >= net_drop))
		return -1;		/* hung up */
	r = (long)read(h, buf, (size_t)max);
	return r > 0 ? r : -1;
}

static void idle(void) { idles++; }
void (*pf_idle)(void) = idle;

/* ---- the hooks an app sets ---- */
static unsigned long the_stamp;

static unsigned long stamp_of(const char *path)
{
	if (is_watched(path))
		looks++;
	return the_stamp;
}

static unsigned long info_of(const char *path, long *size)
{
	struct stat st;
	if (is_watched(path))
		looks++;
	*size = stat(path, &st) ? -1 : (long)st.st_size;
	return *size < 0 ? 0 : the_stamp;
}

static long read_at(const char *path, long off, void *buf, long n)
{
	int h = pf_open(path, PF_READ);
	long r;
	read_ats++;
	if (h < 0)
		return -1;
	lseek(h, off, SEEK_SET);
	r = pf_read(h, buf, n);
	pf_close(h);
	return r;
}

static int load(const char *pem, const char *what_file, char *err)
{
	strcpy(watch, what_file);
	opens = looks = reads = read_ats = idles = 0;
	tls_free_anchors();
	return tls_load_anchors(pem, err, 200);
}

static int roots(const char *dir)
{
	char pem[300], src[300], err[200];
	int n0, n;
	long pemsize;
	FILE *in, *out;
	int c;
	snprintf(src, sizeof(src), "%s/../../certs/CACERT.PEM", ".");
	snprintf(pem, sizeof(pem), "%s/CACERT.PEM", dir);
	snprintf(tls_roots_cache, sizeof(tls_roots_cache), "%s/ROOTS.DAT", dir);
	unlink(tls_roots_cache);
	in = fopen(src, "rb");
	out = fopen(pem, "wb");
	if (!in || !out) {
		printf("FAIL no %s\n", src);
		return 1;
	}
	while ((c = getc(in)) != EOF)
		putc(c, out);
	fclose(in);
	fclose(out);
	pemsize = pf_size(pem);

	printf("== the first time: CACERT.PEM decoded, ROOTS.DAT made\n");
	tls_file_stamp = stamp_of;
	the_stamp = 1000;
	n0 = load(pem, pem, err);
	check(n0 > 100, "the anchors decoded");
	check(opens == 2, "CACERT.PEM opened twice (its checksum, then the decoding)");
	check(reads <= 2 * ((pemsize + 32767) / 32768) + 2, "and read in 32 KB pieces");
	printf("  (%d idles, %ld bytes)\n", idles, pemsize);
	check(idles >= pemsize / 4096 - 1, "a turn at least every 4 KB decoded");
	check(pf_size(tls_roots_cache) > 0, "ROOTS.DAT made");

	printf("== the next session: found by CACERT.PEM's date\n");
	n = load(pem, pem, err);
	check(n == n0, "the same anchors");
	check(opens == 0, "CACERT.PEM not opened");
	check(looks == 2, "two looks at CACERT.PEM (tls_file_stamp, pf_size)");
	n = load(pem, tls_roots_cache, err);
	check(opens == 1, "ROOTS.DAT opened once");

	printf("== one look with tls_file_info\n");
	tls_file_info = info_of;
	n = load(pem, pem, err);
	check(n == n0 && opens == 0 && looks == 1, "one look at CACERT.PEM, not opened");

	printf("== CACERT.PEM's date changed, not its bytes: found by its checksum\n");
	the_stamp = 2000;
	n = load(pem, tls_roots_cache, err);
	check(n == n0, "the same anchors");
	check(opens == 2, "ROOTS.DAT opened twice: read once, then its date written in place");
	n = load(pem, pem, err);
	check(n == n0 && opens == 0, "and then found by the new date");

	printf("== an accessory: CACERT.PEM through tls_read_at\n");
	unlink(tls_roots_cache);
	tls_read_at = read_at;
	n = load(pem, pem, err);
	check(n == n0, "the same anchors");
	check(read_ats == 2 * ((pemsize + 32767) / 32768 + 1),
	      "a read_at per 32 KB (and the end), for the checksum and for the certificates");
	check(opens == read_ats, "CACERT.PEM opened only by read_at: none kept across a turn");
	tls_read_at = 0;
	tls_file_info = 0;
	tls_file_stamp = 0;
	tls_free_anchors();
	return 0;
}

static int seed(const char *dir, int run)
{
	char pem[300], err[200], before[32], after[32];
	int r = 0, i, at_load;
	FILE *f;
	snprintf(pem, sizeof(pem), "../../certs/CACERT.PEM");
	snprintf(tls_seed_path, sizeof(tls_seed_path), "%s/SEED.DAT", dir);
	if (run == 1)
		unlink(tls_seed_path);
	memset(before, 0, sizeof(before));
	if ((f = fopen(tls_seed_path, "rb")) != 0) {
		r = (int)fread(before, 1, sizeof(before), f);
		fclose(f);
	}
	/* the seed read and written with the root certificates, before
	   any connection (conn.c), none of it in the handshakes */
	strcpy(watch, tls_seed_path);
	opens = looks = 0;
	tls_load_anchors(pem, err, sizeof(err));
	at_load = opens;
	for (i = 0; i < 2; i++)
		tls_start(-1, "example.org", 0, 0, err, sizeof(err));
	printf("== seed, run %d\n", run);
	f = fopen(tls_seed_path, "rb");
	check(f && fread(after, 1, sizeof(after), f) == sizeof(after), "SEED.DAT holds 32 bytes");
	if (f)
		fclose(f);
	check(memcmp(before, after, sizeof(after)) != 0, "a new seed written");
	if (run == 1)
		check(r == 0 && looks == 0 && opens == at_load,
		      "there was none before; made with the roots, with no look at the directory");
	else
		check(at_load == 2 && opens == 2 && looks == 0,
		      "two opens (read, then written over) with the roots, none in two handshakes, no look");
	return 0;
}

static char logged[2000];

static void log_line(const char *msg)
{
	if (strlen(logged) + strlen(msg) + 2 < sizeof(logged))
		strcat(strcat(logged, msg), "\n");
}

static int connect_to(const char *port)
{
	struct sockaddr_in a;
	int s, i;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)atoi(port));
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	for (i = 0; i < 100; i++) {
		s = socket(AF_INET, SOCK_STREAM, 0);
		if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0)
			return s;
		close(s);
		usleep(50000);
	}
	return -1;
}

static int retry_of(int mode, int sock, char *err, int errlen)
{
	int retry = -1;
	net_mode = mode, net_writes = 0, net_at = 0;
	tls_start(sock, "localhost", TLS_CHACHA_ONLY, &retry, err, errlen);
	net_mode = NET_REFUSE;
	return retry;
}

static int retry_test(const char *dir)
{
	char key[300], crt[300], ca[300], cmd[1200], port[16], err[200];
	TLS *t;
	pid_t srv;
	int s = -1, r, i;
	snprintf(tls_seed_path, sizeof(tls_seed_path), "%s/SEED.DAT", dir);
	tls_load_anchors("../../certs/CACERT.PEM", err, sizeof(err));
	printf("== retry\n");
	r = retry_of(NET_QUIET, -1, err, sizeof(err));
	check(r == TLS_RETRY_ANY_CIPHER, "no answer to the hello: again with every cipher");
	r = retry_of(NET_ALERT, -1, err, sizeof(err));
	check(r == TLS_RETRY_ANY_CIPHER, "a handshake_failure alert first: again with every cipher");

	/* a test authority (RSA 3072) and the server's certificate it signed */
	snprintf(key, sizeof(key), "%s/key.pem", dir);
	snprintf(crt, sizeof(crt), "%s/crt.pem", dir);
	snprintf(ca, sizeof(ca), "%s/ca.pem", dir);
	snprintf(cmd, sizeof(cmd),
		 "cd %s && openssl req -x509 -newkey rsa:3072 -nodes -keyout cakey.pem -out ca.pem -days 2 "
		 "-subj /CN=Test-CA >/dev/null 2>&1 && openssl req -newkey rsa:2048 -nodes -keyout key.pem "
		 "-out req.pem -subj /CN=localhost >/dev/null 2>&1 && printf 'subjectAltName=DNS:localhost\\n' "
		 "> ext.cnf && openssl x509 -req -in req.pem -CA ca.pem -CAkey cakey.pem -CAcreateserial "
		 "-days 2 -sha256 -extfile ext.cnf -out crt.pem >/dev/null 2>&1", dir);
	if (system(cmd) != 0)
		return check(0, "openssl makes a certificate"), 0;
	snprintf(port, sizeof(port), "%d", 20000 + (int)(getpid() % 20000));
	fflush(stdout);
	srv = fork();
	if (srv == 0) {
		char acc[40];
		snprintf(acc, sizeof(acc), "127.0.0.1:%s", port);
		if (!freopen("/dev/null", "w", stdout) || !freopen("/dev/null", "w", stderr))
			_exit(127);
		execlp("openssl", "openssl", "s_server", "-accept", acc, "-cert", crt, "-key", key, "-tls1_2",
		       "-cipher", "ECDHE-RSA-CHACHA20-POLY1305", "-groups", "X25519", "-naccept", "3", "-quiet", (char *)0);
		_exit(127);
	}
	s = connect_to(port);
	check(s >= 0, "openssl s_server answers");
	if (s >= 0) {
		r = retry_of(NET_SOCKET, s, err, sizeof(err));
		check(net_writes >= 2, "the server took ChaCha20 and sent its certificates");
		check(r == 0, "closed after the certificates: no new try");
		close(s);
	}
	/* whole handshakes, the test authority a root: the log names the
	   curve and the server's key; the authority's signature on the
	   server's certificate is checked once, then known (SIGS.DAT) */
	tls_free_anchors();
	tls_load_anchors(ca, err, sizeof(err));
	snprintf(tls_sigs_path, sizeof(tls_sigs_path), "%s/SIGS.DAT", dir);
	unlink(tls_sigs_path);
	real_now = 1;
	for (i = 0; i < 2; i++) {
		s = connect_to(port);
		if (s < 0)
			break;
		logged[0] = 0;
		tls_forget_sessions();	/* a whole handshake each time */
		tls_log = log_line;
		net_mode = NET_SOCKET, net_writes = 0, net_drop = 0;
		t = tls_start(s, "localhost", 0, 0, err, sizeof(err));
		net_drop = 2;
		tls_log = 0;
		if (t)
			tls_close(t);
		net_mode = NET_REFUSE;
		close(s);
		if (i == 0) {
			check(t != 0, "a whole handshake, the chain up to the test authority");
			check(strstr(logged, "the key exchange: x25519, ") &&
			      strstr(logged, "; the server's key: RSA 2048 bits, its signature "),
			      "the log: the key exchange's curve, the server's key and their times");
			check(strstr(logged, "; 0 of 1 RSA signatures checked before") != 0,
			      "the authority's RSA signature checked");
		} else {
			check(t != 0 && strstr(logged, "; 1 of 1 RSA signatures checked before") != 0,
			      "the next time: known, not checked again");
		}
		if (!t || !strstr(logged, "RSA signatures checked before"))
			printf("%s(%s)\n", logged, err);
	}
	real_now = 0;
	{
		struct stat st;
		check(stat(tls_sigs_path, &st) == 0 && st.st_size == 8 + 64 * 32, "SIGS.DAT as long as before");
	}
	kill(srv, SIGTERM);
	waitpid(srv, 0, 0);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[1], "roots"))
		roots(argv[2]);
	else if (argc >= 4 && !strcmp(argv[1], "seed"))
		seed(argv[2], atoi(argv[3]));
	else if (argc >= 3 && !strcmp(argv[1], "retry"))
		retry_test(argv[2]);
	else
		return printf("usage: test_tlsfiles roots DIR | seed DIR N | retry DIR\n"), 2;
	return failed != 0;
}
