/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * inflate.h - gzip/DEFLATE decompression (inflate.c): HTTP answers,
 * PNG pictures, EWeb's font. Each appends what it unpacks to out, which
 * may grow to max bytes: 1 when the data was whole, 0 when it is
 * damaged, -1 when it would unpack to more than max (ECal and EContact,
 * before the limit, unpacked without one: LONG_MAX keeps that).
 */
#ifndef INFLATE_H
#define INFLATE_H

#include "util.h"

int inflate_raw(const unsigned char *in, long n, SBUF *out, long max);
int inflate_gzip(const unsigned char *in, long n, SBUF *out, long max);
int inflate_zlib(const unsigned char *in, long n, SBUF *out, long max);

/* raw DEFLATE a piece at a time, through a 32 KB window (about 40 KB in
   all, one malloc): get() gives up to n bytes of the compressed data
   (0 or less: no more); inflate_read() gives up to n bytes unpacked,
   0 at the end, -1 when the data is damaged (what came before the
   damage first: a picture cut short shows as far as it goes). A file
   of any size, and a turn for GEM between pieces (the guidelines'
   Speed). */
typedef struct inflate INFLATE;
INFLATE *inflate_open(int (*get)(void *u, unsigned char *buf, long n), void *u);
long inflate_read(INFLATE *z, unsigned char *out, long n);
void inflate_close(INFLATE *z);

/* the same with a buffer of its own that is also the window (about
   72 KB: 64 KB and the tables), for a caller that takes a piece where it
   lies: each byte stored once, a match a plain copy from further back,
   no ring to wrap. inflate_piece() unpacks the next piece, up to max
   bytes (at most 32 KB) and up to 257 more where a match runs on, and
   sets *p to it; it stays where it is until the next call. turn (or 0)
   is called after each 1 KB or so, for GEM. 0 at the end, -1 damaged
   (*p's piece is then what came before the damage). inflate_read()
   isn't for such a one; inflate_close() frees it. (EUpdate's unzip: a
   stock Falcon unpacked programs at some 80 KB/s through
   inflate_read, an instruction-cache miss most of every symbol: Erez,
   1 Nov 2026, "saving takes forever") */
INFLATE *inflate_open_piece(int (*get)(void *u, unsigned char *buf, long n), void *u);
long inflate_piece(INFLATE *z, const unsigned char **p, long max, void (*turn)(void));

#endif
