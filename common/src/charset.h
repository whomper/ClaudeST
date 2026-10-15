/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * charset.h - text conversion between mail charsets and the Atari ST
 * character set (which has Hebrew letters at 0xC2-0xDC), plus the
 * transfer encodings mail uses: base64, quoted-printable, RFC 2047
 * header words and IMAP's modified UTF-7 mailbox names.
 */
#ifndef CHARSET_H
#define CHARSET_H

#include <stddef.h>

enum {
	CS_ASCII, CS_UTF8, CS_LATIN1, CS_LATIN15, CS_CP1252, CS_ISO8859_8,
	CS_CP1255, CS_ATARI
};

int cs_id(const char *name);			/* charset name -> CS_*, unknown -> CS_LATIN1 */

/* one Unicode character -> 0..3 Atari bytes ("?" when there is no match) */
int uni_to_atari(unsigned long u, char *out);
unsigned long atari_to_uni(unsigned char c);

/* convert n bytes of text in charset cs to Atari; malloc'ed, NUL-terminated */
char *cs_to_atari(const char *src, long n, int cs, long *outlen);
/* Atari text -> UTF-8; malloc'ed, NUL-terminated */
char *atari_to_utf8(const char *src, long n, long *outlen);

/* transfer encodings; decoders return malloc'ed buffers */
char *base64_decode(const char *src, long n, long *outlen);
char *base64_encode(const char *src, long n, int linelen, long *outlen);	/* linelen 0 = one line */
char *qp_decode(const char *src, long n, int header, long *outlen);
char *qp_encode(const char *src, long n, long *outlen);	/* body, soft line breaks at 76 */

/* RFC 2047: "=?UTF-8?B?...?=" words in a header value -> Atari text */
char *hdr_decode(const char *value);
/* Atari text -> header value, encoding only when needed */
char *hdr_encode(const char *atari);

/* IMAP modified UTF-7 mailbox names */
char *mutf7_to_atari(const char *name);
char *atari_to_mutf7(const char *name);

/* 1 if s contains bytes >= 0x80 */
int has_8bit(const char *s, long n);

#endif
