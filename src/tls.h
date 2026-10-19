/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tls.h - TLS 1.2 on the Atari itself (Falcon mode), on BearSSL: ECal's
 * session resumption, table-based AES and fastest GHASH, with EMail's
 * ChaCha20-first handshake.
 *
 * The protocols talk to a TLS connection exactly as to a plain one:
 * conn.c sends its bytes through tls_write/tls_read when the connection
 * is encrypted. Certificates are checked against the root certificates
 * in CACERT.PEM (the Mozilla list, as curl publishes it).
 */
#ifndef TLS_H
#define TLS_H

#include <stddef.h>

typedef struct TLS TLS;
void tls_forget_sessions(void);	/* settings changed: start afresh */

/* 0 in the ST edition, which has no TLS of its own (tls_none.c) */
extern const int tls_available;

/* the local time zone, minutes east of UTC, for certificate dates */
extern int tls_tz_minutes;
/* where the random seed is kept between sessions (SEED.DAT) */
extern char tls_seed_path[200];
/* where the certificate signatures already checked are kept (SIGS.DAT);
   empty: for this session only */
extern char tls_sigs_path[200];
/* the decoded root certificates are kept here (ROOTS.DAT, next to CACERT.PEM) */
extern char tls_roots_cache[200];
/* a file's date and time (pf_stamp), 0 if missing; or NULL. With it, a
   ROOTS.DAT checked once against CACERT.PEM's checksum is afterwards
   checked against its size and date only */
extern unsigned long (*tls_file_stamp)(const char *path);
/* up to n bytes of a file from offset off, opening and closing it (open,
   seek, read, close), < 0 on error; or NULL. An accessory sets it: then
   CACERT.PEM is never open while pf_idle lets the program in front run
   (the guidelines, 2.2) */
extern long (*tls_read_at)(const char *path, long off, void *buf, long n);
/* a file's date (as tls_file_stamp) and its size in *size (-1 if
   missing) from one look at the directory (pf_info); or NULL: then
   tls_file_stamp and pf_size, two looks (the guidelines, 6.5) */
extern unsigned long (*tls_file_info)(const char *path, long *size);
/* a word for the status line while something slow happens, or NULL */
extern void (*tls_note)(const char *msg);
/* the handshake's steps and their times, for the log (conn.c), or NULL */
extern void (*tls_log)(const char *msg);

/* load the root certificates from a PEM file (once; later calls are
   cheap). Returns how many, or 0 with err filled in */
int  tls_load_anchors(const char *path, char *err, int errlen);
int  tls_anchor_count(void);
void tls_free_anchors(void);	/* forget them (a new CACERT.PEM) */

/* handshake on a connected socket. flags: TLS_RSA_ONLY asks the server
   for an RSA certificate (fast to check, on the DSP), else ECDSA is
   accepted too; TLS_CHACHA_ONLY offers only ChaCha20-Poly1305 (twice as
   fast as AES-GCM on a 68030, and servers that prefer AES can't choose
   it). A session kept from the last connection to the same host is
   offered for resumption (FTP's data connections take up the control
   connection's session, as FTPS servers want; a new HTTP or IMAP
   connection skips the key exchange). NULL with err filled in on
   failure; *retry then says what a new connection should leave out:
   TLS_RETRY_ECDSA (accept ECDSA) or TLS_RETRY_ANY_CIPHER (every cipher) */
#define TLS_RSA_ONLY     1
#define TLS_CHACHA_ONLY  2
#define TLS_RETRY_ECDSA      1
#define TLS_RETRY_ANY_CIPHER 2
TLS *tls_start(int sock, const char *host, int flags, int *retry, char *err, int errlen);

/* like net_read/net_write: > 0 bytes, 0 nothing yet, < 0 closed */
long tls_read(TLS *t, void *buf, long max);
long tls_write(TLS *t, const void *buf, long n);
void tls_close(TLS *t);		/* sends close_notify, frees; not the socket */

/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305" for the log and the About box */
const char *tls_describe(TLS *t);

/* an RSA public-key operation x = x^e mod n done elsewhere (the Falcon's
   DSP): returns 1 if it did it, 0 to let BearSSL do it. NULL: none */
extern int (*tls_rsa_accel)(unsigned char *x, size_t xlen,
			    const unsigned char *n, size_t nlen,
			    const unsigned char *e, size_t elen);

/* the elliptic curves are done elsewhere (the Falcon's DSP): set by
   tls_set_ec() (declared in ecdsp.h, where BearSSL's types are). Then
   ECDSA is about as cheap as RSA, and a server is offered both at once */
int tls_ec_fast(void);

#endif
