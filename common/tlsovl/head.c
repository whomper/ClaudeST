/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * head.c - TLS.OVL's side of src/tlsovl.h: the TLS_OVL table at the
 * start of its text (ovl.ld), and the program's memory, files, network,
 * clock and log, reached through the TLS_HOST table it was given. The
 * rest of TLS.OVL is the family's own code as the programs had it:
 * src/tls.c, BearSSL, the DSP's ui/dsp.c, ui/dsp_ec.c, src/ecdsp.c,
 * atari/dsprsa.c and atari/dspec.c, and libc.c's strings and vsnprintf.
 */
#include <stddef.h>
#include <stdarg.h>
#include "plat.h"
#include "tls.h"
#include "tlsovl.h"

#ifndef TLSOVL_VERSION
#define TLSOVL_VERSION "0.0"
#endif

void falcon_dsp_use(int use);		/* ui/dsp.c */
const char *falcon_dsp_state(void);
void falcon_dsp_ec_use(int use);	/* ui/dsp_ec.c */
const char *falcon_dsp_ec_state(void);

extern int mem_unaligned_ok;		/* libc.c */

static const TLS_HOST *H;

void (*pf_idle)(void);

static const TLS_VARS vars = {
	&tls_tz_minutes, tls_seed_path, tls_sigs_path, tls_roots_cache,
	&tls_file_stamp, &tls_read_at, &tls_file_info, &tls_note, &tls_log, &pf_idle
};

/* the program's table as far as this overlay uses it: TLS_HOST's first
   version, every entry */
#define HOST_NEEDS ((long)sizeof(TLS_HOST))

static int init(const TLS_HOST *h)
{
	if (!h || h->size < HOST_NEEDS)
		return 0;
	H = h;
	mem_unaligned_ok = 1;		/* a 68020 or later: the program checked */
	return 1;
}

__attribute__((section(".tlsovl"), used))
const TLS_OVL tlsovl = {
	TLSOVL_MAGIC, TLSOVL_MAJOR, sizeof(TLS_OVL), TLSOVL_VERSION,
	init, &vars,
	tls_forget_sessions, tls_load_anchors, tls_anchor_count, tls_free_anchors,
	tls_start, tls_read, tls_write, tls_close, tls_describe, tls_ec_fast,
	falcon_dsp_use, falcon_dsp_state, falcon_dsp_ec_use, falcon_dsp_ec_state
};

/* ---- the program's, through TLS_HOST ---- */

void *malloc(size_t n) { return H->malloc(n); }
void *calloc(size_t n, size_t m) { return H->calloc(n, m); }
void *realloc(void *p, size_t n) { return H->realloc(p, n); }
void free(void *p) { H->free(p); }

long net_read(int h, void *buf, long max) { return H->net_read(h, buf, max); }
long net_write(int h, const void *buf, long n) { return H->net_write(h, buf, n); }

int pf_open(const char *path, int mode) { return H->pf_open(path, mode); }
long pf_read(int h, void *buf, long n) { return H->pf_read(h, buf, n); }
long pf_write(int h, const void *buf, long n) { return H->pf_write(h, buf, n); }
long pf_seek(int h, long off) { return H->pf_seek(h, off); }
void pf_close(int h) { H->pf_close(h); }
long pf_size(const char *path) { return H->pf_size(path); }
char *pf_load(const char *path, long *len) { return H->pf_load(path, len); }
int pf_save(const char *path, const void *data, long len) { return H->pf_save(path, data, len); }
int pf_remove(const char *path) { return H->pf_remove(path); }
void pf_entropy(unsigned char *buf, int n) { H->pf_entropy(buf, n); }
unsigned long pf_ms(void) { return H->pf_ms(); }
void pf_now(PFTIME *t) { H->pf_now(t); }

void conn_log(const char *who, const char *dir, const char *text, long n)
{
	H->conn_log(who, dir, text, n);
}

long tos_supexec(void *f) { return H->supexec(f); }
long get_cookie(long id) { return H->get_cookie(id); }
