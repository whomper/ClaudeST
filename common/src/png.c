/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * png.c - PNG pictures (png.h), on common's inflate.
 *
 * The image data (the IDAT chunks, read one after another) is unpacked a
 * row at a time as it is needed, into two rows' room, unfiltered against
 * the row before and turned into R G B A: no buffer the picture's size,
 * and the caller's row() is called between rows (where a program takes
 * its turns). An interlaced picture's seven passes are put together in a
 * full R G B A picture first. Gamma and colour profiles are left
 * out, as most browsers of the 1990s did; CRCs aren't checked (a damaged
 * picture shows as it comes, as in browsers).
 *
 * Bytes are read one at a time (a 68000 can't read a word at an odd
 * address), and the loops over pixels have no 32-bit multiply or divide.
 */
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "inflate.h"
#include "util.h"

static unsigned long rd32(const unsigned char *p)
{
	return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | p[3];
}

static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

int png_size(const unsigned char *d, long n, long *w, long *h)
{
	if (n < 24 || memcmp(d, sig, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0)
		return 0;
	*w = (long) rd32(d + 16);
	*h = (long) rd32(d + 20);
	return 1;
}

typedef struct {
	long w, h;
	int depth, ctype, interlace;
	int bits;			/* per pixel */
	int bpp;			/* bytes per pixel for the filters, at least 1 */
	unsigned char pal[256][4];
	int npal;
	int has_key;			/* tRNS for grey or RGB: this colour is transparent */
	unsigned key[3];
} HDR;

/* a row's bytes, without its filter byte */
static long row_bytes(const HDR *h, long w)
{
	return (w * h->bits + 7) >> 3;
}

/* |x| for x from -510 to 510 (at x + 510), for Paeth's filter: a table
   instead of three tests and jumps a byte; made the first time it is
   needed */
static unsigned short absv[1021];

/* row (n bytes after its filter byte f) unfiltered in place, prev the
   row above unfiltered (zeros above the first): 0 for an unknown filter.
   The first pixel's bytes (nothing to their left) apart, so the loops
   over the rest have no tests but the filter's own */
static int unfilter(unsigned char *row, const unsigned char *prev, long n, int bpp, int f)
{
	long i, m = n < bpp ? n : bpp;
	switch (f) {
	case 0:
		return 1;
	case 1:
		for (i = bpp; i < n; i++)
			row[i] = (unsigned char) (row[i] + row[i - bpp]);
		return 1;
	case 2:
		for (i = 0; i < n; i++)
			row[i] = (unsigned char) (row[i] + prev[i]);
		return 1;
	case 3:
		for (i = 0; i < m; i++)
			row[i] = (unsigned char) (row[i] + (prev[i] >> 1));
		for (; i < n; i++)
			row[i] = (unsigned char) (row[i] + ((row[i - bpp] + prev[i]) >> 1));
		return 1;
	case 4: {
		/* Paeth's: of left, above and above-left, the nearest to
		   left + above - above-left (ties in that order); the first
		   pixel's is the byte above */
		const unsigned char *q = prev + bpp, *qc = prev;
		const unsigned short *ab = absv + 510;
		unsigned char *r = row + bpp, *ra = row;
		for (i = 0; i < m; i++)
			row[i] = (unsigned char) (row[i] + prev[i]);
		if (absv[0] == 0)
			for (i = 0; i <= 1020; i++)
				absv[i] = (unsigned short) (i < 510 ? 510 - i : i - 510);
		for (i = n - m; i > 0; i--) {
			int a = *ra++, bb = *q++, c = *qc++;
			int da = bb - c, db = a - c;
			unsigned pa = ab[da], pb = ab[db], pc = ab[da + db];
			*r = (unsigned char) (*r + (pa <= pb && pa <= pc ? a : pb <= pc ? bb : c));
			r++;
		}
		return 1;
	}
	}
	return 0;
}

/* the sample at x of a row of 1, 2 or 4-bit samples */
static unsigned small(const unsigned char *s, long x, int depth)
{
	switch (depth) {
	case 1: return (s[x >> 3] >> (7 - (x & 7))) & 1;
	case 2: return (s[x >> 2] >> (6 - ((x & 3) << 1))) & 3;
	default: return (s[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
	}
}

/* w pixels of an unfiltered row as R G B A (n 4), or as R G B (n 3,
   for a picture with no transparency: opaque()); made twice, each with
   its n fixed */
static inline __attribute__((always_inline)) void convert_n(const HDR *h, const unsigned char *s, long w,
							   unsigned char *o, const int n)
{
	long x;
	switch (h->ctype) {
	case 0:			/* grey */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 2, o += n) {
				o[0] = o[1] = o[2] = s[0];
				if (n == 4)
					o[3] = h->has_key && ((unsigned) s[0] << 8 | s[1]) == h->key[0] ? 0 : 255;
			}
		} else if (h->depth == 8) {
			for (x = 0; x < w; x++, o += n) {
				o[0] = o[1] = o[2] = s[x];
				if (n == 4)
					o[3] = h->has_key && s[x] == h->key[0] ? 0 : 255;
			}
		} else {
			/* 1, 2, 4 bits to 0..255: the bits repeated */
			for (x = 0; x < w; x++, o += n) {
				unsigned v = small(s, x, h->depth), g;
				g = h->depth == 1 ? (v ? 255 : 0) : h->depth == 2 ? (v << 6 | v << 4 | v << 2 | v) : (v << 4 | v);
				o[0] = o[1] = o[2] = (unsigned char) g;
				if (n == 4)
					o[3] = h->has_key && v == h->key[0] ? 0 : 255;
			}
		}
		break;
	case 2:			/* RGB */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 6, o += n) {
				o[0] = s[0];
				o[1] = s[2];
				o[2] = s[4];
				if (n == 4)
					o[3] = h->has_key && ((unsigned) s[0] << 8 | s[1]) == h->key[0] &&
					       ((unsigned) s[2] << 8 | s[3]) == h->key[1] &&
					       ((unsigned) s[4] << 8 | s[5]) == h->key[2] ? 0 : 255;
			}
		} else if (n == 3) {
			memcpy(o, s, (size_t) w * 3);
		} else {
			for (x = 0; x < w; x++, s += 3, o += 4) {
				o[0] = s[0];
				o[1] = s[1];
				o[2] = s[2];
				o[3] = h->has_key && s[0] == h->key[0] && s[1] == h->key[1] && s[2] == h->key[2] ? 0 : 255;
			}
		}
		break;
	case 3: {		/* a palette: an index past it is black */
		const unsigned char (*pal)[4] = h->pal;
		if (h->depth == 8) {
			for (x = 0; x < w; x++, o += n) {
				const unsigned char *c = pal[s[x]];
				o[0] = c[0];
				o[1] = c[1];
				o[2] = c[2];
				if (n == 4)
					o[3] = c[3];
			}
		} else if (h->depth == 4) {
			/* two pixels a byte (most small palette pictures) */
			const unsigned char *c;
			for (x = w >> 1; x > 0; x--, s++) {
				c = pal[*s >> 4];
				o[0] = c[0];
				o[1] = c[1];
				o[2] = c[2];
				if (n == 4)
					o[3] = c[3];
				o += n;
				c = pal[*s & 15];
				o[0] = c[0];
				o[1] = c[1];
				o[2] = c[2];
				if (n == 4)
					o[3] = c[3];
				o += n;
			}
			if (w & 1) {
				c = pal[*s >> 4];
				o[0] = c[0];
				o[1] = c[1];
				o[2] = c[2];
				if (n == 4)
					o[3] = c[3];
			}
		} else {
			/* 1, 2 bits: a byte's pixels from its top bits down */
			int d = h->depth, sh;
			unsigned mask = (1u << d) - 1, b;
			for (x = 0; x < w; s++) {
				b = *s;
				for (sh = 8 - d; sh >= 0 && x < w; sh -= d, x++, o += n) {
					const unsigned char *c = pal[(b >> sh) & mask];
					o[0] = c[0];
					o[1] = c[1];
					o[2] = c[2];
					if (n == 4)
						o[3] = c[3];
				}
			}
		}
		break;
	}
	case 4:			/* grey and alpha */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 4, o += 4) {
				o[0] = o[1] = o[2] = s[0];
				o[3] = s[2];
			}
		} else {
			for (x = 0; x < w; x++, s += 2, o += 4) {
				o[0] = o[1] = o[2] = s[0];
				o[3] = s[1];
			}
		}
		break;
	default:		/* 6: RGBA */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 8, o += 4) {
				o[0] = s[0];
				o[1] = s[2];
				o[2] = s[4];
				o[3] = s[6];
			}
		} else {
			memcpy(o, s, (size_t) w * 4);
		}
		break;
	}
}

static void convert(const HDR *h, const unsigned char *s, long w, unsigned char *o)
{
	convert_n(h, s, w, o, 4);
}

static void convert3(const HDR *h, const unsigned char *s, long w, unsigned char *o)
{
	convert_n(h, s, w, o, 3);
}

/* no pixel can be transparent: no alpha channel, no key, a palette's
   every entry solid */
static int opaque(const HDR *h)
{
	int i;
	if (h->ctype == 4 || h->ctype == 6 || h->has_key)
		return 0;
	if (h->ctype == 3)
		for (i = 0; i < 256; i++)
			if (h->pal[i][3] != 255)
				return 0;
	return 1;
}

static int header(HDR *h, const unsigned char *d)
{
	int ok;
	memset(h, 0, sizeof *h);
	h->w = (long) rd32(d);
	h->h = (long) rd32(d + 4);
	h->depth = d[8];
	h->ctype = d[9];
	h->interlace = d[12];
	if (d[10] != 0 || d[11] != 0 || h->interlace > 1)
		return PNG_UNSUPPORTED;
	switch (h->ctype) {
	case 0: ok = h->depth == 1 || h->depth == 2 || h->depth == 4 || h->depth == 8 || h->depth == 16; break;
	case 3: ok = h->depth == 1 || h->depth == 2 || h->depth == 4 || h->depth == 8; break;
	case 2: case 4: case 6: ok = h->depth == 8 || h->depth == 16; break;
	default: ok = 0;
	}
	if (!ok)
		return PNG_DAMAGED;
	if (h->w <= 0 || h->h <= 0 || h->w > 0x7fffL || h->h > 0x7fffL)
		return PNG_TOO_BIG;
	h->bits = h->depth * (h->ctype == 2 ? 3 : h->ctype == 4 ? 2 : h->ctype == 6 ? 4 : 1);
	h->bpp = h->bits < 8 ? 1 : h->bits >> 3;
	return PNG_OK;
}

static const unsigned char a7[7][4] = {	/* each pass: x0, y0, dx, dy */
	{ 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

static long pass_n(long size, int start, int step)
{
	return size > start ? (size - start + step - 1) / step : 0;
}

/* the IDAT chunks' data in turn, for inflate_open: from the chunk at p
   on */
typedef struct {
	const unsigned char *d;
	long n, p;			/* the file; the next chunk to look at */
	const unsigned char *c;		/* the IDAT being read, left bytes of it */
	long left;
} IDATS;

static int idat_get(void *u, unsigned char *buf, long n)
{
	IDATS *s = u;
	long k = 0;
	while (k < n) {
		long t;
		if (s->left == 0) {
			unsigned long len;
			const unsigned char *ty;
			for (;;) {
				if (s->n - s->p < 12)
					return (int) k;
				len = rd32(s->d + s->p);
				ty = s->d + s->p + 4;
				if (len > (unsigned long) (s->n - s->p - 12)) {
					if (memcmp(ty, "IDAT", 4) != 0)
						return (int) k;
					len = (unsigned long) (s->n - s->p - 8);	/* cut short: what there is */
				}
				if (memcmp(ty, "IEND", 4) == 0)
					return (int) k;
				s->p += 12 + (long) len;
				if (memcmp(ty, "IDAT", 4) == 0 && len > 0)
					break;
			}
			s->c = ty + 4;
			s->left = (long) len;
		}
		t = n - k < s->left ? n - k : s->left;
		memcpy(buf + k, s->c, (size_t) t);
		s->c += t;
		s->left -= t;
		k += t;
	}
	return (int) k;
}

/* n bytes unpacked, as many as there are: fewer at the data's end (or
   where it is damaged) */
static long read_all(INFLATE *z, unsigned char *buf, long n)
{
	long k = 0, r;
	while (k < n && (r = inflate_read(z, buf + k, n - k)) > 0)
		k += r;
	return k;
}

int png_decode(const unsigned char *d, long n, long max_px,
	       int (*row)(void *ud, long y, const unsigned char *rgba), void *ud)
{
	return png_decode_rgb(d, n, max_px, row, NULL, ud);
}

int png_decode_rgb(const unsigned char *d, long n, long max_px,
		   int (*row)(void *ud, long y, const unsigned char *rgba),
		   int (*row_rgb)(void *ud, long y, const unsigned char *rgb), void *ud)
{
	HDR h;
	long p = 8, y, first = 0, rbmax;
	const unsigned char *z = NULL;
	unsigned char *line = NULL, *full = NULL, *cur = NULL, *prev = NULL;
	IDATS src;
	INFLATE *zs;
	int r, i, seen_ihdr = 0, ended = 0;

	if (n < 8 || memcmp(d, sig, 8) != 0)
		return PNG_DAMAGED;
	/* the chunks: the header, the palette, transparency, where the data
	   starts (read from there a row at a time, chunk after chunk) */
	while (n - p >= 12) {
		unsigned long len = rd32(d + p);
		const unsigned char *t = d + p + 4, *c = d + p + 8;
		if (len > (unsigned long) (n - p - 12)) {
			if (memcmp(t, "IDAT", 4) != 0)
				break;
			len = (unsigned long) (n - p - 8);	/* cut short: what there is */
		}
		if (memcmp(t, "IHDR", 4) == 0 && len >= 13 && !seen_ihdr) {
			if ((r = header(&h, c)) != PNG_OK)
				return r;
			seen_ihdr = 1;
			for (i = 0; i < 256; i++) {
				h.pal[i][0] = h.pal[i][1] = h.pal[i][2] = 0;
				h.pal[i][3] = 255;
			}
		} else if (!seen_ihdr) {
			return PNG_DAMAGED;
		} else if (memcmp(t, "PLTE", 4) == 0) {
			h.npal = (int) (len / 3 > 256 ? 256 : len / 3);
			for (i = 0; i < h.npal; i++)
				memcpy(h.pal[i], c + 3 * i, 3);
		} else if (memcmp(t, "tRNS", 4) == 0) {
			if (h.ctype == 3) {
				for (i = 0; i < (int) len && i < 256; i++)
					h.pal[i][3] = c[i];
			} else if (h.ctype == 0 && len >= 2) {
				h.has_key = 1;
				h.key[0] = (unsigned) c[0] << 8 | c[1];
			} else if (h.ctype == 2 && len >= 6) {
				h.has_key = 1;
				for (i = 0; i < 3; i++)
					h.key[i] = (unsigned) c[2 * i] << 8 | c[2 * i + 1];
			}
			/* a key for 8 bits or fewer is compared with the sample as it is */
		} else if (memcmp(t, "IDAT", 4) == 0) {
			if (z == NULL) {
				z = c;
				first = p;
			}
		} else if (memcmp(t, "IEND", 4) == 0) {
			break;
		}
		p += 12 + (long) len;
	}
	if (!seen_ihdr || z == NULL)
		return PNG_DAMAGED;
	if (h.w > max_px / h.h)
		return PNG_TOO_BIG;
	src.d = d;
	src.n = n;
	src.p = first;
	src.left = 0;
	/* zlib's two bytes (wherever the chunks split them), then DEFLATE (no
	   preset dictionary) */
	{
		unsigned char zh[2];
		if (idat_get(&src, zh, 2) != 2 || (zh[0] & 15) != 8 || ((unsigned) zh[0] << 8 | zh[1]) % 31 != 0 ||
		    (zh[1] & 0x20))
			return PNG_DAMAGED;
	}
	rbmax = row_bytes(&h, h.w);
	zs = inflate_open(idat_get, &src);
	line = malloc((size_t) h.w * 4);
	cur = malloc((size_t) rbmax + 1);
	prev = calloc((size_t) rbmax + 1, 1);	/* zeros above the first row */
	if (zs == NULL || line == NULL || cur == NULL || prev == NULL) {
		r = PNG_MEMORY;
		goto out;
	}
	r = PNG_OK;
	if (!h.interlace) {
		/* each row unpacked as it is needed: no room for the whole */
		int rgb = row_rgb != NULL && opaque(&h);
		for (y = 0; y < h.h; y++) {
			long got = ended ? 0 : read_all(zs, cur, 1 + rbmax);
			if (got < 1 + rbmax)
				ended = 1;	/* the rest of the picture is clear */
			if (y == 0 && got == 0) {
				r = PNG_DAMAGED;	/* no picture at all */
				break;
			}
			if (got == 0) {
				memset(line, 0, (size_t) h.w * 4);
				rgb = 0;		/* transparent rows from here */
			} else {
				unsigned char *t;
				memset(cur + got, 0, (size_t) (1 + rbmax - got));	/* a row cut short */
				if (!unfilter(cur + 1, prev + 1, rbmax, h.bpp, cur[0])) {
					r = PNG_DAMAGED;
					break;
				}
				if (rgb)
					convert3(&h, cur + 1, h.w, line);
				else
					convert(&h, cur + 1, h.w, line);
				t = prev;
				prev = cur;
				cur = t;
			}
			if (!(rgb ? row_rgb(ud, y, line) : row(ud, y, line))) {
				r = PNG_STOPPED;
				break;
			}
		}
	} else {
		long w4 = h.w * 4;
		full = calloc((size_t) h.h, (size_t) w4);
		if (full == NULL)
			r = PNG_MEMORY;
		for (i = 0; i < 7 && r == PNG_OK && !ended; i++) {
			long pw = pass_n(h.w, a7[i][0], a7[i][2]), ph = pass_n(h.h, a7[i][1], a7[i][3]);
			long rb = row_bytes(&h, pw), py, x;
			if (pw == 0 || ph == 0)
				continue;
			memset(prev, 0, (size_t) rb + 1);	/* zeros above a pass's first row */
			for (py = 0; py < ph; py++) {
				unsigned char *o, *q, *t;
				long got = read_all(zs, cur, 1 + rb);
				if (got == 0) {
					ended = 1;
					if (i == 0 && py == 0)
						r = PNG_DAMAGED;	/* no picture at all */
					break;
				}
				if (got < 1 + rb) {
					ended = 1;
					memset(cur + got, 0, (size_t) (1 + rb - got));
				}
				if (!unfilter(cur + 1, prev + 1, rb, h.bpp, cur[0])) {
					r = PNG_DAMAGED;
					break;
				}
				convert(&h, cur + 1, pw, line);
				o = full + (a7[i][1] + py * a7[i][3]) * w4 + a7[i][0] * 4;
				q = line;
				for (x = 0; x < pw; x++, q += 4, o += a7[i][2] * 4)
					memcpy(o, q, 4);
				t = prev;
				prev = cur;
				cur = t;
				if (ended)
					break;
			}
		}
		for (y = 0; y < h.h && r == PNG_OK; y++)
			if (!row(ud, y, full + y * w4))
				r = PNG_STOPPED;
	}
out:
	free(full);
	free(line);
	free(cur);
	free(prev);
	if (zs != NULL)
		inflate_close(zs);
	return r;
}
