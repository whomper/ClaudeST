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
enum { PNG_OK, PNG_DAMAGED, PNG_UNSUPPORTED, PNG_MEMORY, PNG_TOO_BIG, PNG_STOPPED, PNG_MORE };

/* 1 and its size, from the header; 0 if it isn't a PNG */
int png_size(const unsigned char *d, long n, long *w, long *h);

/* each row in turn, y from 0, as w * 4 bytes of R G B A; row returns 0
   to stop (PNG_STOPPED). A picture of more than max_px pixels is not
   decoded (PNG_TOO_BIG). A picture cut short gives its rows that came,
   then transparent ones, and PNG_OK. */
int png_decode(const unsigned char *d, long n, long max_px,
	       int (*row)(void *ud, long y, const unsigned char *rgba), void *ud);
/* the same, but a picture that can have no transparency and isn't
   interlaced gives its rows to row_rgb, w * 3 bytes of R G B (a picture
   cut short: the rest as transparent rows, to row) */
int png_decode_rgb(const unsigned char *d, long n, long max_px,
		   int (*row)(void *ud, long y, const unsigned char *rgba),
		   int (*row_rgb)(void *ud, long y, const unsigned char *rgb), void *ud);

#ifdef PNG_STEPS
/* -DPNG_STEPS: the same decode as a job, a few rows a turn. png_begin
   reads the chunks and takes the memory (rgb: rows as R G B where the
   picture can have no transparency, as png_decode_rgb; png_job_rgb says
   whether it does); each png_step gives up to rows rows (an interlaced
   picture's passes count a row each, then its rows are given) and answers
   PNG_MORE, PNG_OK when the last is given, or why not; png_end frees it
   whatever happened (NULL too) */
typedef struct PNG_JOB PNG_JOB;
int png_begin(PNG_JOB **j, const unsigned char *d, long n, long max_px, int rgb);
int png_step(PNG_JOB *j, long rows,
	     int (*row)(void *ud, long y, const unsigned char *rgba),
	     int (*row_rgb)(void *ud, long y, const unsigned char *rgb), void *ud);
int png_job_rgb(const PNG_JOB *j);
void png_end(PNG_JOB *j);
#endif

#endif
