/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * png.h - PNG pictures (png.c): every colour type and depth, interlaced
 * or not, transparency from tRNS and from an alpha channel. The rows come
 * out top to bottom as R G B A bytes, to a function that can shrink them
 * as they come (pic.c), so a picture that isn't interlaced needs no room
 * for all of its pixels at once.
 */
#ifndef PNG_H
#define PNG_H

/* why not */
enum { PNG_OK, PNG_DAMAGED, PNG_UNSUPPORTED, PNG_MEMORY, PNG_TOO_BIG, PNG_STOPPED };

/* 1 and its size, from the header; 0 if it isn't a PNG */
int png_size(const unsigned char *d, long n, long *w, long *h);

/* each row in turn, y from 0, as w * 4 bytes of R G B A; row returns 0
   to stop (PNG_STOPPED). A picture of more than max_px pixels is not
   decoded (PNG_TOO_BIG). A picture cut short gives its rows that came,
   then transparent ones, and PNG_OK. */
int png_decode(const unsigned char *d, long n, long max_px,
	       int (*row)(void *ud, long y, const unsigned char *rgba), void *ud);

#endif
