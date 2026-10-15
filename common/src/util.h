/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * util.h - growable strings and small text helpers.
 */
#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdarg.h>

typedef struct {
	char *s;
	long len, cap;
} SBUF;

void sb_init(SBUF *b);
void sb_free(SBUF *b);
void sb_reset(SBUF *b);
int  sb_add(SBUF *b, const char *s, long n);
int  sb_adds(SBUF *b, const char *s);
int  sb_room(SBUF *b, long n);		/* make room for n more bytes */
int  sb_reserve(SBUF *b, long n);	/* room for exactly n more, no doubling */
int  sb_addc(SBUF *b, char c);
int  sb_printf(SBUF *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *sb_steal(SBUF *b);		/* hand over the buffer (NUL-terminated) */

char *str_ndup(const char *s, long n);
/* n with thousands separators: 1,234,567. Eight buffers in turn, so a
   printf may use several */
const char *num(long n);
char *str_trim(char *s);		/* in place */
int   str_istarts(const char *s, const char *prefix);
const char *str_istr(const char *h, const char *n);	/* case-insensitive strstr */
void  str_copy(char *d, const char *s, long size);	/* always terminated */
void  path_join(char *out, long size, const char *dir, const char *name);
unsigned long str_hash(const char *s);

#endif
