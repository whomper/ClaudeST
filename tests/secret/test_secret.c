/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Passwords in NAME.INF (src/secret.c) on the build machine: encrypted
 * with the key in NAME.KEY, made the first time; the same key read again
 * the next time; another key can't read them; and with no key file to be
 * had, the password goes in as it is and secret_warn says so, once (the
 * AERA guidelines, 7.2).
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "plat.h"
#include "secret.h"

static int failed, checks, warnings;
static char dir[] = "/tmp/secretXXXXXX", warned_of[300];

int pf_open(const char *path, int mode)
{
	return open(path, mode == PF_WRITE ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0600);
}
long pf_read(int h, void *buf, long n) { return (long)read(h, buf, n); }
long pf_write(int h, const void *buf, long n) { return (long)write(h, buf, n); }
void pf_close(int h) { close(h); }

void pf_entropy(void *buf, int n)
{
	static unsigned long x = 0x2545F491UL;
	unsigned char *b = buf;
	int i;
	for (i = 0; i < n; i++) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		b[i] = (unsigned char)(x >> 24);
	}
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static void warn(const char *keyfile)
{
	warnings++;
	snprintf(warned_of, sizeof(warned_of), "%s", keyfile);
}

int main(void)
{
	char key[300], key2[300], enc[300], enc2[300], dec[200], longpw[200];
	if (!mkdtemp(dir))
		return 2;
	snprintf(key, sizeof(key), "%s/ECAL.KEY", dir);
	snprintf(key2, sizeof(key2), "%s/OTHER.KEY", dir);
	secret_warn = warn;

	check(secret_init(key) == 1, "a key made");
	check(access(key, R_OK) == 0, "NAME.KEY written");
	check(secret_encode("hunter2", enc, sizeof(enc)) == 1, "encrypted");
	check(!strncmp(enc, SECRET_TAG, strlen(SECRET_TAG)) && !strstr(enc, "hunter2"), "not in plain text");
	check(secret_decode(enc, dec, sizeof(dec)) && !strcmp(dec, "hunter2"), "read back");
	check(secret_encode("hunter2", enc2, sizeof(enc2)) == 1 && strcmp(enc, enc2), "a fresh nonce each time");
	check(secret_encode("", enc2, sizeof(enc2)) == 1 && !enc2[0], "an empty one stays empty");
	check(secret_decode("plain old", dec, sizeof(dec)) && !strcmp(dec, "plain old"), "an older file's plain value");

	check(secret_init(key) == 1, "the key read again");
	check(secret_decode(enc, dec, sizeof(dec)) && !strcmp(dec, "hunter2"), "read back with the key read again");
	check(secret_init(key2) == 1, "another key");
	check(!secret_decode(enc, dec, sizeof(dec)), "another key can't read it");
	check(warnings == 0, "no warning while there is a key");

	/* a key file that can't be made (its folder isn't there) */
	snprintf(key2, sizeof(key2), "%s/NOFOLDER/EMAIL.KEY", dir);
	check(secret_init(key2) == 0, "no key to be had");
	check(secret_encode("hunter2", enc, sizeof(enc)) == 0 && !strcmp(enc, "hunter2"), "as it is, and said so");
	check(warnings == 1 && !strcmp(warned_of, key2), "secret_warn told, with the key file");
	check(secret_encode("swordfish", enc, sizeof(enc)) == 0 && warnings == 1, "told once a run");
	check(secret_encode("", enc, sizeof(enc)) == 1, "an empty one isn't a password");

	/* with a key: too long, or too little room, goes in as it is too */
	check(secret_init(key) == 1, "the first key again");
	memset(longpw, 'p', 150);
	longpw[150] = 0;
	check(secret_encode(longpw, enc, sizeof(enc)) == 0 && !strcmp(enc, longpw), "150 characters: as it is");
	check(secret_encode("hunter2", enc, 20) == 0, "too little room: as it is");

	unlink(key);
	snprintf(key2, sizeof(key2), "%s/OTHER.KEY", dir);
	unlink(key2);
	rmdir(dir);
	printf("secret: %d checks, %d failed\n", checks, failed);
	return failed != 0;
}
