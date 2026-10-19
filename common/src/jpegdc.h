/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * jpegdc.h - JPEGs read at 1/8 of their size from their blocks' DC
 * values alone (see jpegdc.c): what EContact's photo.c and pic.c fall
 * back on when TJpgDec won't take a picture (progressive, SOF1, unusual
 * sampling, CMYK).
 */
#ifndef JPEGDC_H
#define JPEGDC_H

/* why not */
enum { JW_OK, JW_DAMAGED, JW_UNSUPPORTED, JW_MEMORY };

typedef struct JDC JDC;
/* the headers, up to the first scan: the size, or NULL and why */
JDC *jdc_open(const unsigned char *jpg, long len, int *w, int *h, int *why);
/* every block's average; 0 on failure (jdc_why says why) */
int  jdc_read(JDC *j);
int  jdc_why(const JDC *j);
/* the colour of the 8x8 block at bx, by */
void jdc_rgb(const JDC *j, int bx, int by, int *r, int *g, int *b);
void jdc_free(JDC *j);
/* called now and then while reading; 0 stops it (Cancel) */
extern int (*jdc_poll)(void);

#endif
