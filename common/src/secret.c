/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * secret.c - passwords in the settings file (NAME.INF), encrypted (see secret.h).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "secret.h"
#include "plat.h"

static unsigned long key[4];
/* the text being encrypted or read: a password's on the stack, a longer
   one (a cookie, up to SECRET_MAX) from the heap, so that a 1 MB ST's
   programs carry no 4 KB of BSS for it; wiped after use either way */
#define SMALL 128
static unsigned char *work(unsigned char *small, int n)
{
	return n <= SMALL ? small : (unsigned char *)malloc(6 + n);
}

static void done(unsigned char *b, unsigned char *small, int n)
{
	memset(b, 0, 6 + (n <= SMALL ? SMALL : n));
	if (b != small)
		free(b);
}
static int have_key, warned;
static char keyname[128];

void (*secret_warn)(const char *keyfile);

/* XTEA, 32 rounds, on one 64-bit block */
static void xtea(unsigned long v[2])
{
	unsigned long v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9UL;
	int i;
	for (i = 0; i < 32; i++) {
		v0 = (v0 + ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]))) & 0xffffffffUL;
		sum = (sum + delta) & 0xffffffffUL;
		v1 = (v1 + ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]))) & 0xffffffffUL;
	}
	v[0] = v0;
	v[1] = v1;
}

/* the text XORed with XTEA(nonce, counter): the same both ways */
static void crypt(const unsigned char nonce[4], unsigned char *buf, int n)
{
	unsigned long block[2], counter = 0;
	int i;
	for (i = 0; i < n; i++) {
		if (i % 8 == 0) {
			block[0] = (unsigned long)nonce[0] << 24 | (unsigned long)nonce[1] << 16 |
				   (unsigned long)nonce[2] << 8 | nonce[3];
			block[1] = counter++;
			xtea(block);
		}
		buf[i] ^= (unsigned char)(block[(i % 8) / 4] >> (8 * (3 - i % 4)));
	}
}

int secret_init(const char *keyfile)
{
	unsigned char raw[16];
	int h, i;
	have_key = 0;
	snprintf(keyname, sizeof(keyname), "%s", keyfile);
	h = pf_open(keyfile, PF_READ);
	if (h >= 0) {
		have_key = pf_read(h, raw, 16) == 16;
		pf_close(h);
	}
	if (!have_key) {
		pf_entropy(raw, 16);
		h = pf_open(keyfile, PF_WRITE);
		if (h < 0)
			return 0;
		have_key = pf_write(h, raw, 16) == 16;
		pf_close(h);
	}
	for (i = 0; i < 4; i++)
		key[i] = (unsigned long)raw[4 * i] << 24 | (unsigned long)raw[4 * i + 1] << 16 |
			 (unsigned long)raw[4 * i + 2] << 8 | raw[4 * i + 3];
	return have_key;
}

static const char hexd[] = "0123456789ABCDEF";

int secret_encode(const char *plain, char *out, int size)
{
	unsigned char small[6 + SMALL], *buf;
	int n = (int)strlen(plain), i, o;
	if (!*plain) {
		snprintf(out, size, "%s", "");
		return 1;
	}
	if (!have_key || n > SECRET_MAX || size < (int)strlen(SECRET_TAG) + 2 * (6 + n) + 1) {
		snprintf(out, size, "%s", plain);	/* as it is, and said so */
		if (!warned && secret_warn) {
			warned = 1;
			secret_warn(keyname);
		}
		return 0;
	}
	if (!(buf = work(small, n))) {
		snprintf(out, size, "%s", plain);
		return 0;
	}
	pf_entropy(buf, 4);			/* the nonce */
	buf[4] = 'P';				/* a check: wrong key or not ours */
	buf[5] = 'W';
	memcpy(buf + 6, plain, n);
	crypt(buf, buf + 4, n + 2);
	o = (int)strlen(SECRET_TAG);
	memcpy(out, SECRET_TAG, o);
	for (i = 0; i < n + 6; i++) {
		out[o++] = hexd[buf[i] >> 4];
		out[o++] = hexd[buf[i] & 15];
	}
	out[o] = 0;
	done(buf, small, n);
	return 1;
}

static int hexv(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

int secret_decode(const char *in, char *out, int size)
{
	unsigned char small[6 + SMALL], *buf;
	int t = (int)strlen(SECRET_TAG), len, n, i, ok;
	if (strncmp(in, SECRET_TAG, t)) {
		snprintf(out, size, "%s", in);		/* written by an older version */
		return 1;
	}
	in += t;
	len = (int)strlen(in);
	n = len / 2;
	out[0] = 0;
	if (!have_key || len % 2 || n < 6 || n > 6 + SECRET_MAX || !(buf = work(small, n - 6)))
		return 0;
	for (i = 0; i < n; i++) {
		int a = hexv(in[2 * i]), b = hexv(in[2 * i + 1]);
		if (a < 0 || b < 0) {
			done(buf, small, n - 6);
			return 0;
		}
		buf[i] = (unsigned char)(a << 4 | b);
	}
	crypt(buf, buf + 4, n - 4);
	ok = buf[4] == 'P' && buf[5] == 'W' && n - 6 < size;
	if (ok) {
		memcpy(out, buf + 6, n - 6);
		out[n - 6] = 0;
	}
	done(buf, small, n - 6);
	return ok;
}
