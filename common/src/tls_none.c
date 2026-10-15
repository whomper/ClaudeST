/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tls_none.c - the ST edition (NAMEST.PRG/.ACC) has no TLS of its own: an
 * ST is too slow for it, and leaving out BearSSL's 150 KB leaves room for
 * STinG and the program's data on a 1 MB machine. It reaches secure
 * servers through the Raspberry Pi gateway.
 *
 * The app names its Falcon edition in TLS_FULL_EDITION (e.g. -D
 * TLS_FULL_EDITION=\"ECAL.ACC\"), for the message below.
 */
#include <stddef.h>
#include "tls.h"
#include "util.h"

#ifndef TLS_FULL_EDITION
#define TLS_FULL_EDITION "the Falcon edition"
#endif

const int tls_available = 0;
int tls_tz_minutes;
char tls_seed_path[200];
char tls_roots_cache[200];
void (*tls_note)(const char *msg);
int (*tls_rsa_accel)(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		     const unsigned char *e, size_t elen);

static const char why[] = "this is the ST edition: secure connections go through the Raspberry Pi gateway "
			  "(Falcon mode is in " TLS_FULL_EDITION ")";

int tls_load_anchors(const char *path, char *err, int errlen)
{
	(void)path;
	str_copy(err, why, errlen);
	return 0;
}

int tls_anchor_count(void) { return 0; }
void tls_free_anchors(void) { }

void tls_forget_sessions(void) { }

TLS *tls_start(int sock, const char *host, int flags, int *retry, char *err, int errlen)
{
	(void)sock;
	(void)host;
	(void)flags;
	if (retry)
		*retry = 0;
	str_copy(err, why, errlen);
	return 0;
}

long tls_read(TLS *t, void *buf, long max) { (void)t; (void)buf; (void)max; return -1; }
long tls_write(TLS *t, const void *buf, long n) { (void)t; (void)buf; (void)n; return -1; }
void tls_close(TLS *t) { (void)t; }
const char *tls_describe(TLS *t) { (void)t; return ""; }
