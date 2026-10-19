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

#endif
