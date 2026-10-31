/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic.c - pictures (pic.h): JPEG, PNG and GIF (and WebP, built with
 * PIC_LIBWEBP) decoded to the size they are shown at, and the screen's own
 * colours for them.
 *
 * Each decoder gives its rows in turn, at its own size, to the scaler,
 * which makes the picture's rows as they come: a shrinking picture is
 * averaged (each pixel shown is the mean of the pixels it covers,
 * weighted by how solid they are), an enlarged one repeats its pixels.
 * So only the shown picture is kept whole, never the file's: a 1000x750
 * photo shown at 320x240 takes 230 KB, not 3 MB. A JPEG is first
 * unpacked at 1/2, 1/4 or 1/8 when it is shown that small or smaller,
 * which skips most of the work.
 *
 * The scaler's columns come from a table made once a picture, and its
 * sums are 32-bit additions: the only divisions are one a component of
 * each pixel shown.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"
#include "png.h"
#include "nsgif.h"
#ifdef PIC_LIBJPEG
#include <stdio.h>		/* jpeglib.h names FILE */
#define JPEG_INTERNALS		/* the natural order, for the DSP's way */
#include "jpeglib.h"
#include "jerror.h"
#else
#include "jpegdc.h"
#include "tjpgd.h"
#endif
#ifdef PIC_LIBWEBP
/* libwebp's decoder (third_party/libwebp): its own calls too, for a lossy
   picture's rows as they come */
#include "src/dec/vp8i_dec.h"
#include "src/dec/webpi_dec.h"
#include "src/dsp/dsp.h"
#include "src/webp/decode.h"
#endif

int (*pic_poll)(void);

/* ---------------------------------------------------------------- kinds */

int pic_kind(const unsigned char *d, long n)
{
	long i;
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
		return PIC_JPEG;
	if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
		return PIC_PNG;
	if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
		return PIC_GIF;
	if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
		return PIC_WEBP;
	if (n >= 12 && memcmp(d + 4, "ftyp", 4) == 0 && (memcmp(d + 8, "avif", 4) == 0 || memcmp(d + 8, "avis", 4) == 0))
		return PIC_AVIF;
	if (n >= 2 && d[0] == 'B' && d[1] == 'M')
		return PIC_BMP;
	if (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0)
		return PIC_ICO;
	for (i = 0; i + 4 <= n && i < 1024; i++)
		if (memcmp(d + i, "<svg", 4) == 0)
			return PIC_SVG;
	return PIC_NONE;
}

const char *pic_kind_name(int kind)
{
	switch (kind) {
	case PIC_JPEG: return "JPEG";
	case PIC_PNG: return "PNG";
	case PIC_GIF: return "GIF";
	case PIC_BMP: return "BMP";
	case PIC_WEBP: return "WebP";
	case PIC_AVIF: return "AVIF";
	case PIC_SVG: return "SVG";
	case PIC_ICO: return "icon";
	}
	return "unknown";
}

const char *pic_why_text(int why)
{
	switch (why) {
	case PIC_OK: return "decoded";
	case PIC_DAMAGED: return "damaged";
	case PIC_UNSUPPORTED: return "a kind of picture that can't be shown";
	case PIC_MEMORY: return "not enough memory";
	case PIC_TOO_BIG: return "too big";
	case PIC_STOPPED: return "stopped";
	}
	return "not decoded";
}

/* a JPEG's size, from its frame header (SOF0..SOF15 but DHT, JPG, DAC) */
static int jpeg_size(const unsigned char *d, long n, long *w, long *h)
{
	long p = 2;
	while (p + 4 <= n) {
		int m;
		long len;
		if (d[p] != 0xFF) {
			p++;
			continue;
		}
		m = d[p + 1];
		if (m == 0xFF) {
			p++;
			continue;
		}
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
			p += 2;
			continue;
		}
		len = ((long) d[p + 2] << 8) | d[p + 3];
		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			if (p + 9 > n)
				return 0;
			*h = ((long) d[p + 5] << 8) | d[p + 6];
			*w = ((long) d[p + 7] << 8) | d[p + 8];
			return *w > 0 && *h > 0;
		}
		if (m == 0xD9 || m == 0xDA || len < 2)
			return 0;
		p += 2 + len;
	}
	return 0;
}

int pic_size(const unsigned char *d, long n, long *w, long *h)
{
	switch (pic_kind(d, n)) {
	case PIC_JPEG:
		return jpeg_size(d, n, w, h);
	case PIC_PNG:
		return png_size(d, n, w, h) && *w > 0 && *h > 0;
	case PIC_GIF:
		if (n < 10)
			return 0;
		*w = d[6] | ((long) d[7] << 8);
		*h = d[8] | ((long) d[9] << 8);
		return *w > 0 && *h > 0;
#ifdef PIC_LIBWEBP
	case PIC_WEBP: {
		int ww, hh;
		if (!WebPGetInfo(d, (size_t) n, &ww, &hh))
			return 0;
		*w = ww;
		*h = hh;
		return *w > 0 && *h > 0;
	}
#endif
	}
	return 0;
}

/* ---------------------------------------------------------------- the scaler */

typedef struct {
	PIC *pic;
	long sw, sh;			/* the rows that come */
	int down_x, down_y;		/* shrinking (else the same or enlarging) */
	short *xmap;			/* down_x: each column's pixel shown; else each shown pixel's column */
	unsigned long *sum;		/* down: dw * 5 (r, g, b weighted by alpha; alpha; pixels) */
	long dy;			/* the row shown being made */
	long y;				/* rows taken */
	int any_alpha;
	int polls;
	int solid;			/* every row came by sc_row_rgb: no alpha in the sums */
	/* solid rows whose sums fit 16 bits (a JPEG's): R G B sums of 16
	   bits, a third of the memory the 5 longs take, and the counts
	   from the columns (cw) and the rows taken (ny) */
	int s16;
	unsigned short *sum16;		/* dw * 3 */
	short *xm3;			/* xmap * 3 (down_x: the sums' place; else the pixel's) */
	unsigned short *cw;		/* each column shown: the columns in it */
	int ny;				/* rows in the sums */
	int maxcw;
	unsigned char *rgb;		/* sw * 3: an opaque R G B A row as R G B */
} SCALER;

/* a 16 x 16 product: a 68000's mulu.w (GCC calls its 32-bit routine for
   one followed by a shift, though both are 16 bits) */
#if defined(__mc68000__) && !defined(__mc68020__) && !defined(__mc68030__) && !defined(__mc68040__) && \
    !defined(__mc68060__)
static unsigned long mul16(unsigned short a, unsigned short b)
{
	unsigned long r = a;
	__asm__("mulu.w %1,%0" : "+d" (r) : "dmi" (b));
	return r;
}
#else
#define mul16(a, b) ((unsigned long) (unsigned short) (a) * (unsigned short) (b))
#endif

/* n source pixels to m shown, by their centres: down, each source
   pixel's pixel shown; else each shown pixel's source */
static long centre(long i, long from, long to)
{
	return ((2 * i + 1) * to) / (2 * from);
}

static short *map(long n, long m, int down)
{
	long i, cnt = down ? n : m;
	short *t = malloc((size_t) cnt * sizeof *t);
	if (t == NULL)
		return NULL;
	for (i = 0; i < cnt; i++)
		t[i] = (short) (down ? centre(i, n, m) : centre(i, m, n));
	return t;
}

static int sc_init(SCALER *s, PIC *p, long sw, long sh)
{
	memset(s, 0, sizeof *s);
	s->pic = p;
	s->sw = sw;
	s->sh = sh;
	s->down_x = sw > p->w;
	s->down_y = sh > p->h;
	s->xmap = map(sw, p->w, s->down_x);
	s->sum = calloc((size_t) p->w * 5, sizeof *s->sum);
	return s->xmap != NULL && s->sum != NULL;
}

static void sc_free(SCALER *s)
{
	free(s->xmap);
	free(s->sum);
	free(s->sum16);
	free(s->xm3);
	free(s->cw);
	free(s->rgb);
}

/* the 16-bit sums for solid rows, when each pixel shown is made of 257
   or fewer: 1 (else the 5 longs, as for any picture) */
static int sc_16(SCALER *s)
{
	long x, y, w = s->pic->w, run = 0, maxny = 1, last = -1;
	s->sum16 = calloc((size_t) w * 3, sizeof *s->sum16);
	s->xm3 = malloc((size_t) (s->down_x ? s->sw : w) * sizeof *s->xm3);
	s->cw = calloc((size_t) w + 1, sizeof *s->cw);
	if (s->sum16 == NULL || s->xm3 == NULL || s->cw == NULL)
		return 0;
	if (s->down_x) {
		for (x = 0; x < s->sw; x++) {
			s->xm3[x] = (short) (s->xmap[x] * 3);
			s->cw[s->xmap[x]]++;
		}
	} else {
		for (x = 0; x < w; x++) {
			s->xm3[x] = (short) (s->xmap[x] * 3);
			s->cw[x] = 1;
		}
	}
	s->maxcw = 1;
	for (x = 0; x < w; x++)
		if (s->cw[x] > s->maxcw)
			s->maxcw = s->cw[x];
	if (s->down_y)
		for (y = 0; y < s->sh; y++) {
			long dy = centre(y, s->sh, s->pic->h);
			run = dy == last ? run + 1 : 1;
			last = dy;
			if (run > maxny)
				maxny = run;
		}
	return (long) s->maxcw * maxny <= 257;
}

/* pixel i of the picture shown */
static void put(SCALER *s, long i, int r, int g, int b, int a)
{
	s->pic->px[i] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s->pic->alpha[i] = (unsigned char) a;
	if (a != 255)
		s->any_alpha = 1;
}


/* 65536 / n, rounded, for averaging n pixels with a 16 x 16 multiply
   (a 68000 divides 32 bits in software: three to a pixel were most of
   the scaler's time); made the first time it is needed */
static unsigned short recip[258];

/* the sums made into row dy, and cleared */
static void flush(SCALER *s, long dy)
{
	long x, i = dy * s->pic->w;
	unsigned long *q = s->sum;
	if (recip[2] == 0)
		for (x = 2; x < 258; x++)
			recip[x] = (unsigned short) ((65536L + x / 2) / x);
	if (s->s16) {
		/* each column's divisor for these rows, the mean rounded */
		unsigned short k[258], *o = s->pic->px + i, *q16 = s->sum16;
		int c;
		for (c = 1; c <= s->maxcw; c++)
			k[c] = c * s->ny > 1 ? recip[c * s->ny] : 0;
		for (x = 0; x < s->pic->w; x++, q16 += 3) {
			unsigned r, g, b;
			unsigned short kk = k[s->cw[x]];
			if (kk == 0) {
				r = q16[0];
				g = q16[1];
				b = q16[2];
			} else {
				r = (unsigned) ((mul16(q16[0], kk) + 32768UL) >> 16);
				g = (unsigned) ((mul16(q16[1], kk) + 32768UL) >> 16);
				b = (unsigned) ((mul16(q16[2], kk) + 32768UL) >> 16);
			}
			o[x] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
			q16[0] = q16[1] = q16[2] = 0;
		}
		memset(s->pic->alpha + i, 255, (size_t) s->pic->w);
		s->ny = 0;
		return;
	}
	if (s->solid) {
		/* solid rows (a JPEG's): sums of at most 257 pixels fit 16 bits,
		   the mean rounded */
		unsigned short *o = s->pic->px + i;
		for (x = 0; x < s->pic->w; x++, q += 5) {
			unsigned n = (unsigned) q[4], r, g, b;
			if (n == 0) {
				r = g = b = 0;
			} else if (n == 1) {
				r = (unsigned) q[0];
				g = (unsigned) q[1];
				b = (unsigned) q[2];
			} else if (n < 258) {
				unsigned short k = recip[n];
				r = (unsigned) ((mul16((unsigned short) q[0], k) + 32768UL) >> 16);
				g = (unsigned) ((mul16((unsigned short) q[1], k) + 32768UL) >> 16);
				b = (unsigned) ((mul16((unsigned short) q[2], k) + 32768UL) >> 16);
			} else {
				r = (unsigned) (q[0] / n);
				g = (unsigned) (q[1] / n);
				b = (unsigned) (q[2] / n);
			}
			o[x] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
			q[0] = q[1] = q[2] = q[3] = q[4] = 0;
		}
		memset(s->pic->alpha + i, 255, (size_t) s->pic->w);
		return;
	}
	for (x = 0; x < s->pic->w; x++, q += 5, i++) {
		unsigned long n = q[4], a = q[3];
		if (n == 0) {
			put(s, i, 0, 0, 0, 0);
		} else if (n == 1) {
			/* one pixel (enlarging): as it is, no divide (a 68000's are
			   in software) */
			if (a == 255)
				put(s, i, (int) q[0], (int) q[1], (int) q[2], 255);
			else if (a == 0)
				put(s, i, 0, 0, 0, 0);
			else {
				unsigned long r = q[0] * 255 / a, g = q[1] * 255 / a, b = q[2] * 255 / a;
				put(s, i, r > 255 ? 255 : (int) r, g > 255 ? 255 : (int) g, b > 255 ? 255 : (int) b, (int) a);
			}
		} else if (a == 255 * n) {
			put(s, i, (int) (q[0] / n), (int) (q[1] / n), (int) (q[2] / n), 255);
		} else if (a == 0) {
			put(s, i, 0, 0, 0, 0);
		} else {
			/* the colour as solid, weighted by alpha: sum (c * (a + 1) >> 8) / sum a */
			unsigned long r = q[0] * 255 / a, g = q[1] * 255 / a, b = q[2] * 255 / a;
			put(s, i, r > 255 ? 255 : (int) r, g > 255 ? 255 : (int) g, b > 255 ? 255 : (int) b,
			    (int) (a / n));
		}
		q[0] = q[1] = q[2] = q[3] = q[4] = 0;
	}
}

/* enlarging n rows to m: the first row shown whose source is row i or
   later, the least d with centre(d, m, n) >= i */
static long first_shown(long i, long n, long m)
{
	long d = (2 * i * m - m + 2 * n - 1) / (2 * n);
	if (d < 0)
		d = 0;
	while (d > 0 && centre(d - 1, m, n) >= i)
		d--;
	while (d < m && centre(d, m, n) < i)
		d++;
	return d;
}

static void sc_rows(SCALER *s);

/* a row of a solid picture, R G B (a JPEG's): no alpha to weigh */
static int sc_row_rgb(SCALER *s, const unsigned char *rgb)
{
	long x, w = s->pic->w;
	unsigned long *q;
	if (pic_poll != NULL && ++s->polls >= 16) {
		s->polls = 0;
		if (!pic_poll())
			return 0;
	}
	if (s->y >= s->sh)
		return 1;
	if (s->y == 0) {
		s->solid = 1;
		s->s16 = (s->sw != w || s->sh != s->pic->h) && sc_16(s);
	}
	if (s->s16) {
		unsigned short *q16 = s->sum16;
		const short *m = s->xm3;
		if (s->down_x) {
			for (x = 0; x < s->sw; x++, rgb += 3) {
				unsigned short *e = q16 + m[x];
				e[0] += rgb[0];
				e[1] += rgb[1];
				e[2] += rgb[2];
			}
		} else {
			for (x = 0; x < w; x++, q16 += 3) {
				const unsigned char *c = rgb + m[x];
				q16[0] += c[0];
				q16[1] += c[1];
				q16[2] += c[2];
			}
		}
		s->ny++;
		sc_rows(s);
		return 1;
	}
	if (s->sw == w && s->sh == s->pic->h) {
		unsigned short *o = s->pic->px + mul16((unsigned short) s->y, (unsigned short) w);
		for (x = 0; x < w; x++, rgb += 3)
			o[x] = (unsigned short) (((rgb[0] & 0xF8) << 8) | ((rgb[1] & 0xFC) << 3) | (rgb[2] >> 3));
		/* solid, for when clear rows follow (a JPEG cut short) */
		memset(s->pic->alpha + mul16((unsigned short) s->y, (unsigned short) w), 255, (size_t) w);
		s->y++;
		return 1;
	}
	if (s->down_x) {
		const short *m = s->xmap;
		for (x = 0; x < s->sw; x++, rgb += 3) {
			q = s->sum + m[x] * 5;
			q[0] += rgb[0];
			q[1] += rgb[1];
			q[2] += rgb[2];
			q[3] += 255;
			q[4]++;
		}
	} else {
		for (x = 0, q = s->sum; x < w; x++, q += 5) {
			const unsigned char *c = rgb + s->xmap[x] * 3;
			q[0] += c[0];
			q[1] += c[1];
			q[2] += c[2];
			q[3] += 255;
			q[4]++;
		}
	}
	sc_rows(s);
	return 1;
}

/* a row of the decoded picture, R G B A */
static int sc_row(SCALER *s, const unsigned char *rgba)
{
	long x, w = s->pic->w;
	if ((s->y == 0 || s->solid) && s->y < s->sh) {
		/* while every row is opaque (most PNGs and GIFs), the solid
		   rows' sums: no alpha to weigh, no divides */
		const unsigned char *c = rgba;
		unsigned char *o = s->rgb;
		if (o == NULL && (o = s->rgb = malloc((size_t) s->sw * 3)) == NULL)
			goto rgba;
		for (x = 0; x < s->sw && c[3] == 255; x++, c += 4, o += 3) {
			o[0] = c[0];
			o[1] = c[1];
			o[2] = c[2];
		}
		if (x == s->sw)
			return sc_row_rgb(s, s->rgb);
	}
rgba:
	if (s->s16) {
		/* the 16-bit sums so far into the longs */
		for (x = 0; x < w; x++) {
			unsigned long n = (unsigned long) s->cw[x] * s->ny;
			s->sum[5 * x] = s->sum16[3 * x];
			s->sum[5 * x + 1] = s->sum16[3 * x + 1];
			s->sum[5 * x + 2] = s->sum16[3 * x + 2];
			s->sum[5 * x + 3] = 255 * n;
			s->sum[5 * x + 4] = n;
		}
		s->s16 = 0;
	}
	s->solid = 0;		/* the sums weigh alpha from now on */
	if (pic_poll != NULL && ++s->polls >= 16) {
		s->polls = 0;
		if (!pic_poll())
			return 0;
	}
	if (s->y >= s->sh)
		return 1;
	if (s->sw == w && s->sh == s->pic->h) {
		/* the size it is: as it is */
		const unsigned char *q = rgba;
		unsigned short *o = s->pic->px + s->y * w;
		unsigned char *a = s->pic->alpha + s->y * w;
		unsigned all = 255;
		for (x = 0; x < w; x++, q += 4) {
			o[x] = (unsigned short) (((q[0] & 0xF8) << 8) | ((q[1] & 0xFC) << 3) | (q[2] >> 3));
			a[x] = q[3];
			all &= q[3];
		}
		if (all != 255)
			s->any_alpha = 1;
		s->y++;
		return 1;
	}
	{
		/* the row's pixels to the columns shown */
		unsigned long *q;
		if (s->down_x) {
			const unsigned char *c = rgba;
			for (x = 0; x < s->sw; x++, c += 4) {
				q = s->sum + s->xmap[x] * 5;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		} else {
			for (x = 0, q = s->sum; x < w; x++, q += 5) {
				const unsigned char *c = rgba + s->xmap[x] * 4;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		}
	}
	sc_rows(s);
	return 1;
}

/* the rows shown that the row summed finishes (shrinking: when the next
   one's first row comes; enlarging: each whose centre falls in it), and
   the next row */
static void sc_rows(SCALER *s)
{
	long w = s->pic->w;
	if (s->down_y) {
		/* rows shown are made when the next one's first row comes */
		long dy = s->y + 1 < s->sh ? centre(s->y + 1, s->sh, s->pic->h) : -1;
		if (dy != s->dy) {
			flush(s, s->dy);
			s->dy = dy;
		}
	} else {
		/* every row shown whose centre falls in this one: those from
		   dy0 to before dy1 */
		long dy0 = first_shown(s->y, s->sh, s->pic->h), dy1 = first_shown(s->y + 1, s->sh, s->pic->h), dy;
		if (dy1 > dy0) {
			flush(s, dy0);
			for (dy = dy0 + 1; dy < dy1; dy++)
				memcpy(s->pic->px + dy * w, s->pic->px + dy0 * w, (size_t) w * 2),
				memcpy(s->pic->alpha + dy * w, s->pic->alpha + dy0 * w, (size_t) w);
		} else if (s->s16) {
			memset(s->sum16, 0, (size_t) w * 3 * sizeof *s->sum16);
			s->ny = 0;
		} else {
			memset(s->sum, 0, (size_t) w * 5 * sizeof *s->sum);
		}
	}
	s->y++;
}

/* ---------------------------------------------------------------- JPEG */

#ifdef PIC_LIBJPEG
/* libjpeg-turbo (third_party/libjpeg-turbo), the library NetSurf takes:
   every kind of JPEG a page has (progressive ones too), unpacked by its
   IDCT straight at n/8 of the size (n from 1 to 8), the smallest still
   as big as shown; on a 68030 1.8 times as quick as TJpgDec at the same
   size, and six times on a big photo shown small */

typedef struct {
	struct jpeg_error_mgr pub;
	void *jmp[5];			/* __builtin_setjmp's */
} LJERR;

static void lj_exit(j_common_ptr c)
{
	__builtin_longjmp(((LJERR *) c->err)->jmp, 1);
}

static void lj_emit(j_common_ptr c, int level)
{
	(void) c;
	(void) level;
}

static void lj_output(j_common_ptr c)
{
	(void) c;
}

static void lj_format(j_common_ptr c, char *buf)
{
	(void) c;
	buf[0] = 0;
}

static void lj_reset(j_common_ptr c)
{
	c->err->num_warnings = 0;
	c->err->msg_code = 0;
}

/* libjpeg's error handling without its jerror.c (which prints to stderr
   and calls exit(), neither of which the Atari's runtime has): no
   messages, and a failure jumps back to decode_jpeg */
static struct jpeg_error_mgr *lj_errors(struct jpeg_error_mgr *e)
{
	memset(e, 0, sizeof *e);
	e->error_exit = lj_exit;
	e->emit_message = lj_emit;
	e->output_message = lj_output;
	e->format_message = lj_format;
	e->reset_error_mgr = lj_reset;
	return e;
}

/* the file, in memory: cut short, it ends there (libjpeg makes the rest
   grey, as browsers show it) */
static void src_init(j_decompress_ptr c)
{
	(void) c;
}

static boolean src_fill(j_decompress_ptr c)
{
	static const JOCTET eoi[2] = { 0xFF, JPEG_EOI };
	c->src->next_input_byte = eoi;
	c->src->bytes_in_buffer = 2;
	return TRUE;
}

static void src_skip(j_decompress_ptr c, long n)
{
	if (n <= 0)
		return;
	if ((size_t) n > c->src->bytes_in_buffer) {
		src_fill(c);
		return;
	}
	c->src->next_input_byte += n;
	c->src->bytes_in_buffer -= (size_t) n;
}

static void src_term(j_decompress_ptr c)
{
	(void) c;
}

/* what a decode holds, kept off the stack that __builtin_longjmp leaves */
typedef struct {
	struct jpeg_decompress_struct ci;
	LJERR err;
	struct jpeg_source_mgr src;
	unsigned char *row;
	int stopped;
	int dsp_on;			/* the DSP is held (let go if libjpeg gives up) */
	struct jpeg_progress_mgr prog;
} LJ;

/* libjpeg's progress hook, before each row of MCUs it reads: a
   progressive picture's scans are all read first, in one call
   (jpeg_read_coefficients, jpeg_start_decompress), and a 320 by 240 one
   was 1.2 s of a stock Falcon without a turn (EWeb's speed rig, 31 Oct
   2026): pic_poll's turn there too, and stopped as anywhere else */
static void lj_progress(j_common_ptr c)
{
	LJ *j = (LJ *) (void *) c;	/* (ci is its first) */
	if (pic_poll != NULL && !pic_poll()) {
		j->stopped = 1;
		__builtin_longjmp(j->err.jmp, 1);
	}
}

/* ---------------------------------------------------------------- JPEG on the DSP */

/* dsp/jpeg.a56 decodes a baseline JPEG itself: libjpeg reads the headers,
   the 68030 sends the scan's bytes when the DSP asks for them and reads
   each MCU's pixels back. The DSP unpacks the Huffman codes, dequantises,
   transforms straight to n/8 of the size (each pixel the mean of those
   it covers), makes the colours (the chroma over the luma it covers) */

const PIC_DSP *pic_dsp;
int pic_dsp_failed;
int pic_dsp_shrink = 1;
int pic_dsp_dither = 1;
static int dsp_state;			/* 0: not checked yet, 1: checked, -1: the 68030 does it */
static int dsp_testing;			/* the check: 1 its DSP decodes, 2 its 68030 ones */

/* jpeg.a56's commands and memory */
#define D_LOADX  1
#define D_LOADY  4
#define D_DECODE 7
#define D_VARS   0x00			/* x: the variables */
#define D_PRED   0x20			/* x: the DC predictors */
#define D_BLK    0x40			/* x: the block */
#define D_ZZ     0xc0			/* x: the natural order */
#define D_TRM    0x600			/* x: the colour terms */
#define D_TR     0x2000			/* x, y, y: the colour tables */
#define D_TG     0x2000
#define D_TB     0x2400
#define D_FT     0x2a00			/* x, y: the fast AC tables (made by jpeg.a56) */
#define D_HLA    0x1000			/* x, y: the Huffman tables, $200 a component */
#define D_SPLIT  0x1e00			/* x, y: AC symbols' runs and sizes */
#define D_TAB    0x1f00			/* x, y: the small tables */
#define D_BT     (D_TAB + 0x20)		/* x: the block table */
#define D_QT     0x40			/* y: the quantisers, 64 a component */
#define D_YPL    0x200			/* y: the MCU's luma */
#define D_CBP    0x300			/* y: its Cb, Cr */
#define D_CRP    0x340
#define D_CONST  0x500			/* y: the format's constants */
#define D_KT     0x900			/* y: shrinking's divisors */
#define D_SHRINK 0x3000			/* y: shrinking's tables, 4096 words at most */

/* the 8-point inverse DCT: B[x][u] = C(u) / 2 * cos((2x + 1) u pi / 16),
   times 2^23 */
static const long dsp_basis[8][8] = {
	{  2965821,  4113712,  3875032,  3487436,  2965821,  2330230,  1605091,   818268 },
	{  2965821,  3487436,  1605091,  -818268, -2965821, -4113712, -3875032, -2330230 },
	{  2965821,  2330230, -1605091, -4113712, -2965821,   818268,  3875032,  3487436 },
	{  2965821,   818268, -3875032, -2330230,  2965821,  3487436, -1605091, -4113712 },
	{  2965821,  -818268, -3875032,  2330230,  2965821, -3487436, -1605091,  4113712 },
	{  2965821, -2330230, -1605091,  4113712, -2965821,  -818268,  3875032, -3487436 },
	{  2965821, -3487436,  1605091,   818268, -2965821,  4113712, -3875032,  2330230 },
	{  2965821, -4113712,  3875032, -3487436,  2965821, -2330230,  1605091,  -818268 }
};

/* each format's 9 constants (jpeg.a56's colour tables): for R, G and B
   its mask, how far it moves down (halved: jpeg.a56 doubles the product,
   so R G B's R, which stays, is exact) and what puts its top bit right */
static const unsigned long dsp_565[9] = {
	0xF80000, 0x004000, 0x008000, 0xFC0000, 0x000200, 0x000400, 0xF80000, 0x000008, 0x000010
};
static const unsigned long dsp_rgb[9] = {
	0xFF0000, 0x400000, 0x800000, 0xFF0000, 0x004000, 0x008000, 0xFF0000, 0x000040, 0x000080
};

/* the JPEGs it takes: 8-bit baseline (Huffman, one scan) of grey or of
   Y Cb Cr with the chroma at most halved each way, shown at 2/8 of the
   size or more (at 1/8 there is no transform to speak of) */
static int dsp_takes(j_decompress_ptr c, int k)
{
	jpeg_component_info *ci = c->comp_info;
	int i;
	if (k < 2 || c->data_precision != 8 || c->progressive_mode || c->arith_code || c->master->lossless ||
	    c->inputctl->has_multiple_scans || c->blocks_in_MCU > 6)
		return 0;
	for (i = 0; i < c->num_components; i++)
		if (c->dc_huff_tbl_ptrs[ci[i].dc_tbl_no] == NULL || c->ac_huff_tbl_ptrs[ci[i].ac_tbl_no] == NULL ||
		    ci[i].quant_table == NULL)
			return 0;
	if (c->num_components == 1)
		return c->jpeg_color_space == JCS_GRAYSCALE;
	return c->num_components == 3 && c->jpeg_color_space == JCS_YCbCr && c->comps_in_scan == 3 &&
	       ci[0].h_samp_factor <= 2 && ci[0].v_samp_factor <= 2 && ci[1].h_samp_factor == 1 &&
	       ci[1].v_samp_factor == 1 && ci[2].h_samp_factor == 1 && ci[2].v_samp_factor == 1;
}

/* a JPEG libjpeg reads into coefficients (progressive or
   arithmetic-coded), whose transform and colours the DSP may take: the
   kinds it takes from the scan otherwise */
static int dsp_takes_coef(j_decompress_ptr c, int k)
{
	jpeg_component_info *ci = c->comp_info;
	int i;
	if (k < 2 || c->data_precision != 8 || c->master->lossless || !(c->progressive_mode || c->arith_code))
		return 0;
	for (i = 0; i < c->num_components; i++)
		if (ci[i].quant_table == NULL && c->quant_tbl_ptrs[ci[i].quant_tbl_no] == NULL)
			return 0;
	if (c->num_components == 1)
		return c->jpeg_color_space == JCS_GRAYSCALE;
	return c->num_components == 3 && c->jpeg_color_space == JCS_YCbCr && ci[0].h_samp_factor <= 2 &&
	       ci[0].v_samp_factor <= 2 && ci[1].h_samp_factor == 1 && ci[1].v_samp_factor == 1 &&
	       ci[2].h_samp_factor == 1 && ci[2].v_samp_factor == 1;
}

/* the M x 8 transform to M pixels a block: pixel i the mean of the
   8/M pixels it covers (parts of them at the ends), twice the true
   one (so each entry is below 1, as the DSP's fractions are) */
static void dsp_matrix(unsigned long *w, int m)
{
	int i, u, x;
	for (i = 0; i < m; i++)
		for (u = 0; u < 8; u++) {
			long sum = 0;
			for (x = 0; x < 8; x++) {
				/* how much of source pixel x is in pixel i, in M-ths:
				   [mx, mx + m) against [8i, 8i + 8) */
				int lo = m * x > 8 * i ? m * x : 8 * i, hi = m * x + m < 8 * i + 8 ? m * x + m : 8 * i + 8;
				if (hi > lo)
					sum += (hi - lo) * dsp_basis[x][u];
			}
			/* weights (hi - lo) / 8, times 2 */
			sum = sum >= 0 ? (sum + 2) >> 2 : -((-sum + 2) >> 2);
			*w++ = (unsigned long) sum & 0xFFFFFFUL;
		}
}

/* words into the DSP's X or Y memory */
static void dsp_load(int cmd, unsigned addr, const unsigned long *w, long n)
{
	unsigned long h[3];
	h[0] = (unsigned long) cmd;
	h[1] = addr;
	h[2] = (unsigned long) n;
	pic_dsp->put(h, 3);
	pic_dsp->put(w, n);
}

/* a Huffman table for jpeg.a56: the length and symbol of the code each
   byte the bits may start with begins with (0: longer than 8 bits) at
   base; maxcode[l] and valoff[l] for the longer ones, and the symbols, at
   base + $800 (as libjpeg's jpeg_make_d_derived_tbl) */
static void dsp_huff(const JHUFF_TBL *t, unsigned base)
{
	static unsigned long len[256], sym[256], mv[64], hv[256];
	long code = 0;
	int l, i, p = 0;
	memset(len, 0, sizeof len);
	memset(sym, 0, sizeof sym);
	memset(hv, 0, sizeof hv);
	for (i = 0; i < 64; i++)
		mv[i] = 0xFFFFFFUL;	/* maxcode -1: none */
	for (l = 1; l <= 16; l++) {
		mv[32 + l] = (unsigned long) (p - code) & 0xFFFFFFUL;	/* valoff */
		for (i = 0; i < t->bits[l] && p < 256; i++, p++, code++) {
			hv[p] = t->huffval[p];
			if (l <= 8) {
				int sfx, n = 1 << (8 - l);
				for (sfx = 0; sfx < n; sfx++) {
					int at = (int) ((code << (8 - l)) | sfx) & 255;
					len[at] = (unsigned long) l;
					sym[at] = t->huffval[p];
				}
			}
		}
		if (t->bits[l])
			mv[l] = (unsigned long) (code - 1);
		code <<= 1;
	}
	mv[17] = 0x7FFFFFUL;
	dsp_load(D_LOADX, base, len, 256);
	dsp_load(D_LOADY, base, sym, 256);
	dsp_load(D_LOADX, base + 0x800, mv, 64);
	dsp_load(D_LOADY, base + 0x800, hv, 256);
}

/* the tables every picture has the same (the natural order, AC symbols
   split, the shifts and masks, the rows and columns used up to each
   coefficient) */
static void dsp_fixed(void)
{
	static unsigned long t[256], u[256];
	int i, r, c;
	for (i = 0; i < 64; i++)
		t[i] = (unsigned long) jpeg_natural_order[i];
	dsp_load(D_LOADX, D_ZZ, t, 64);
	for (i = 0; i < 256; i++) {
		t[i] = (unsigned long) (i >> 4);
		u[i] = (unsigned long) (i & 15);
	}
	dsp_load(D_LOADX, D_SPLIT, t, 256);
	dsp_load(D_LOADY, D_SPLIT, u, 256);
	for (i = 0; i <= 16; i++) {
		t[i] = (1UL << i) - 1;				/* x: 2^s - 1 */
		u[i] = i ? 1UL << (i - 1) : 0;			/* y: 2^(s - 24), as a fraction */
		u[0x20 + i] = i ? 1UL << (i - 1) : 0;		/* y: 2^(s - 1) */
	}
	for (i = 0; i <= 17; i++)
		u[0x40 + i] = i >= 2 ? 1UL << (24 - i) : 0;	/* y: the refill's shift */
	for (i = 0, r = c = 0; i < 64; i++) {
		int n = jpeg_natural_order[i];
		r = n >> 3 > r ? n >> 3 : r;
		c = (n & 7) > c ? n & 7 : c;
		t[0x80 + i] = (unsigned long) r;	/* x: the last row to coefficient k */
		u[0x80 + i] = (unsigned long) c;	/* y: the last column */
	}
	dsp_load(D_LOADX, D_TAB, t, 17);
	dsp_load(D_LOADX, D_TAB + 0x80, t + 0x80, 64);
	dsp_load(D_LOADY, D_TAB, u, 0xc0);
}

/* the scan's bytes for jpeg.a56, two to a word (b0 << 8 | b1): the 0
   after each $FF taken out, the data ending at a marker (a restart's
   RST, or the end), and 0s past it. Or (coef not NULL) a progressive
   JPEG's coefficients, as libjpeg read them: for each block of each MCU
   in turn, K and its first K in zigzag order (to the last other than 0) */
typedef struct {
	const unsigned char *p, *end;
	int stop;
	j_decompress_ptr c;
	jvirt_barray_ptr *coef;
	JBLOCKARRAY rows[3];		/* this row of MCUs' blocks, each component's */
	long mx, my, mcols, mrows;	/* the next block's MCU, and how many */
	int b, nb, hy, vy;		/* the block in it, of nb (hy x vy of luma) */
	int bi, bn;			/* the words of the block being sent */
	unsigned short bw[65];
} DSPSRC;

/* the next block's words, into bw */
static void dsp_block(DSPSRC *s)
{
	JCOEFPTR q;
	int ci, bx, by, k, n;
	s->bi = 0;
	s->bn = 1;
	s->bw[0] = 0;				/* (past the last: 0s) */
	if (s->my >= s->mrows)
		return;
	if (s->b == 0 && s->mx == 0)
		for (ci = 0; ci < (s->nb == 1 ? 1 : 3); ci++) {
			int v = ci == 0 ? s->vy : 1;
			s->rows[ci] = (*s->c->mem->access_virt_barray)((j_common_ptr) s->c, s->coef[ci],
								      (JDIMENSION) (s->my * v), (JDIMENSION) v, FALSE);
		}
	if (s->b < s->hy * s->vy) {
		ci = 0;
		bx = (int) (s->mx * s->hy) + s->b % s->hy;
		by = s->b / s->hy;
	} else {
		ci = s->b - s->hy * s->vy + 1;
		bx = (int) s->mx;
		by = 0;
	}
	q = s->rows[ci][by][bx];
	for (k = 63; k > 0 && q[jpeg_natural_order[k]] == 0; k--)
		;
	n = k + 1;
	s->bw[0] = (unsigned short) n;
	for (k = 0; k < n; k++)
		s->bw[1 + k] = (unsigned short) q[jpeg_natural_order[k]];
	s->bn = n + 1;
	if (++s->b == s->nb) {
		s->b = 0;
		if (++s->mx == s->mcols) {
			s->mx = 0;
			s->my++;
		}
	}
}

static int dsp_byte(DSPSRC *s)
{
	for (;;) {
		if (s->stop || s->p >= s->end) {
			s->stop = 1;
			return 0;
		}
		if (*s->p != 0xFF)
			return *s->p++;
		if (s->p + 1 >= s->end) {
			s->stop = 1;
			return 0;
		}
		if (s->p[1] == 0x00) {
			s->p += 2;
			return 0xFF;
		}
		if (s->p[1] == 0xFF) {		/* fill */
			s->p++;
			continue;
		}
		s->stop = 1;			/* a marker */
		return 0;
	}
}

/* 256 words the DSP asked for; restart: from after the next RST marker */
static void dsp_feed(DSPSRC *s, int restart)
{
	static unsigned short w[256];
	int i;
	if (s->coef != NULL) {
		for (i = 0; i < 256; i++) {
			if (s->bi == s->bn)
				dsp_block(s);
			w[i] = s->bw[s->bi++];
		}
		pic_dsp->put16(w, 256);
		return;
	}
	if (restart) {
		/* to the next RSTn, past it (as libjpeg, given a good file) */
		while (s->p + 1 < s->end && !(s->p[0] == 0xFF && s->p[1] >= 0xD0 && s->p[1] <= 0xD7))
			s->p++;
		s->p += 2;
		s->stop = s->p > s->end;
	}
	for (i = 0; i < 256; i++) {
		const unsigned char *q = s->p;
		unsigned b0;
		if (!s->stop && q + 1 < s->end && q[0] != 0xFF && q[1] != 0xFF) {
			w[i] = (unsigned short) (q[0] << 8 | q[1]);	/* the most */
			s->p = q + 2;
			continue;
		}
		b0 = (unsigned) dsp_byte(s);
		w[i] = (unsigned short) (b0 << 8 | (unsigned) dsp_byte(s));
	}
	pic_dsp->put16(w, 256);
}

/* the DSP shrinking the picture too: each source pixel in the box of the
   pixel shown it falls in (the scaler's centre mapping), each pixel shown
   the mean of its box, rounded as the scaler's 16-bit way does: the same
   pixels, with only those shown coming back. jpeg.a56's tables (at
   D_SHRINK, in the order of the variables V_SCM to V_SMR), and for the
   68030 each MCU's rectangle of pixels shown: from (xa, ya), ce x re of
   them (the last column or row of boxes may go on into the next MCU) */
typedef struct {
	short *xa, *ce, *ya, *re;
	unsigned long *t;
	long at[7], n;			/* each table's place in t; t's length */
} DSPSHR;

static void dsp_shrink_free(DSPSHR *s)
{
	free(s->xa);
	free(s->ya);
	free(s->t);
	memset(s, 0, sizeof *s);
}

/* the tables, or 0: the picture isn't shrunk (or its boxes are over 16
   pixels, or the tables over 4096 words) */
static int dsp_shrink_plan(DSPSHR *s, long ow, long oh, int mw, int mh, long nmx, long nmy, long w, long h)
{
	long x, y, i, maxw = 0, maxh = 0;
	short *cx = NULL, *cy = NULL;
	unsigned long *t;
	memset(s, 0, sizeof *s);
	s->at[0] = 0;				/* SCM: each source column's box */
	s->at[1] = s->at[0] + ow;		/* SRM: each source row's, x 16 */
	s->at[2] = s->at[1] + oh;		/* SWC: each column shown's width - 1, x 16 */
	s->at[3] = s->at[2] + w;		/* SWR: each row shown's height - 1 */
	s->at[4] = s->at[3] + h;		/* SCR: carried down (the DSP clears it) */
	s->at[5] = s->at[4] + 2 * w;		/* SMC: each MCU column's */
	s->at[6] = s->at[5] + 3 * nmx;		/* SMR: each MCU row's */
	s->n = s->at[6] + 3 * nmy;
	if (ow < w || oh < h || (ow == w && oh == h) || s->n > 4096)
		return 0;
	s->t = t = calloc((size_t) s->n, sizeof *t);
	s->xa = malloc((size_t) nmx * 2 * sizeof *s->xa);
	s->ya = malloc((size_t) nmy * 2 * sizeof *s->ya);
	cx = malloc((size_t) ow * sizeof *cx);
	cy = malloc((size_t) oh * sizeof *cy);
	if (t == NULL || s->xa == NULL || s->ya == NULL || cx == NULL || cy == NULL)
		goto no;
	s->ce = s->xa + nmx;
	s->re = s->ya + nmy;
	for (x = 0; x < ow; x++) {
		cx[x] = (short) centre(x, ow, w);
		t[s->at[2] + cx[x]] += 16;
	}
	for (y = 0; y < oh; y++) {
		cy[y] = (short) centre(y, oh, h);
		t[s->at[3] + cy[y]]++;
	}
	for (i = 0; i < w; i++) {
		maxw = (long) t[s->at[2] + i] > maxw ? (long) t[s->at[2] + i] : maxw;
		t[s->at[2] + i] -= 16;
	}
	for (i = 0; i < h; i++) {
		maxh = (long) t[s->at[3] + i] > maxh ? (long) t[s->at[3] + i] : maxh;
		t[s->at[3] + i]--;
	}
	if ((maxw / 16) * maxh > 16)
		goto no;
	for (x = 0; x < ow; x++)
		t[s->at[0] + x] = (unsigned long) (cx[x] - cx[x - x % mw]);
	for (y = 0; y < oh; y++)
		t[s->at[1] + y] = (unsigned long) (cy[y] - cy[y - y % mh]) * 16;
	for (i = 0; i < nmx; i++) {
		long x0 = i * mw, x1 = (x0 + mw < ow ? x0 + mw : ow) - 1;
		int out = x1 + 1 < ow && cx[x1 + 1] == cx[x1];
		unsigned long *e = t + s->at[5] + 3 * i;
		e[0] = (unsigned long) cx[x0];
		e[1] = (unsigned long) (cx[x1] - cx[x0] + 1);
		e[2] = (unsigned long) out;
		s->xa[i] = cx[x0];
		s->ce[i] = (short) (e[1] - (unsigned long) out);
	}
	for (i = 0; i < nmy; i++) {
		long y0 = i * mh, y1 = (y0 + mh < oh ? y0 + mh : oh) - 1;
		int out = y1 + 1 < oh && cy[y1 + 1] == cy[y1];
		unsigned long *e = t + s->at[6] + 3 * i;
		e[0] = (unsigned long) cy[y0];
		e[1] = (unsigned long) (cy[y1] - cy[y0] + 1);
		e[2] = (unsigned long) out;
		s->ya[i] = cy[y0];
		s->re[i] = (short) (e[1] - (unsigned long) out);
	}
	free(cx);
	free(cy);
	return 1;
no:
	free(cx);
	free(cy);
	dsp_shrink_free(s);
	return 0;
}

/* the sums' tables (R << 12 in X; G, B in Y: a pixel's X R << 12 + G,
   its Y B) and, 16 on (jpeg.a56 reads
   each 9 as a ring), the bits shown, 5-6-5, for shrinking: the constants
   of dsp_565's kind */
static const unsigned long dsp_sums[25] = {
	0xFF0000, 0x040000, 0x080000, 0xFF0000, 0x000040, 0x000080, 0xFF0000, 0x000040, 0x000080,
	0, 0, 0, 0, 0, 0, 0,
	0xF80000, 0x004000, 0x008000, 0xFC0000, 0x000200, 0x000400, 0xF80000, 0x000008, 0x000010
};

/* an MCU's pixels from the DSP, into the picture (5-6-5) or the band of
   R G B rows, after the 256-word lots it asks for */
static void dsp_read(LJ *j, PIC *p, unsigned char *band, DSPSRC *src, long x, long y, int cols, int rows)
{
	unsigned long h;
	for (;;) {
		if (band == NULL)
			h = pic_dsp->get16(p->px + y * p->w + x, cols, rows, p->w);
		else
			h = pic_dsp->get24(band + x * 3, cols, rows, (long) j->ci.output_width * 3);
		if (h == 0 || pic_dsp_failed)
			return;
		if (h != 1 && h != 2) {
			pic_dsp_failed = 1;
			return;
		}
		dsp_feed(src, h == 2);
	}
}

/* 1 decoded, 0 not (why), -1 the DSP stopped answering, -2 it couldn't
   be had */
static int decode_dsp(LJ *j, PIC *p, SCALER *sc, int k, int *why, jvirt_barray_ptr *coef)
{
	j_decompress_ptr c = &j->ci;
	int grey = c->num_components == 1, direct = c->out_color_space == JCS_RGB565;
	int hy = grey ? 1 : c->comp_info[0].h_samp_factor, vy = grey ? 1 : c->comp_info[0].v_samp_factor;
	int nb = coef == NULL ? c->blocks_in_MCU : hy * vy + (grey ? 0 : 2);
	int mw = hy * k, mh = vy * k, b, i, ci, seen = 0, ok = 0, ft[3];
	long ow = (long) c->output_width, oh = (long) c->output_height, mx, my;
	/* MCUs across and down: the scan's, or (coefficients, read from
	   every scan) the frame's */
	long mcols = coef == NULL ? (long) c->MCUs_per_row : ((long) c->image_width + 8 * hy - 1) / (8 * hy);
	long mrows = coef == NULL ? (long) c->MCU_rows_in_scan : ((long) c->image_height + 8 * vy - 1) / (8 * vy);
	unsigned long w[64 * 3];
	unsigned char *band = NULL;
	DSPSRC src;
	DSPSHR shr;
	int shrink = !direct && pic_dsp_shrink && dsp_shrink_plan(&shr, ow, oh, mw, mh, mcols, mrows, p->w, p->h);

	if (!direct && !shrink) {
		band = malloc((size_t) ow * mh * 3);
		if (band == NULL || !sc_init(sc, p, ow, oh)) {
			free(band);
			*why = PIC_MEMORY;
			return 0;
		}
	}
	if (!pic_dsp->begin(PIC_DSP_JPEG)) {
		free(band);
		if (shrink)
			dsp_shrink_free(&shr);
		return -2;
	}
	j->dsp_on = 1;
	pic_dsp_failed = 0;

	/* the setup: the variables (jpeg.a56's V_M to V_TB) */
	memset(w, 0, sizeof w);
	w[0x00] = (unsigned long) k;
	w[0x01] = (unsigned long) nb;
	w[0x02] = (unsigned long) mw;
	w[0x03] = (unsigned long) hy;
	w[0x04] = (unsigned long) vy;
	w[0x05] = grey ? 1 : 3;
	w[0x06] = direct ? 0 : 1;
	w[0x09] = (unsigned long) mcols;
	w[0x0a] = (unsigned long) mrows;
	w[0x0b] = (unsigned long) (ow - (mcols - 1) * mw);
	w[0x0c] = (unsigned long) mw * 3;
	w[0x0e] = (unsigned long) (oh - (mrows - 1) * mh);
	w[0x0f] = (unsigned long) mh;
	w[0x13] = coef == NULL ? (unsigned long) c->restart_interval : 0;
	/* the fast AC tables jpeg.a56 makes: Cr's is Cb's when they share
	   their Huffman table, as they mostly do */
	ft[0] = 0;
	ft[1] = 1;
	ft[2] = grey || c->comp_info[2].ac_tbl_no != c->comp_info[1].ac_tbl_no ? 2 : 1;
	w[0x2b] = grey ? 1 : (unsigned long) ft[2] + 1;
	w[0x2c] = coef != NULL ? 2 : 0;		/* V_SCALE: the coefficients come */
	if (shrink) {
		/* V_SCALE, the tables' places, the width shown */
		w[0x2c] |= 1;
		for (i = 0; i < 7; i++)
			w[0x2d + i] = (unsigned long) (D_SHRINK + shr.at[i]);
		w[0x34] = (unsigned long) p->w;
	}
	dsp_load(D_LOADX, D_VARS, w, 0x35);
	/* each block's quantisers, where its pixels go, the pitch, the bias,
	   its Huffman tables and DC predictor */
	for (b = 0; b < nb; b++) {
		unsigned long *e = w + 8 * b;
		ci = coef != NULL ? (b < hy * vy ? 0 : b - hy * vy + 1) :
		     c->cur_comp_info[c->MCU_membership[b]]->component_index;
		e[0] = (unsigned long) (D_QT + 64 * ci);
		if (ci == 0) {
			e[1] = (unsigned long) (D_YPL + (seen / hy) * k * mw + (seen % hy) * k);
			e[2] = (unsigned long) (mw - k);
			e[3] = 0x4000;		/* the DC + 4: each pixel + 1/2, rounded */
			seen++;
		} else {
			e[1] = ci == 1 ? D_CBP : D_CRP;
			e[2] = 0;
			e[3] = 0;
		}
		e[4] = (unsigned long) (D_HLA + 0x200 * ci);
		e[5] = (unsigned long) (D_HLA + 0x100 + 0x200 * ci);
		e[6] = (unsigned long) (D_PRED + ci);
		e[7] = (unsigned long) (D_FT + 0x200 * ft[ci]);	/* its fast AC table */
	}
	dsp_load(D_LOADX, D_BT, w, 8L * nb);
	memset(w, 0, sizeof w);
	dsp_load(D_LOADX, D_BLK, w, 64);
	if (grey) {
		for (i = 0; i < k * k * 3; i += 3) {
			w[i] = D_TR + 384;
			w[i + 1] = D_TG + 384;
			w[i + 2] = D_TB + 384;
		}
		dsp_load(D_LOADX, D_TRM, w, (long) k * k * 3);
	}
	dsp_fixed();
	for (ci = 0; ci < (grey ? 1 : 3) && coef == NULL; ci++) {
		jpeg_component_info *cp = &c->comp_info[ci];
		dsp_huff(c->dc_huff_tbl_ptrs[cp->dc_tbl_no], D_HLA + 0x200 * ci);
		dsp_huff(c->ac_huff_tbl_ptrs[cp->ac_tbl_no], D_HLA + 0x100 + 0x200 * ci);
	}
	/* the transform, the quantisers (Q * 2048), the format's constants */
	dsp_matrix(w, k);
	dsp_load(D_LOADY, 0, w, 8L * k);
	for (ci = 0; ci < (grey ? 1 : 3); ci++) {
		const JQUANT_TBL *q = c->comp_info[ci].quant_table;
		for (i = 0; i < 64; i++) {
			unsigned long v = (unsigned long) q->quantval[i] * 2048;
			w[64 * ci + i] = v > 0x7FFFFFUL ? 0x7FFFFFUL : v;
		}
	}
	dsp_load(D_LOADY, D_QT, w, grey ? 64 : 3 * 64);
	if (shrink) {
		static unsigned long kt[256];
		/* each box's divisor: 65536 / n rounded (the scaler's), x 64; a
		   box's columns - 1 x 16 + its rows - 1 */
		for (i = 0; i < 256; i++) {
			long n = (long) (i / 16 + 1) * (i % 16 + 1);
			kt[i] = n > 16 ? 0 : n == 1 ? 65536UL * 64 : (unsigned long) ((65536L + n / 2) / n) * 64;
		}
		dsp_load(D_LOADY, D_KT, kt, 256);
		dsp_load(D_LOADY, D_SHRINK, shr.t, shr.at[4]);
		dsp_load(D_LOADY, (unsigned) (D_SHRINK + shr.at[5]), shr.t + shr.at[5], shr.n - shr.at[5]);
		dsp_load(D_LOADY, D_CONST, dsp_sums, 25);
	} else {
		dsp_load(D_LOADY, D_CONST, direct ? dsp_565 : dsp_rgb, 9);
	}

	/* the scan, from where libjpeg's headers end */
	memset(&src, 0, sizeof src);
	src.p = j->src.next_input_byte;
	src.end = src.p + j->src.bytes_in_buffer;
	src.c = c;
	src.coef = coef;
	src.mcols = mcols;
	src.mrows = mrows;
	src.nb = nb;
	src.hy = hy;
	src.vy = vy;
	w[0] = D_DECODE;
	pic_dsp->put(w, 1);
	for (my = 0; my < mrows && !pic_dsp_failed; my++) {
		int rows = (int) (oh - my * mh < mh ? oh - my * mh : mh);
		for (mx = 0; mx < mcols && !pic_dsp_failed; mx++)
			if (shrink)
				dsp_read(j, p, NULL, &src, shr.xa[mx], shr.ya[my], shr.ce[mx], shr.re[my]);
			else
				dsp_read(j, p, band, &src, mx * mw, my * mh,
					 (int) (ow - mx * mw < mw ? ow - mx * mw : mw), rows);
		if (band != NULL && !pic_dsp_failed) {
			for (i = 0; i < rows; i++)
				if (!sc_row_rgb(sc, band + (long) i * ow * 3)) {
					*why = PIC_STOPPED;
					goto out;
				}
		} else if (pic_poll != NULL && !pic_poll()) {
			*why = PIC_STOPPED;
			goto out;
		}
	}
	ok = 1;
out:
	pic_dsp->end(ok && !pic_dsp_failed);
	j->dsp_on = 0;
	free(band);
	if (shrink)
		dsp_shrink_free(&shr);
	return pic_dsp_failed ? -1 : ok;
}

/* the check, the first time: a known picture (32 x 24, smooth colours,
   4:2:0) decoded on the DSP and on the 68030, at its size (5-6-5 from the
   DSP) and at 10 x 7 (from 3/8 by the scaler): the same within a step or
   two of 5-6-5 (the DSP repeats the chroma where libjpeg's 68030 way
   blends it, and its transform is exact) */
static const unsigned char dsp_test_jpg[] = {
	0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03,
	0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x04, 0x05, 0x08, 0x05, 0x05, 0x04, 0x04, 0x05, 0x0a, 0x07,
	0x07, 0x06, 0x08, 0x0c, 0x0a, 0x0c, 0x0c, 0x0b, 0x0a, 0x0b, 0x0b, 0x0d, 0x0e, 0x12, 0x10, 0x0d,
	0x0e, 0x11, 0x0e, 0x0b, 0x0b, 0x10, 0x16, 0x10, 0x11, 0x13, 0x14, 0x15, 0x15, 0x15, 0x0c, 0x0f,
	0x17, 0x18, 0x16, 0x14, 0x18, 0x12, 0x14, 0x15, 0x14, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x03, 0x04,
	0x04, 0x05, 0x04, 0x05, 0x09, 0x05, 0x05, 0x09, 0x14, 0x0d, 0x0b, 0x0d, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0xc0,
	0x00, 0x11, 0x08, 0x00, 0x18, 0x00, 0x20, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11,
	0x01, 0xff, 0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05,
	0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21,
	0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23,
	0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
	0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
	0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
	0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1,
	0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00,
	0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00,
	0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13,
	0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15,
	0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
	0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
	0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4,
	0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
	0xfa, 0xff, 0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xfa,
	0x97, 0x47, 0xb5, 0x8e, 0xe6, 0x35, 0xe0, 0x1c, 0xd6, 0xac, 0xde, 0x19, 0x8e, 0x78, 0xf8, 0x41,
	0xd3, 0xd2, 0xb8, 0x7f, 0x08, 0x78, 0x95, 0x24, 0x54, 0xcb, 0x57, 0xa8, 0xe9, 0x5a, 0x94, 0x73,
	0xc6, 0x39, 0x07, 0x22, 0xbf, 0x9b, 0xe1, 0x83, 0xad, 0x87, 0x9e, 0x88, 0xf4, 0x70, 0xf9, 0x4d,
	0x4c, 0xb3, 0x5b, 0x6c, 0x79, 0xd6, 0xbf, 0xe0, 0x85, 0x65, 0x6c, 0x47, 0xfa, 0x57, 0x92, 0xf8,
	0xb3, 0xc1, 0x7e, 0x5e, 0xfc, 0x27, 0xe9, 0x5f, 0x54, 0x5e, 0xdb, 0xc5, 0x71, 0x19, 0xe0, 0x1a,
	0xf3, 0xff, 0x00, 0x14, 0xf8, 0x7d, 0x25, 0x57, 0xc2, 0xd7, 0xeb, 0x5c, 0x3b, 0x8c, 0xa9, 0x4e,
	0x51, 0x52, 0x3e, 0x9e, 0x8f, 0x15, 0xcb, 0x00, 0xb9, 0x5c, 0x8f, 0x9e, 0x7c, 0x27, 0xe3, 0x13,
	0x0e, 0xc0, 0x5f, 0x1f, 0x8d, 0x7a, 0xff, 0x00, 0x87, 0x3c, 0x74, 0xbb, 0x53, 0x32, 0x7e, 0xb4,
	0x51, 0x5e, 0xbd, 0x6c, 0xbf, 0x0e, 0xf5, 0x71, 0x3f, 0x58, 0xe2, 0x2c, 0x0d, 0x08, 0x53, 0x6d,
	0x44, 0xee, 0xac, 0x7c, 0x5d, 0x1c, 0xe8, 0x32, 0xe0, 0xfe, 0x34, 0xb7, 0xba, 0x8c, 0x77, 0x48,
	0x79, 0x06, 0x8a, 0x2b, 0x7c, 0x1e, 0x0e, 0x95, 0x39, 0xae, 0x54, 0x7f, 0x1b, 0xf1, 0x65, 0x49,
	0x51, 0x94, 0xb9, 0x1d, 0x8f, 0xff, 0xd9
};

static int dsp_close(const PIC *a, const PIC *b)
{
	long i, n = (long) a->w * a->h, sum = 0;
	if (a->w != b->w || a->h != b->h)
		return 0;
	for (i = 0; i < n; i++) {
		int d1 = ((a->px[i] >> 11) & 31) - ((b->px[i] >> 11) & 31);
		int d2 = ((a->px[i] >> 5) & 63) - ((b->px[i] >> 5) & 63);
		int d3 = (a->px[i] & 31) - (b->px[i] & 31);
		d1 = d1 < 0 ? -d1 : d1;
		d2 = d2 < 0 ? -d2 : d2;
		d3 = d3 < 0 ? -d3 : d3;
		if (d1 > 3 || d2 > 6 || d3 > 3)
			return 0;
		sum += d1 + d2 / 2 + d3;
	}
	return sum <= n * 3;		/* a step in each component at most, on the whole */
}

static int dsp_check(void)
{
	static const short size[2][2] = { { 32, 24 }, { 10, 7 } };
	int s, ok = 1, why;
	for (s = 0; s < 2 && ok; s++) {
		PIC *a, *b;
		dsp_testing = 1;
		a = pic_decode(dsp_test_jpg, (long) sizeof dsp_test_jpg, size[s][0], size[s][1], 1024, &why);
		dsp_testing = 2;
		b = pic_decode(dsp_test_jpg, (long) sizeof dsp_test_jpg, size[s][0], size[s][1], 1024, &why);
		ok = a != NULL && b != NULL && dsp_close(a, b);
		pic_free(a);
		pic_free(b);
	}
	dsp_testing = 0;
	return ok;
}

static int dsp_usable(void)
{
	if (pic_dsp == NULL || dsp_state < 0 || dsp_testing == 2)
		return 0;
	if (dsp_testing == 1 || dsp_state > 0)
		return 1;
	dsp_state = dsp_check() ? 1 : -1;
	return dsp_state > 0;
}

const char *pic_dsp_state(void)
{
	return pic_dsp == NULL || dsp_state < 0 ? "68030" : dsp_state > 0 ? "DSP" : "-";
}

static int decode_lj(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why, int may)
{
	LJ *j;
	long y, x;
	int k, ok = 0, dsp = may && dsp_usable();
	j = calloc(1, sizeof *j);
	if (j == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	j->ci.err = lj_errors(&j->err.pub);
	if (__builtin_setjmp(j->err.jmp)) {
		/* libjpeg gave up: damaged, or out of memory */
		if (j->dsp_on)
			pic_dsp->end(0);
		*why = j->stopped ? PIC_STOPPED : j->err.pub.msg_code == JERR_OUT_OF_MEMORY ? PIC_MEMORY :
		       (j->err.pub.msg_code == JERR_BAD_PRECISION || j->err.pub.msg_code == JERR_NOT_COMPILED) ?
		       PIC_UNSUPPORTED : PIC_DAMAGED;
		goto done;
	}
	jpeg_create_decompress(&j->ci);
	j->prog.progress_monitor = lj_progress;
	j->ci.progress = &j->prog;
	j->src.init_source = src_init;
	j->src.fill_input_buffer = src_fill;
	j->src.skip_input_data = src_skip;
	j->src.resync_to_restart = jpeg_resync_to_restart;
	j->src.term_source = src_term;
	j->src.next_input_byte = d;
	j->src.bytes_in_buffer = (size_t) n;
	j->ci.src = &j->src;
	jpeg_read_header(&j->ci, TRUE);
	/* CMYK (Adobe's, inverted) is made RGB below; the rest by libjpeg */
	j->ci.out_color_space = j->ci.jpeg_color_space == JCS_CMYK || j->ci.jpeg_color_space == JCS_YCCK ?
				JCS_CMYK : JCS_RGB;
	/* n/8 of the size: the least n still as big as shown */
	for (k = 1; k < 8; k++)
		if (((long) j->ci.image_width * k + 7) / 8 >= p->w && ((long) j->ci.image_height * k + 7) / 8 >= p->h)
			break;
	j->ci.scale_num = (unsigned) k;
	j->ci.scale_denom = 8;
	/* the integer transform made of fewer multiplies (13% fewer
	   instructions for the whole picture on the 68030, a quality step
	   no one sees: 38.2 dB against the exact one's 39.2, both against a
	   decode in floating point, the 5-6-5 pixels' own loss included) */
	j->ci.dct_method = JDCT_IFAST;
	jpeg_calc_output_dimensions(&j->ci);
	if (j->ci.out_color_space == JCS_RGB && (long) j->ci.output_width == p->w &&
	    (long) j->ci.output_height == p->h) {
		/* at the size shown: libjpeg writes the 5-6-5 pixels into the
		   picture, as put() makes them (cut, not dithered) */
		j->ci.out_color_space = JCS_RGB565;
		j->ci.dither_mode = JDITHER_NONE;
	}
	p->how = PIC_JPEG;
	p->scale = k;
	if (dsp && dsp_takes_coef(&j->ci, k)) {
		/* progressive: libjpeg reads every scan into coefficients, the
		   DSP does the rest from them; if it can't be had, the whole
		   picture again on the 68030 (decode_jpeg) */
		jvirt_barray_ptr *coef = jpeg_read_coefficients(&j->ci);
		ok = decode_dsp(j, p, sc, k, why, coef);
		if (ok < 0 && ok != -2 && dsp_testing == 0)
			dsp_state = -1;
		jpeg_abort_decompress(&j->ci);
		if (ok == -2)
			ok = -1;
		goto done;
	}
	jpeg_start_decompress(&j->ci);
	if (dsp && dsp_takes(&j->ci, k)) {
		ok = decode_dsp(j, p, sc, k, why, NULL);
		if (ok != -2) {
			/* -1: the DSP stopped answering; from now on the 68030
			   does them, this one again (decode_jpeg) */
			if (ok < 0 && dsp_testing == 0)
				dsp_state = -1;
			jpeg_abort_decompress(&j->ci);
			goto done;
		}
		ok = 0;			/* the DSP couldn't be had: the 68030 this time */
	}
	if (j->ci.out_color_space == JCS_RGB565) {
		for (y = 0; y < (long) j->ci.output_height; y++) {
			JSAMPROW r = (JSAMPROW) (p->px + y * p->w);
			if (pic_poll != NULL && (y & 15) == 15 && !pic_poll()) {
				*why = PIC_STOPPED;
				jpeg_abort_decompress(&j->ci);
				goto done;
			}
			jpeg_read_scanlines(&j->ci, &r, 1);
		}
		jpeg_finish_decompress(&j->ci);
		ok = 1;
		goto done;
	}
	j->row = malloc((size_t) j->ci.output_width * (size_t) j->ci.output_components);
	if (j->row == NULL || !sc_init(sc, p, j->ci.output_width, j->ci.output_height)) {
		*why = PIC_MEMORY;
		jpeg_abort_decompress(&j->ci);
		goto done;
	}
	for (y = 0; y < (long) j->ci.output_height; y++) {
		JSAMPROW r = j->row;
		jpeg_read_scanlines(&j->ci, &r, 1);
		if (j->ci.out_color_space == JCS_CMYK) {
			/* Adobe writes CMYK inverted: each component times K,
			   made R G B in place */
			const unsigned char *s = j->row;
			unsigned char *o = j->row;
			for (x = 0; x < (long) j->ci.output_width; x++, s += 4, o += 3) {
				unsigned k4 = s[3];
				o[0] = (unsigned char) (s[0] * k4 / 255);
				o[1] = (unsigned char) (s[1] * k4 / 255);
				o[2] = (unsigned char) (s[2] * k4 / 255);
			}
		}
		if (!sc_row_rgb(sc, j->row)) {
			*why = PIC_STOPPED;
			jpeg_abort_decompress(&j->ci);
			goto done;
		}
	}
	jpeg_finish_decompress(&j->ci);
	ok = 1;
done:
	jpeg_destroy_decompress(&j->ci);
	free(j->row);
	free(j);
	return ok;
}

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	int ok = decode_lj(d, n, p, sc, why, 1);
	if (ok < 0) {
		/* the DSP stopped answering, or couldn't be had for a
		   progressive one: again on the 68030 */
		sc_free(sc);
		memset(sc, 0, sizeof *sc);
		*why = PIC_OK;
		ok = decode_lj(d, n, p, sc, why, 0);
	}
	return ok;
}

#else

const PIC_DSP *pic_dsp;			/* (no DSP for TJpgDec's builds) */
int pic_dsp_failed;
int pic_dsp_shrink = 1;
int pic_dsp_dither = 1;

const char *pic_dsp_state(void)
{
	return "68000";
}

const char *pic_dither_state(void)
{
	return "68000";
}

/* TJpgDec (third_party/tjpgd), for the family's programs on a 68000:
   small, and quick at 1/8 (no IDCT); the kinds it won't take from their
   blocks' averages (jpegdc.c) */

typedef struct {
	const unsigned char *p;
	long left;
	SCALER *sc;
	unsigned char *strip;		/* an MCU row of the unpacked picture, R G B */
	long sw;
	int rows;			/* in the strip */
	int stopped;
} JPG;

static size_t jin(JDEC *jd, uint8_t *buf, size_t n)
{
	JPG *j = jd->device;
	if ((long) n > j->left)
		n = (size_t) j->left;
	if (buf != NULL)
		memcpy(buf, j->p, n);
	j->p += n;
	j->left -= (long) n;
	return n;
}

static int jout(JDEC *jd, void *bitmap, JRECT *r)
{
	JPG *j = jd->device;
	const unsigned char *px = bitmap;
	long y, w = r->right - r->left + 1;
	for (y = 0; y <= r->bottom - r->top; y++, px += w * 3)
		memcpy(j->strip + (mul16((unsigned short) y, (unsigned short) j->sw) + r->left) * 3, px, (size_t) w * 3);
	j->rows = r->bottom - r->top + 1;
	if (r->right + 1 >= j->sw) {
		/* the row of blocks is whole */
		for (y = 0; y < j->rows; y++)
			if (!sc_row_rgb(j->sc, j->strip + mul16((unsigned short) y, (unsigned short) j->sw) * 3)) {
				j->stopped = 1;
				return 0;
			}
	}
	return 1;
}

/* the unpacked size at a scale, as TJpgDec makes it (each block row and
   column rounded down) */
static long scaled(long size, int mcu, int scale)
{
	long whole = size / mcu;
	return whole * (mcu >> scale) + ((size - whole * mcu) >> scale);
}

/* jpegdc's polls, through pic_poll: whether it stopped */
static int jdc_stopped;
static int jdc_pic_poll(void)
{
	if (pic_poll != NULL && !pic_poll()) {
		jdc_stopped = 1;
		return 0;
	}
	return 1;
}

#define POOL 11512L			/* TJpgDec's workspace with its Huffman tables, and the
					   folding multipliers of 1/2 and 1/4 (4 x 128 bytes at most) */

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	JDEC jd;
	JPG j;
	void *pool = malloc(POOL);
	int scale = 0, rc;
	long sw, sh;
	memset(&j, 0, sizeof j);
	j.p = d;
	j.left = n;
	if (pool == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	rc = jd_prepare(&jd, jin, pool, (size_t) POOL, &j);
	if (rc == JDR_OK) {
		/* as small as it can be unpacked and still be as big as shown */
		while (scale < 3 && (jd.width >> (scale + 1)) >= p->w && (jd.height >> (scale + 1)) >= p->h)
			scale++;
		sw = scaled(jd.width, jd.msx * 8, scale);
		sh = scaled(jd.height, jd.msy * 8, scale);
		j.sw = sw;
		j.strip = malloc((size_t) sw * 16 * 3);
		if (j.strip == NULL || !sc_init(sc, p, sw, sh)) {
			free(j.strip);
			free(pool);
			*why = PIC_MEMORY;
			return 0;
		}
		j.sc = sc;
		p->how = PIC_JPEG;
		p->scale = scale;
		rc = jd_decomp(&jd, jout, (uint8_t) scale);
		free(j.strip);
		free(pool);
		if (rc == JDR_OK || (rc == JDR_INP && sc->y > 0)) {
			/* a picture cut short: the rest stays clear */
			unsigned char *row = sc->y < sh ? calloc((size_t) sw, 4) : NULL;
			if (row != NULL) {
				while (sc->y < sh)
					if (!sc_row(sc, row))
						break;
				free(row);
			}
			return 1;
		}
		if (j.stopped) {
			*why = PIC_STOPPED;
			return 0;
		}
		sc_free(sc);
		memset(sc, 0, sizeof *sc);
		/* damaged as TJpgDec sees it: the averages may still be read */
	} else {
		free(pool);
	}
	{
		/* progressive, SOF1, other samplings, CMYK: each 8x8 block's
		   average, an eighth of the size */
		int jw, jh, jwhy;
		int (*was)(void) = jdc_poll;
		long bw, bh, bx, by;
		unsigned char *row;
		JDC *c = jdc_open(d, n, &jw, &jh, &jwhy);
		if (c == NULL) {
			*why = jwhy == JW_MEMORY ? PIC_MEMORY : jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_stopped = 0;
		jdc_poll = jdc_pic_poll;
		if (!jdc_read(c)) {
			jdc_poll = was;
			jwhy = jdc_why(c);
			jdc_free(c);
			*why = jdc_stopped ? PIC_STOPPED : jwhy == JW_MEMORY ? PIC_MEMORY :
			       jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_poll = was;
		bw = (jw + 7) / 8;
		bh = (jh + 7) / 8;
		row = malloc((size_t) bw * 3);
		if (row == NULL || !sc_init(sc, p, bw, bh)) {
			free(row);
			jdc_free(c);
			*why = PIC_MEMORY;
			return 0;
		}
		p->how = PIC_JPEG_AVG;
		p->scale = 3;
		for (by = 0; by < bh; by++) {
			for (bx = 0; bx < bw; bx++) {
				int r, g, b;
				jdc_rgb(c, (int) bx, (int) by, &r, &g, &b);
				row[bx * 3] = (unsigned char) r;
				row[bx * 3 + 1] = (unsigned char) g;
				row[bx * 3 + 2] = (unsigned char) b;
			}
			if (!sc_row_rgb(sc, row)) {
				free(row);
				jdc_free(c);
				*why = PIC_STOPPED;
				return 0;
			}
		}
		free(row);
		jdc_free(c);
		return 1;
	}
}

#endif

/* ---------------------------------------------------------------- PNG */

static int png_row(void *ud, long y, const unsigned char *rgba)
{
	(void) y;
	return sc_row(ud, rgba);
}

static int png_row_rgb(void *ud, long y, const unsigned char *rgb)
{
	(void) y;
	return sc_row_rgb(ud, rgb);
}

static int decode_png(const unsigned char *d, long n, PIC *p, SCALER *sc, long max_px, int *why)
{
	long w, h;
	int r;
	if (!png_size(d, n, &w, &h) || w <= 0 || h <= 0) {
		*why = PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, w, h)) {
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_PNG;
	r = png_decode_rgb(d, n, max_px, png_row, png_row_rgb, sc);
	switch (r) {
	case PNG_OK: return 1;
	case PNG_MEMORY: *why = PIC_MEMORY; break;
	case PNG_TOO_BIG: *why = PIC_TOO_BIG; break;
	case PNG_STOPPED: *why = PIC_STOPPED; break;
	case PNG_UNSUPPORTED: *why = PIC_UNSUPPORTED; break;
	default: *why = PIC_DAMAGED;
	}
	return 0;
}

/* ---------------------------------------------------------------- GIF */

typedef struct {
	long w, h;
	unsigned char px[1];		/* w * h * 4 */
} GBMP;

/* the most pixels a GIF's canvas may have (pic_decode's max_px): libnsgif
   makes it as big as its frames say, which a damaged file can make far
   bigger than its header (and w * h * 4 would wrap a 32-bit size_t: a
   small block, overrun; found by tests/pic's fuzzing) */
static long gb_max;
static int gb_big;		/* a canvas was refused for it */

static nsgif_bitmap_t *gb_create(int w, int h)
{
	GBMP *b;
	if (w <= 0 || h <= 0 || w > gb_max / h) {
		gb_big = 1;
		return NULL;
	}
	b = calloc(1, sizeof *b + (size_t) w * (size_t) h * 4);
	if (b != NULL) {
		b->w = w;
		b->h = h;
	}
	return b;
}

static void gb_destroy(nsgif_bitmap_t *b)
{
	free(b);
}

static uint8_t *gb_buffer(nsgif_bitmap_t *b)
{
	/* nsgif wants it on a 4-byte boundary: px follows two longs */
	return ((GBMP *) b)->px;
}

static int decode_gif(const unsigned char *d, long n, PIC *p, SCALER *sc, long max_px, int *why)
{
	static const nsgif_bitmap_cb_vt vt = { gb_create, gb_destroy, gb_buffer, NULL, NULL, NULL, NULL };
	nsgif_t *g;
	nsgif_bitmap_t *bmp;
	const nsgif_info_t *info;
	nsgif_error e;
	long y;
	gb_max = max_px;
	gb_big = 0;
	if (nsgif_create(&vt, NSGIF_BITMAP_FMT_R8G8B8A8, &g) != NSGIF_OK) {
		*why = PIC_MEMORY;
		return 0;
	}
	e = nsgif_data_scan(g, (size_t) n, d);
	nsgif_data_complete(g);
	info = nsgif_get_info(g);
	if (info == NULL || info->frame_count == 0) {
		nsgif_destroy(g);
		*why = gb_big ? PIC_TOO_BIG : e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	e = nsgif_frame_decode(g, 0, &bmp);
	if (e != NSGIF_OK || bmp == NULL) {
		nsgif_destroy(g);
		*why = gb_big ? PIC_TOO_BIG : e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, ((GBMP *) bmp)->w, ((GBMP *) bmp)->h)) {
		nsgif_destroy(g);
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_GIF;
	for (y = 0; y < ((GBMP *) bmp)->h; y++)
		if (!sc_row(sc, ((GBMP *) bmp)->px + y * ((GBMP *) bmp)->w * 4)) {
			nsgif_destroy(g);
			*why = PIC_STOPPED;
			return 0;
		}
	nsgif_destroy(g);
	return 1;
}

/* ---------------------------------------------------------------- WebP */

#ifdef PIC_LIBWEBP

/* a lossy picture's rows, as libwebp makes them (its VP8Io: a band at a
   time, each row its Y and the U and V of its pair of rows, and its
   alpha), each to the scaler as it comes: nothing of the picture is kept
   but the bands libwebp keeps for its filters, and its alpha */
typedef struct {
	SCALER *sc;
	unsigned char *row;		/* a row's R G B, or R G B A */
	int stopped;
} WPIO;

static int wp_put(const VP8Io *io)
{
	WPIO *c = (WPIO *) io->opaque;
	const uint8_t *y = io->y, *u = io->u, *v = io->v, *a = io->a;
	unsigned char *r = c->row;
	int j, x, w = io->mb_w, ok;
	for (j = 0; j < io->mb_h; j++) {
		/* (its chroma at half size, each value for two pixels across:
		   libwebp's simple sampler, as libjpeg's for a JPEG) */
		WebPSamplers[MODE_RGB](y, u, v, r, w);
		if (a != NULL) {
			/* R G B made R G B A where it is, from the end */
			for (x = w - 1; x >= 0; x--) {
				r[4 * x + 3] = a[x];
				r[4 * x + 2] = r[3 * x + 2];
				r[4 * x + 1] = r[3 * x + 1];
				r[4 * x] = r[3 * x];
			}
			ok = sc_row(c->sc, r);
			a += io->width;
		} else {
			ok = sc_row_rgb(c->sc, r);
		}
		if (!ok) {
			c->stopped = 1;
			return 0;
		}
		y += io->y_stride;
		if (j & 1) {
			u += io->uv_stride;
			v += io->uv_stride;
		}
	}
	return 1;
}

static int webp_why(VP8StatusCode s)
{
	return s == VP8_STATUS_OUT_OF_MEMORY ? PIC_MEMORY :
	       s == VP8_STATUS_UNSUPPORTED_FEATURE ? PIC_UNSUPPORTED : PIC_DAMAGED;
}

/* a lossless picture: libwebp's decoder keeps it whole as it goes (a
   pixel can repeat any before it), 4 bytes a pixel, and gives it whole,
   R G B A, 4 more; then its rows to the scaler */
static int webp_whole(const unsigned char *d, long n, SCALER *sc, int *why)
{
	WebPDecoderConfig cf;
	VP8StatusCode st;
	long y;
	if (!WebPInitDecoderConfig(&cf)) {
		*why = PIC_DAMAGED;
		return 0;
	}
	cf.output.colorspace = MODE_RGBA;
	st = WebPDecode(d, (size_t) n, &cf);
	if (st != VP8_STATUS_OK) {
		*why = webp_why(st);
		return 0;
	}
	for (y = 0; y < cf.output.height; y++)
		if (!sc_row(sc, cf.output.u.RGBA.rgba + y * cf.output.u.RGBA.stride)) {
			WebPFreeDecBuffer(&cf.output);
			*why = PIC_STOPPED;
			return 0;
		}
	WebPFreeDecBuffer(&cf.output);
	return 1;
}

static int decode_webp(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	WebPBitstreamFeatures f;
	WebPHeaderStructure hd;
	VP8StatusCode st;
	VP8Decoder *dec;
	VP8Io io;
	WPIO c;
	int ok;
	st = WebPGetFeatures(d, (size_t) n, &f);
	if (st != VP8_STATUS_OK || f.width <= 0 || f.height <= 0) {
		*why = st == VP8_STATUS_OK ? PIC_DAMAGED : webp_why(st);
		return 0;
	}
	if (f.has_animation) {
		*why = PIC_UNSUPPORTED;	/* (its first frame: not yet) */
		return 0;
	}
	if (!sc_init(sc, p, f.width, f.height)) {
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_WEBP;
	memset(&hd, 0, sizeof hd);
	hd.data = d;
	hd.data_size = (size_t) n;
	hd.have_all_data = 1;
	st = WebPParseHeaders(&hd);
	if (st != VP8_STATUS_OK) {
		*why = webp_why(st);
		return 0;
	}
	if (hd.is_lossless)
		return webp_whole(d, n, sc, why);
	memset(&c, 0, sizeof c);
	c.sc = sc;
	c.row = malloc((size_t) f.width * 4);
	dec = VP8New();
	if (c.row == NULL || dec == NULL || !VP8InitIo(&io)) {
		free(c.row);
		VP8Delete(dec);
		*why = PIC_MEMORY;
		return 0;
	}
	io.data = hd.data + hd.offset;
	io.data_size = hd.data_size - hd.offset;
	io.opaque = &c;
	io.put = wp_put;
	dec->alpha_data = hd.alpha_data;
	dec->alpha_data_size = hd.alpha_data_size;
	WebPInitSamplers();
	ok = VP8GetHeaders(dec, &io) && VP8Decode(dec, &io);
	if (!ok)
		*why = c.stopped ? PIC_STOPPED : webp_why(dec->status);
	VP8Delete(dec);
	free(c.row);
	return ok;
}

#endif

/* ---------------------------------------------------------------- decoding */

void pic_free(PIC *p)
{
	if (p == NULL)
		return;
	free(p->px);
	free(p->alpha);
	free(p);
}

PIC *pic_decode(const unsigned char *d, long n, int w, int h, long max_px, int *why)
{
	PIC *p;
	SCALER sc;
	long sw, sh;
	int kind = pic_kind(d, n), ok = 0, dummy;
	if (why == NULL)
		why = &dummy;
	*why = PIC_OK;
	if (kind != PIC_JPEG && kind != PIC_PNG && kind != PIC_GIF
#ifdef PIC_LIBWEBP
	    && kind != PIC_WEBP
#endif
	    ) {
		*why = PIC_UNSUPPORTED;
		return NULL;
	}
	if (!pic_size(d, n, &sw, &sh)) {
		*why = PIC_DAMAGED;
		return NULL;
	}
	if (sw > max_px / sh) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	if (w <= 0 || h <= 0) {
		w = (int) sw;
		h = (int) sh;
	}
	if (w > 0x7fff || h > 0x7fff || (long) w * h > max_px) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	p = calloc(1, sizeof *p);
	if (p == NULL) {
		*why = PIC_MEMORY;
		return NULL;
	}
	p->w = w;
	p->h = h;
	p->px = malloc((size_t) w * h * 2);
	p->alpha = malloc((size_t) w * h);
	memset(&sc, 0, sizeof sc);
	if (p->px == NULL || p->alpha == NULL) {
		*why = PIC_MEMORY;
	} else {
#ifdef PIC_LIBJPEG
		/* libjpeg gives every row (grey past the end of a JPEG cut
		   short), and its pixels are solid: nothing to clear (a 480 x
		   320 photo's 460 KB were 4% of its time on a Falcon) */
		if (kind != PIC_JPEG)
#endif
		{
			memset(p->alpha, 0, (size_t) w * h);
			memset(p->px, 0, (size_t) w * h * 2);
		}
		switch (kind) {
		case PIC_JPEG: ok = decode_jpeg(d, n, p, &sc, why); break;
		case PIC_PNG: ok = decode_png(d, n, p, &sc, max_px, why); break;
#ifdef PIC_LIBWEBP
		case PIC_WEBP: ok = decode_webp(d, n, p, &sc, why); break;
#endif
		default: ok = decode_gif(d, n, p, &sc, max_px, why); break;
		}
	}
	if (ok && sc.down_y && sc.y < sc.sh && sc.dy >= 0) {
		/* fewer rows came than the header said: the last one made */
		flush(&sc, sc.dy);
	}
	if (ok && !sc.any_alpha) {
		free(p->alpha);
		p->alpha = NULL;
	}
	sc_free(&sc);
	if (!ok) {
		pic_free(p);
		return NULL;
	}
	return p;
}

/* ---------------------------------------------------------------- the screen's colours */

void pic_pal_init(PIC_PAL *pal, int n, const unsigned char rgb[][3])
{
	int i, j;
	pal->n = n < 2 ? 2 : n > 256 ? 256 : n;
	memcpy(pal->rgb, rgb, (size_t) pal->n * 3);
	if (pal->n <= 4)
		pic_pal_free(pal);
	else if (pal->near == NULL)
		pal->near = calloc(32 * 32 * 32, 1);
	else
		memset(pal->near, 0, 32 * 32 * 32);
	/* the pens by their red, for nearest_far */
	for (i = 0; i < pal->n; i++) {
		for (j = i; j > 0 && pal->rgb[pal->order[j - 1]][0] > pal->rgb[i][0]; j--)
			pal->order[j] = pal->order[j - 1];
		pal->order[j] = (unsigned char) i;
	}
}


void pic_pal_free(PIC_PAL *pal)
{
	free(pal->near);
	pal->near = NULL;
}

/* a colour (each component -128..383) to its index in near[] */
#define NEAR_AT(r, g, b) ((((r) + 128) >> 4) * 1024 + (((g) + 128) >> 4) * 32 + (((b) + 128) >> 4))

/* a square: each side at most 640 apart, so a 16 x 16 multiply (a
   68000's muls.w; a long one was a call to GCC's 32-bit routine) */
#define SQ(v) ((long) (short) (v) * (short) (v))

static int nearest_far(PIC_PAL *pal, int r, int g, int b)
{
	int k = NEAR_AT(r, g, b), lo, hi, best = 0;
	long bd = 0x7fffffffL;
	if (pal->near != NULL && pal->near[k])
		return pal->near[k] - 1;
	/* the step's middle */
	r = (r & ~15) + 8;
	g = (g & ~15) + 8;
	b = (b & ~15) + 8;
	/* outwards from the pens of about this red (they are sorted by it),
	   until red alone puts a pen farther than the best: a few pens of
	   256 instead of all (the 256-colour modes spent most of a picture's
	   time here) */
	lo = 0;
	hi = pal->n;
	while (lo < hi) {
		int mid = (lo + hi) >> 1;
		if (pal->rgb[pal->order[mid]][0] < r)
			lo = mid + 1;
		else
			hi = mid;
	}
	hi = lo;
	lo = lo - 1;
	while (lo >= 0 || hi < pal->n) {
		int i, side, dr;
		long dd;
		if (hi < pal->n && (lo < 0 || pal->rgb[pal->order[hi]][0] - r <= r - pal->rgb[pal->order[lo]][0]))
			i = pal->order[hi++], side = 1;
		else
			i = pal->order[lo--], side = 0;
		dr = r - pal->rgb[i][0];
		if (3 * SQ(dr) >= bd) {
			/* every pen past this one on its side is farther still */
			if (side)
				hi = pal->n;
			else
				lo = -1;
			continue;
		}
		dd = 3 * SQ(dr) + 4 * SQ(g - pal->rgb[i][1]) + 2 * SQ(b - pal->rgb[i][2]);
		if (dd < bd) {
			bd = dd;
			best = i;
		}
	}
	if (pal->near != NULL)
		pal->near[k] = (unsigned char) (best + 1);
	return best;
}

int pic_pal_nearest(PIC_PAL *pal, int r, int g, int b)
{
	return nearest_far(pal, r, g, b);
}

/* two pens (a monochrome screen): Floyd-Steinberg on the brightness alone,
   from tables, in 16-bit arithmetic. The three colours' dither took 250 us
   a pixel on an 8 MHz ST (5.7 s for a 192 x 128 photo); brightness is all
   two pens can show */
static int dither_two(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	long w = p->w, y;
	short lr[32], lg[64], lb[32], y0, y1, mid, i;
	short *rows = calloc((size_t) (w + 2) * 2, sizeof *rows), *cur, *nxt;
	int light;
	/* each pen's brightness (0.299, 0.587, 0.114 in 256ths), and each
	   5-6-5 component's share of a pixel's */
	y0 = (short) ((77 * pal->rgb[0][0] + 150 * pal->rgb[0][1] + 29 * pal->rgb[0][2]) >> 8);
	y1 = (short) ((77 * pal->rgb[1][0] + 150 * pal->rgb[1][1] + 29 * pal->rgb[1][2]) >> 8);
	light = y0 >= y1 ? 0 : 1;
	if (light) {
		short t = y0;
		y0 = y1;
		y1 = t;
	}
	mid = (short) ((y0 + y1) >> 1);		/* y0 the light pen's, y1 the dark one's */
	for (i = 0; i < 32; i++) {
		int c = (i << 3) | (i >> 2);
		lr[i] = (short) ((77 * c) >> 8);
		lb[i] = (short) ((29 * c) >> 8);
	}
	for (i = 0; i < 64; i++)
		lg[i] = (short) ((150 * ((i << 2) | (i >> 4))) >> 8);
	if (rows == NULL) {
		long k;
		for (k = 0; k < (long) p->h * w; k++) {
			unsigned v = p->px[k];
			out[k] = (unsigned char) (lr[v >> 11] + lg[(v >> 5) & 63] + lb[v & 31] >= mid ? light : !light);
		}
		return 1;
	}
	cur = rows + 1;
	nxt = rows + w + 3;
	for (y = 0; y < p->h; y++) {
		const unsigned short *px = p->px + y * w;
		const unsigned char *al = p->alpha != NULL ? p->alpha + y * w : NULL;
		unsigned char *o = out + y * w;
		short e = 0, *c, *d;	/* e: 7/16 of the last pixel's error, in 16ths */
		long n;
		if (pic_poll != NULL && y > 0 && (y & 15) == 0 && !pic_poll()) {
			free(rows);
			return 0;
		}
		/* a pixel at s (its alpha a, or none), its pen to o; c its row's
		   errors, d the next row's, STEP the way along (serpentine): a
		   loop each way, so that the pointers step by addressing (a
		   68000's (a0)+ and -(a0)); GCC makes the weights shifts and adds */
#define TWO(STEP) do { \
			unsigned v = *s; \
			short want, q; \
			if (a != NULL && *a < 128) { \
				*o = 0; \
				e = 0; \
				break; \
			} \
			want = (short) (lr[v >> 11] + lg[(v >> 5) & 63] + lb[v & 31] + ((e + *c) >> 4)); \
			if (want >= mid) { \
				*o = (unsigned char) light; \
				q = want - y0; \
			} else { \
				*o = (unsigned char) !light; \
				q = want - y1; \
			} \
			e = (short) (q * 7); \
			d[-(STEP)] += (short) (q * 3); \
			d[0] += (short) (q * 5); \
			d[STEP] = q; \
		} while (0)
		if (!(y & 1)) {
			const unsigned short *s = px;
			const unsigned char *a = al;
			c = cur;
			d = nxt;
			for (n = w; n > 0; n--, s++, o++, c++, d++, a = a != NULL ? a + 1 : NULL)
				TWO(1);
		} else {
			const unsigned short *s = px + w - 1;
			const unsigned char *a = al != NULL ? al + w - 1 : NULL;
			o += w - 1;
			c = cur + w - 1;
			d = nxt + w - 1;
			for (n = w; n > 0; n--, s--, o--, c--, d--, a = a != NULL ? a - 1 : NULL)
				TWO(-1);
		}
#undef TWO
		{
			short *t = cur;
			cur = nxt;
			nxt = t;
			memset(nxt - 1, 0, (size_t) (w + 2) * sizeof *nxt);
		}
	}
	free(rows);
	return 1;
}

static int dither_many(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	/* Floyd-Steinberg: each pixel's error (what it wanted less its pen)
	   carried 7/16 on, and 3/16, 5/16 and 1/16 to the row below,
	   serpentine. The colour wanted may lie past the cube's edge (light
	   blue on 16 colours wants more blue than there is): its pen is the
	   nearest to that, so the error is paid back by pens that lie that
	   way instead of being lost to a clamp (the colour came out grey) */
	long w = p->w, y;
	short *rows = calloc((size_t) (w + 2) * 6, sizeof *rows), *cur, *nxt;
	if (rows == NULL) {
		long i;
		for (i = 0; i < (long) p->h * w; i++) {
			unsigned v = p->px[i];
			out[i] = (unsigned char) nearest_far(pal, (v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8);
		}
		return 1;
	}
	cur = rows;
	nxt = rows + (w + 2) * 3;
	for (y = 0; y < p->h; y++) {
		const unsigned short *px = p->px + y * w;
		const unsigned char *al = p->alpha != NULL ? p->alpha + y * w : NULL;
		unsigned char *o = out + y * w;
		int dir = (y & 1) ? -1 : 1, step = 3 * dir;
		long x = dir > 0 ? 0 : w - 1, n;
		short *c = cur + (x + 1) * 3, *d = nxt + (x + 1) * 3;
		int er = 0, eg = 0, eb = 0;	/* 7/16 of the last pixel's, in 16ths */
		if (pic_poll != NULL && y > 0 && (y & 15) == 0 && !pic_poll()) {
			free(rows);
			return 0;
		}
		for (n = 0; n < w; n++, x += dir, c += step, d += step) {
			unsigned v = px[x];
			int r, g, b, k, qr, qg, qb;
			if (al != NULL && al[x] < 128) {
				o[x] = 0;
				er = eg = eb = 0;
				continue;
			}
			r = (int) ((v >> 8) & 0xF8) + ((er + c[0]) >> 4);
			g = (int) ((v >> 3) & 0xFC) + ((eg + c[1]) >> 4);
			b = (int) ((v << 3) & 0xF8) + ((eb + c[2]) >> 4);
			r = r < -128 ? -128 : r > 383 ? 383 : r;
			g = g < -128 ? -128 : g > 383 ? 383 : g;
			b = b < -128 ? -128 : b > 383 ? 383 : b;
			k = pal->near != NULL ? pal->near[NEAR_AT(r, g, b)] : 0;
			k = k ? k - 1 : nearest_far(pal, r, g, b);
			o[x] = (unsigned char) k;
			qr = r - pal->rgb[k][0];
			qg = g - pal->rgb[k][1];
			qb = b - pal->rgb[k][2];
			er = qr * 7;
			eg = qg * 7;
			eb = qb * 7;
			d[-step] += (short) (qr * 3);
			d[-step + 1] += (short) (qg * 3);
			d[-step + 2] += (short) (qb * 3);
			d[0] += (short) (qr * 5);
			d[1] += (short) (qg * 5);
			d[2] += (short) (qb * 5);
			d[step] = (short) qr;
			d[step + 1] = (short) qg;
			d[step + 2] = (short) qb;
		}
		{
			short *t = cur;
			cur = nxt;
			nxt = t;
			memset(nxt, 0, (size_t) (w + 2) * 6);
		}
	}
	free(rows);
	return 1;
}

#ifdef PIC_LIBJPEG
/* ---------------------------------------------------------------- the dither on the DSP */

/* dsp/dither.a56 does dither_many's work to the bit: the 68030 sends the
   palette's tables once (while the program stays on the DSP and the
   palette is the same, its near table is kept), then each row's pixels,
   and reads the row before's pens while the DSP dithers this one */

#define DD_PAL    8
#define DD_DITHER 9
#define DD_WMAX   1280			/* the widest row it takes */
#define DD_S1     0x800			/* x, y: the pens by red (red, green; blue, pen + 1) */
#define DD_S2     0xa00
#define DD_PR     0xc00			/* x, y, x: pen k - 1's red, green, blue */
#define DD_PB     0xe00
#define DD_NT     0x1000		/* y: the near table */
#define DD_MASK   0xFFFFFFUL
#define DD_FAR    1900L			/* past the pens at either end, by red */

static int dd_state;			/* 0: not checked yet, 1: checked, -1: the 68030 does it */
static int dd_n;			/* the palette the DSP has (0: none) */
static unsigned char dd_rgb[256][3];

/* the tables: the program's constants and the palette's */
static void dd_palette(const PIC_PAL *pal)
{
	static const unsigned long consts[20] = {
		0x001000, 0x000020, 0x000010, 0x40, 0x70, 0x700000, 0x300000, 0x500000, 0x100000,
		0x50, 0x70, 0xb0, DD_PR, 0x010000, 0x000100, 1, 0x90, 0x400000, 0xff, 0xff8000
	};
	unsigned long w[3 * 258], shi;
	long n = pal->n, i;
	/* internal X $40..$E0: WR, WG, LO, WB, WI, and the pack's constant */
	for (i = 0; i < 32; i++) {
		long m = i - 16, mid = 16 * m + 136, lo = 0, hi = n;
		while (lo < hi) {		/* nearest_far's first pen at or above the middle */
			long k = (lo + hi) >> 1;
			if (pal->rgb[pal->order[k]][0] < mid)
				lo = k + 1;
			else
				hi = k;
		}
		w[i] = (unsigned long) (DD_NT + i * 352);
		w[32 + i] = (unsigned long) (i * 11);
		w[64 + i] = (unsigned long) (DD_S1 + 1 + lo);
		w[96 + i] = (unsigned long) (i / 3);
		w[128 + i] = 1UL << (8 * (i % 3));
	}
	w[160] = DD_MASK;
	dsp_load(D_LOADX, 0x40, w, 161);
	/* internal Y $00..$E0: TBR, TBG (signed), TBB (signed), MID, WS */
	memset(w, 0, 0xe1 * sizeof *w);
	for (i = 0; i < 64; i++) {
		if (i < 32) {
			w[i] = ((unsigned long) (8 * i - 128) << 15) & DD_MASK;
			w[0x70 + (i < 16 ? i : i - 32)] = ((unsigned long) (8 * i - 128) << 15) & DD_MASK;
			w[0x80 + i] = ((unsigned long) (16 * (i - 16) + 136) << 12) & DD_MASK;
			w[0xa0 + i] = (0UL - (1UL << (23 - 8 * (i % 3)))) & DD_MASK;
		}
		w[0x40 + (i < 32 ? i : i - 64)] = ((unsigned long) (4 * i - 128) << 15) & DD_MASK;
	}
	w[0xe0] = 0xFDFDFEUL;			/* -$020202's low word */
	dsp_load(D_LOADY, 0, w, 0xe1);
	shi = (unsigned long) (DD_S1 + 1 + n);
	dsp_load(D_LOADX, 0x19, &shi, 1);
	dsp_load(D_LOADX, 0x20, consts, 20);
	/* the pens by red, at the ends far below and above every one */
	w[0] = (0UL - ((unsigned long) DD_FAR << 12)) & DD_MASK;
	for (i = 0; i < n; i++)
		w[1 + i] = (unsigned long) pal->rgb[pal->order[i]][0] << 12;
	w[1 + n] = (unsigned long) (DD_FAR << 12);
	dsp_load(D_LOADX, DD_S1, w, n + 2);
	for (i = 0; i < n; i++) {
		w[i] = (unsigned long) pal->rgb[pal->order[i]][1] << 12;
		w[n + i] = (unsigned long) pal->rgb[pal->order[i]][2] << 12;
		w[2 * n + i] = (unsigned long) pal->order[i] + 1;
	}
	dsp_load(D_LOADY, DD_S1 + 1, w, n);
	dsp_load(D_LOADX, DD_S2 + 1, w + n, n);
	dsp_load(D_LOADY, DD_S2 + 1, w + 2 * n, n);
	/* each pen's colour by its pen + 1 */
	for (i = 0; i < n; i++) {
		w[i] = ((unsigned long) (pal->rgb[i][0] - 128) << 15) & DD_MASK;
		w[n + i] = ((unsigned long) (pal->rgb[i][1] - 128) << 15) & DD_MASK;
		w[2 * n + i] = ((unsigned long) (pal->rgb[i][2] - 128) << 15) & DD_MASK;
	}
	dsp_load(D_LOADX, DD_PR + 1, w, n);
	dsp_load(D_LOADY, DD_PR + 1, w + n, n);
	dsp_load(D_LOADX, DD_PB + 1, w + 2 * n, n);
	w[0] = DD_PAL;
	pic_dsp->put(w, 1);
	dd_n = pal->n;
	memcpy(dd_rgb, pal->rgb, (size_t) pal->n * 3);
}

/* 1: dithered on the DSP; 0: it couldn't be had; -1: it stopped
   answering */
static int dither_dsp(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	static unsigned char last[DD_WMAX + 2];
	long w = p->w, wo = (w + 2) / 3, y;
	unsigned long h[4];
	int got = pic_dsp->begin(PIC_DSP_DITHER);
	if (!got)
		return 0;
	pic_dsp_failed = 0;
	if (got == 1 || dd_n != pal->n || memcmp(dd_rgb, pal->rgb, (size_t) pal->n * 3) != 0)
		dd_palette(pal);
	h[0] = DD_DITHER;
	h[1] = (unsigned long) w;
	h[2] = (unsigned long) p->h;
	h[3] = (unsigned long) wo;
	pic_dsp->put(h, 4);
	/* a row in, then the row before's pens out (three to a word: the
	   last word's spare bytes go to the next row's start, written over
	   when it comes; the last row's through last) */
	for (y = 0; y <= p->h && !pic_dsp_failed; y++) {
		if (y < p->h) {
			static const unsigned long pad[2] = { 1, 1 };
			pic_dsp->putpx(p->px + y * w, p->alpha != NULL ? p->alpha + y * w : NULL, w);
			pic_dsp->put(pad, 2);	/* (the last word of pens' spare bytes) */
		}
		if (y > 0) {
			unsigned char *d = y < p->h ? out + (y - 1) * w : last;
			if (pic_dsp->get24(d, (int) wo, 1, 0) != 0)
				pic_dsp_failed = 1;
			else if (y == p->h)
				memcpy(out + (y - 1) * w, last, (size_t) w);
		}
	}
	pic_dsp->end(!pic_dsp_failed);
	if (pic_dsp_failed) {
		dd_n = 0;
		return -1;
	}
	return 1;
}

/* the check, the first time: a known picture (colours across the cube
   and past its edges' reach, some of it clear) dithered on both, the same
   pens to the bit: 1; 0 not; -1 the DSP couldn't be had */
static int dd_check(PIC_PAL *pal)
{
	PIC t;
	unsigned short px[40 * 12];
	unsigned char al[40 * 12], a[40 * 12], b[40 * 12];
	int i, r;
	memset(&t, 0, sizeof t);
	t.w = 40;
	t.h = 12;
	t.px = px;
	t.alpha = al;
	for (i = 0; i < 40 * 12; i++) {
		int x = i % 40, y = i / 40;
		px[i] = (unsigned short) ((((x * 31) / 39) << 11) | (((y * 63) / 11) << 5) | ((x + y * 7) & 31));
		al[i] = (unsigned char) (x == 7 || (x + y) % 13 == 0 ? 0 : 255);
	}
	r = dither_dsp(&t, pal, a);
	if (r == 0)
		return -1;
	{
		int (*poll)(void) = pic_poll;	/* 12 rows: no turn, and not stopped */
		pic_poll = NULL;
		dither_many(&t, pal, b);
		pic_poll = poll;
	}
	return r > 0 && memcmp(a, b, sizeof a) == 0;
}

const char *pic_dither_state(void)
{
	return pic_dsp == NULL || dd_state < 0 ? "68030" : dd_state > 0 ? "DSP" : "-";
}

/* the DSP's, when it may: 1 done */
static int dither_on_dsp(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	int r;
	if (pic_dsp == NULL || !pic_dsp_dither || dd_state < 0 || p->w > DD_WMAX || p->w < 1 ||
	    (long) p->w * p->h < 1024)
		return 0;
	if (dd_state == 0) {
		r = dd_check(pal);
		if (r <= 0) {
			dd_state = r < 0 ? 0 : -1;
			return 0;
		}
		dd_state = 1;
	}
	r = dither_dsp(p, pal, out);
	if (r < 0)
		dd_state = -1;		/* it stopped answering: the 68030 from now on */
	return r > 0;
}
#endif

int pic_dither(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	if (pal->n == 2)
		return dither_two(p, pal, out);
#ifdef PIC_LIBJPEG
	if (dither_on_dsp(p, pal, out))
		return 1;
#endif
	return dither_many(p, pal, out);
}
