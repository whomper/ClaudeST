/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tlsovl.h - TLS.OVL, the family's Direct connection in a file of its
 * own: tls.c, BearSSL and the Falcon's DSP code, built for the 68030
 * (common/tlsovl). Every program carries the same file next to it, as
 * it carries CACERT.PEM; an ST never loads it, a TT or a Falcon when a
 * Direct connection is first made (src/tls_ovl.c, the program's side).
 *
 * The program and TLS.OVL meet through two tables: what the program
 * gives (TLS_HOST) and what TLS.OVL offers (TLS_OVL, at the start of
 * its text). Each starts with its size, and both only ever grow at the
 * end, so an older TLS.OVL works with a newer program and the other way
 * round: whoever reads a table looks at its size before an entry past
 * the first version's. A change that can't be made by adding raises
 * TLSOVL_MAJOR, and a program refuses a TLS.OVL of another major.
 */
#ifndef TLSOVL_H
#define TLSOVL_H

#include <stddef.h>
#include <stdarg.h>

#define TLSOVL_MAGIC 0x544C534FUL	/* "TLSO" */
#define TLSOVL_MAJOR 1

struct TLS;
struct PFTIME_;

/* what the program lends TLS.OVL: its memory, files, network, clock and
   log. The overlay has no heap and no files of its own */
typedef struct {
	long size;			/* sizeof(TLS_HOST) as the program knows it */
	void *(*malloc)(size_t n);
	void *(*calloc)(size_t n, size_t m);
	void *(*realloc)(void *p, size_t n);
	void (*free)(void *p);
	long (*net_read)(int h, void *buf, long max);
	long (*net_write)(int h, const void *buf, long n);
	int (*pf_open)(const char *path, int mode);
	long (*pf_read)(int h, void *buf, long n);
	long (*pf_write)(int h, const void *buf, long n);
	long (*pf_seek)(int h, long off);
	void (*pf_close)(int h);
	long (*pf_size)(const char *path);
	char *(*pf_load)(const char *path, long *len);
	int (*pf_save)(const char *path, const void *data, long len);
	int (*pf_remove)(const char *path);
	void (*pf_entropy)(unsigned char *buf, int n);
	unsigned long (*pf_ms)(void);
	void (*pf_now)(void *t);	/* a PFTIME */
	void (*conn_log)(const char *who, const char *dir, const char *text, long n);
	long (*supexec)(void *f);	/* tos.h's tos_supexec */
	long (*get_cookie)(long id);
} TLS_HOST;

/* the settings tls.h's variables hold, where TLS.OVL keeps its own copies:
   the program's side copies them over before each call, so a hook set or
   changed by the program is the one the overlay uses */
typedef struct {
	int *tz_minutes;
	char *seed_path, *sigs_path, *roots_cache;	/* 200 bytes each */
	unsigned long (**file_stamp)(const char *path);
	long (**read_at)(const char *path, long off, void *buf, long n);
	unsigned long (**file_info)(const char *path, long *size);
	void (**note)(const char *msg);
	void (**log)(const char *msg);
	void (**idle)(void);		/* pf_idle */
} TLS_VARS;

/* what TLS.OVL offers: tls.h's functions, and the DSP's */
typedef struct {
	unsigned long magic;		/* TLSOVL_MAGIC */
	short major;			/* TLSOVL_MAJOR */
	short size;			/* sizeof(TLS_OVL) as the overlay knows it */
	char version[8];		/* TLS.OVL's release, "1.0" */
	/* 1 when it can work with this program's TLS_HOST; first, once */
	int (*init)(const TLS_HOST *h);
	const TLS_VARS *vars;
	void (*forget_sessions)(void);
	int (*load_anchors)(const char *path, char *err, int errlen);
	int (*anchor_count)(void);
	void (*free_anchors)(void);
	struct TLS *(*start)(int sock, const char *host, int flags, int *retry, char *err, int errlen);
	long (*read)(struct TLS *t, void *buf, long max);
	long (*write)(struct TLS *t, const void *buf, long n);
	void (*close)(struct TLS *t);
	const char *(*describe)(struct TLS *t);
	int (*ec_fast)(void);
	/* the Falcon's DSP: use it (a Direct connection, the DSP ticked) or
	   not; what does the work now, "DSP" or "68030" */
	void (*dsp_rsa)(int use);
	const char *(*dsp_rsa_state)(void);
	void (*dsp_ec)(int use);
	const char *(*dsp_ec_state)(void);
} TLS_OVL;

/* the program's side (src/tls_ovl.c) */

/* where TLS.OVL is: beside CACERT.PEM, in the program's folder (an
   accessory's, not the root), set where CACERT.PEM's path is; "TLS.OVL"
   (the current folder) until then */
extern char tls_ovl_path[200];
/* load TLS.OVL now if it isn't yet: 1, or 0 with err filled in (missing,
   another major, this Atari has no 68020). tls_start and
   tls_load_anchors call it themselves; a program calls it first to say
   at once, when a Direct connection is ticked, what is wrong */
int tls_ovl_load(char *err, int errlen);
int tls_ovl_loaded(void);
/* TLS.OVL's version for the About box and the log ("1.0"), "" if not
   loaded */
const char *tls_ovl_version(void);
/* let it go (an accessory giving back its memory, store_release), only
   with no connection open: the next connection loads it again */
void tls_ovl_unload(void);
/* the DSP for RSA (ec 0) or the elliptic curves (ec 1), for
   ui/dsp_ovl.c: use it or not, kept until TLS.OVL is loaded; and what
   does the work, "DSP" or "68030" */
void tls_ovl_dsp(int ec, int use);
const char *tls_ovl_dsp_state(int ec);

#endif
