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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
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

void pf_now(PFTIME *t)
{
	t->year = 2026, t->mon = 10, t->day = 18, t->hour = 12, t->min = t->sec = 0;
}

unsigned long pf_ms(void) { return 0; }

void pf_entropy(unsigned char *buf, int n)
{
	int i;
	for (i = 0; i < n; i++)
		buf[i] = (unsigned char)(i * 7 + 1);
}

long net_write(int h, const void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long net_read(int h, void *buf, long max) { (void)h; (void)buf; (void)max; return -1; }

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
	check(idles >= pemsize / 4096 - 1, "pf_idle at every 4 KB decoded");
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
	check(read_ats == (pemsize + 32767) / 32768 + 1, "a read_at per 32 KB (and the end)");
	check(opens == 1 + read_ats, "CACERT.PEM opened for its checksum and per read_at");
	tls_read_at = 0;
	tls_file_info = 0;
	tls_file_stamp = 0;
	tls_free_anchors();
	return 0;
}

static int seed(const char *dir, int run)
{
	char pem[300], err[200], before[32], after[32];
	int r = 0, i;
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
	tls_load_anchors(pem, err, sizeof(err));
	strcpy(watch, tls_seed_path);
	opens = looks = 0;
	for (i = 0; i < 2; i++)
		tls_start(-1, "example.org", 0, 0, err, sizeof(err));
	printf("== seed, run %d\n", run);
	f = fopen(tls_seed_path, "rb");
	check(f && fread(after, 1, sizeof(after), f) == sizeof(after), "SEED.DAT holds 32 bytes");
	if (f)
		fclose(f);
	check(memcmp(before, after, sizeof(after)) != 0, "a new seed written");
	if (run == 1)
		check(r == 0 && looks == 0, "there was none before; made with no look at the directory");
	else
		check(opens == 2 && looks == 0, "two opens (read, then written over), no look, for two handshakes");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[1], "roots"))
		roots(argv[2]);
	else if (argc >= 4 && !strcmp(argv[1], "seed"))
		seed(argv[2], atoi(argv[3]));
	else
		return printf("usage: test_tlsfiles roots DIR | seed DIR N\n"), 2;
	return failed != 0;
}
