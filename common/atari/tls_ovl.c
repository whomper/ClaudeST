/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tls_ovl.c - the program's side of TLS.OVL (src/tlsovl.h): tls.h's
 * functions, each handed on to the overlay, which is loaded the first
 * time a Direct connection needs it (tls_start, tls_load_anchors) or
 * when the program asks (tls_ovl_load). It takes the place of tls.c and
 * the DSP's files in a program: one program for every Atari, the ST
 * keeping the 140 KB for its data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "plat.h"
#include "util.h"
#include "conn.h"
#include "tls.h"
#include "tlsovl.h"
#include "ovl.h"

/* the program can make a Direct connection: with TLS.OVL next to it */
const int tls_available = 1;
int tls_tz_minutes;
char tls_seed_path[200];
char tls_sigs_path[200];
char tls_roots_cache[200];
unsigned long (*tls_file_stamp)(const char *path);
long (*tls_read_at)(const char *path, long off, void *buf, long n);
unsigned long (*tls_file_info)(const char *path, long *size);
void (*tls_note)(const char *msg);
void (*tls_log)(const char *msg);
/* TLS.OVL's own: it sets it from its DSP code */
int (*tls_rsa_accel)(unsigned char *x, size_t xlen, const unsigned char *n, size_t nlen,
		     const unsigned char *e, size_t elen);

char tls_ovl_path[200] = "TLS.OVL";

static const TLS_OVL *ov;
static void *image;
static int loading;			/* pf_idle runs during the load: not twice */
static int want[2];			/* the DSP: RSA, the curves */

static void now(void *t) { pf_now((PFTIME *)t); }

static const TLS_HOST host = {
	sizeof(TLS_HOST),
	malloc, calloc, realloc, free,
	net_read, net_write,
	pf_open, pf_read, pf_write, pf_seek, pf_close, pf_size, pf_load, pf_save, pf_remove,
	pf_entropy, pf_ms, now,
	conn_log,
	tos_supexec, get_cookie
};

/* the code just written into memory is what the CPU runs next: clear its
   caches (a 68030's 256-byte instruction cache, a 68040's or 68060's
   copyback data cache, and a 68060's branch cache, which CPUSHA leaves
   alone: EWeb's js_ovl.c), in supervisor mode */
static long cpu;
static long flush(void)
{
	if (cpu >= 40)
		__asm__ volatile(".short 0xf4f8" ::: "memory");		/* cpusha bc */
	if (cpu >= 60)
		__asm__ volatile(".short 0x4e7a,0x0002\n\t"		/* movec cacr,d0 */
				 "ori.l #0x00400000,%%d0\n\t"		/* CABC: clear the branch cache */
				 ".short 0x4e7b,0x0002"			/* movec d0,cacr */
				 ::: "d0", "memory");
	else if (cpu >= 20 && cpu < 40)
		__asm__ volatile(".short 0x4e7a,0x0002\n\t"		/* movec cacr,d0 */
				 "ori.w #0x0808,%%d0\n\t"		/* CI, CD: clear both */
				 ".short 0x4e7b,0x0002"			/* movec d0,cacr */
				 ::: "d0", "memory");
	return 0;
}

int tls_ovl_loaded(void)
{
	return ov != 0;
}

const char *tls_ovl_version(void)
{
	return ov ? ov->version : "";
}

int tls_ovl_load(char *err, int errlen)
{
	const TLS_OVL *o;
	long taken;
	unsigned long t0;
	char msg[80];
	if (ov)
		return 1;
	if (loading) {
		str_copy(err, "TLS.OVL is being loaded", errlen);
		return 0;
	}
	cpu = get_cookie(0x5F435055L);		/* _CPU: 0, 10, 20, 30, 40, 60 */
	if (cpu < 20) {
		str_copy(err, "a Direct connection needs a 68020 or better (a TT, a Falcon): "
			 "use the Raspberry Pi gateway", errlen);
		return 0;
	}
	t0 = pf_ms();
	loading = 1;
	image = ovl_load(tls_ovl_path, &taken, err, errlen);
	loading = 0;
	if (!image) {
		if (pf_size(tls_ovl_path) < 0)
			str_copy(err, "TLS.OVL is missing: copy it from the program's release, "
				 "beside CACERT.PEM", errlen);
		return 0;
	}
	tos_supexec((void *)flush);
	o = image;
	if (o->magic != TLSOVL_MAGIC || o->size < (short)sizeof(TLS_OVL)) {
		free(image);
		image = 0;
		str_copy(err, "TLS.OVL is damaged: copy it again from the release", errlen);
		return 0;
	}
	if (o->major != TLSOVL_MAJOR || !o->init(&host)) {
		snprintf(err, errlen, "TLS.OVL %.7s doesn't suit this program: take the one from its release",
			 o->version);
		free(image);
		image = 0;
		return 0;
	}
	ov = o;
	snprintf(msg, sizeof(msg), "TLS.OVL %.7s loaded, %ld KB, %lu ms", ov->version,
		 (taken + 1023) / 1024, pf_ms() - t0);
	conn_log("TLS", " -- ", msg, strlen(msg));
	ov->dsp_rsa(want[0]);
	ov->dsp_ec(want[1]);
	return 1;
}

void tls_ovl_unload(void)
{
	if (!ov)
		return;
	ov->free_anchors();
	ov->forget_sessions();
	ov = 0;
	free(image);
	image = 0;
}

void tls_ovl_dsp(int ec, int use)
{
	want[ec != 0] = use;
	if (ov)
		(ec ? ov->dsp_ec : ov->dsp_rsa)(use);
}

const char *tls_ovl_dsp_state(int ec)
{
	if (!ov)
		return "68030";		/* the DSP is tried when TLS.OVL is loaded */
	return ec ? ov->dsp_ec_state() : ov->dsp_rsa_state();
}

/* the program's settings into the overlay's copies: the hooks every time,
   the paths when they matter (a handshake, the root certificates) */
static void sync(int paths)
{
	const TLS_VARS *v = ov->vars;
	*v->tz_minutes = tls_tz_minutes;
	*v->file_stamp = tls_file_stamp;
	*v->read_at = tls_read_at;
	*v->file_info = tls_file_info;
	*v->note = tls_note;
	*v->log = tls_log;
	*v->idle = pf_idle;
	if (paths) {
		str_copy(v->seed_path, tls_seed_path, sizeof(tls_seed_path));
		str_copy(v->sigs_path, tls_sigs_path, sizeof(tls_sigs_path));
		str_copy(v->roots_cache, tls_roots_cache, sizeof(tls_roots_cache));
	}
}

int tls_load_anchors(const char *path, char *err, int errlen)
{
	if (!tls_ovl_load(err, errlen))
		return 0;
	sync(1);
	return ov->load_anchors(path, err, errlen);
}

int tls_anchor_count(void)
{
	return ov ? ov->anchor_count() : 0;
}

void tls_free_anchors(void)
{
	if (ov) {
		sync(1);
		ov->free_anchors();
	}
}

void tls_forget_sessions(void)
{
	if (ov) {
		sync(1);
		ov->forget_sessions();
	}
}

int tls_ec_fast(void)
{
	return ov && ov->ec_fast();
}

TLS *tls_start(int sock, const char *host_name, int flags, int *retry, char *err, int errlen)
{
	if (retry)
		*retry = 0;
	if (!tls_ovl_load(err, errlen))
		return 0;
	sync(1);
	return ov->start(sock, host_name, flags, retry, err, errlen);
}

long tls_read(TLS *t, void *buf, long max)
{
	if (!ov)
		return -1;
	sync(0);
	return ov->read(t, buf, max);
}

long tls_write(TLS *t, const void *buf, long n)
{
	if (!ov)
		return -1;
	sync(0);
	return ov->write(t, buf, n);
}

void tls_close(TLS *t)
{
	if (ov && t) {
		sync(0);
		ov->close(t);
	}
}

const char *tls_describe(TLS *t)
{
	return ov && t ? ov->describe(t) : "";
}
