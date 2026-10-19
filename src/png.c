/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * png.c - PNG pictures (png.h), on common's inflate.
 *
 * The image data (every IDAT, joined when there are several) is unpacked
 * in one go into a buffer of exactly its size, then each row is
 * unfiltered in place and turned into R G B A. An interlaced picture's
 * seven passes are put together in a full R G B A picture first; one that
 * isn't goes out a row at a time. Gamma and colour profiles are left
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

static int paeth(int a, int b, int c)
{
	int p = a + b - c, pa = p - a, pb = p - b, pc = p - c;
	if (pa < 0)
		pa = -pa;
	if (pb < 0)
		pb = -pb;
	if (pc < 0)
		pc = -pc;
	if (pa <= pb && pa <= pc)
		return a;
	return pb <= pc ? b : c;
}

/* row (n bytes after its filter byte f) unfiltered in place, prev the
   row above unfiltered (NULL for the first): 0 for an unknown filter */
static int unfilter(unsigned char *row, const unsigned char *prev, long n, int bpp, int f)
{
	long i;
	switch (f) {
	case 0:
		return 1;
	case 1:
		for (i = bpp; i < n; i++)
			row[i] = (unsigned char) (row[i] + row[i - bpp]);
		return 1;
	case 2:
		if (prev != NULL)
			for (i = 0; i < n; i++)
				row[i] = (unsigned char) (row[i] + prev[i]);
		return 1;
	case 3:
		for (i = 0; i < n; i++) {
			int a = i >= bpp ? row[i - bpp] : 0, b = prev != NULL ? prev[i] : 0;
			row[i] = (unsigned char) (row[i] + ((a + b) >> 1));
		}
		return 1;
	case 4:
		for (i = 0; i < n; i++) {
			int a = i >= bpp ? row[i - bpp] : 0, b = prev != NULL ? prev[i] : 0;
			int c = i >= bpp && prev != NULL ? prev[i - bpp] : 0;
			row[i] = (unsigned char) (row[i] + paeth(a, b, c));
		}
		return 1;
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

/* w pixels of an unfiltered row as R G B A */
static void convert(const HDR *h, const unsigned char *s, long w, unsigned char *o)
{
	long x;
	switch (h->ctype) {
	case 0:			/* grey */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 2, o += 4) {
				o[0] = o[1] = o[2] = s[0];
				o[3] = h->has_key && ((unsigned) s[0] << 8 | s[1]) == h->key[0] ? 0 : 255;
			}
		} else if (h->depth == 8) {
			for (x = 0; x < w; x++, o += 4) {
				o[0] = o[1] = o[2] = s[x];
				o[3] = h->has_key && s[x] == h->key[0] ? 0 : 255;
			}
		} else {
			/* 1, 2, 4 bits to 0..255: the bits repeated */
			for (x = 0; x < w; x++, o += 4) {
				unsigned v = small(s, x, h->depth), g;
				g = h->depth == 1 ? (v ? 255 : 0) : h->depth == 2 ? (v << 6 | v << 4 | v << 2 | v) : (v << 4 | v);
				o[0] = o[1] = o[2] = (unsigned char) g;
				o[3] = h->has_key && v == h->key[0] ? 0 : 255;
			}
		}
		break;
	case 2:			/* RGB */
		if (h->depth == 16) {
			for (x = 0; x < w; x++, s += 6, o += 4) {
				o[0] = s[0];
				o[1] = s[2];
				o[2] = s[4];
				o[3] = h->has_key && ((unsigned) s[0] << 8 | s[1]) == h->key[0] &&
				       ((unsigned) s[2] << 8 | s[3]) == h->key[1] &&
				       ((unsigned) s[4] << 8 | s[5]) == h->key[2] ? 0 : 255;
			}
		} else {
			for (x = 0; x < w; x++, s += 3, o += 4) {
				o[0] = s[0];
				o[1] = s[1];
				o[2] = s[2];
				o[3] = h->has_key && s[0] == h->key[0] && s[1] == h->key[1] && s[2] == h->key[2] ? 0 : 255;
			}
		}
		break;
	case 3:			/* a palette: an index past it is black */
		for (x = 0; x < w; x++, o += 4) {
			unsigned v = h->depth == 8 ? s[x] : small(s, x, h->depth);
			memcpy(o, h->pal[v], 4);
		}
		break;
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

int png_decode(const unsigned char *d, long n, long max_px,
	       int (*row)(void *ud, long y, const unsigned char *rgba), void *ud)
{
	HDR h;
	long p = 8, need = 0, zlen = 0, y;
	const unsigned char *z = NULL;
	unsigned char *joined = NULL, *line = NULL, *full = NULL, *prev;
	SBUF raw;
	int r, i, nidat = 0, seen_ihdr = 0;

	if (n < 8 || memcmp(d, sig, 8) != 0)
		return PNG_DAMAGED;
	/* the chunks: the header, the palette, transparency, the data */
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
			if (nidat++ == 0) {
				z = c;
				zlen = (long) len;
			} else {
				/* more than one: joined into one buffer */
				unsigned char *j = realloc(joined, (size_t) (zlen + (long) len));
				if (j == NULL) {
					free(joined);
					return PNG_MEMORY;
				}
				if (joined == NULL)
					memcpy(j, z, (size_t) zlen);
				joined = j;
				memcpy(joined + zlen, c, len);
				zlen += (long) len;
				z = joined;
			}
		} else if (memcmp(t, "IEND", 4) == 0) {
			break;
		}
		p += 12 + (long) len;
	}
	if (!seen_ihdr || z == NULL || zlen < 2) {
		free(joined);
		return PNG_DAMAGED;
	}
	if (h.w > max_px / h.h) {
		free(joined);
		return PNG_TOO_BIG;
	}
	/* the image data's size */
	if (h.interlace) {
		for (i = 0; i < 7; i++) {
			long pw = pass_n(h.w, a7[i][0], a7[i][2]), ph = pass_n(h.h, a7[i][1], a7[i][3]);
			if (pw > 0 && ph > 0)
				need += ph * (1 + row_bytes(&h, pw));
		}
	} else {
		need = h.h * (1 + row_bytes(&h, h.w));
	}
	/* zlib's two bytes, then DEFLATE (no preset dictionary) */
	if ((z[0] & 15) != 8 || ((unsigned) z[0] << 8 | z[1]) % 31 != 0 || (z[1] & 0x20)) {
		free(joined);
		return PNG_DAMAGED;
	}
	sb_init(&raw);
	if (!sb_reserve(&raw, need)) {
		free(joined);
		return PNG_MEMORY;
	}
	r = inflate_raw(z + 2, zlen - 2, &raw, need);
	free(joined);
	if (r == 0 && raw.len == 0) {
		sb_free(&raw);
		return PNG_DAMAGED;
	}
	/* r is -1 when there was more than the picture: the rest is left */
	memset(raw.s + raw.len, 0, (size_t) (need - raw.len));	/* a picture cut short */

	line = malloc((size_t) h.w * 4);
	if (line == NULL) {
		sb_free(&raw);
		return PNG_MEMORY;
	}
	r = PNG_OK;
	if (!h.interlace) {
		long rb = row_bytes(&h, h.w);
		unsigned char *s = (unsigned char *) raw.s;
		prev = NULL;
		for (y = 0; y < h.h; y++, s += 1 + rb) {
			if ((long) (s - (unsigned char *) raw.s) >= raw.len) {
				memset(line, 0, (size_t) h.w * 4);
			} else {
				if (!unfilter(s + 1, prev, rb, h.bpp, s[0])) {
					r = PNG_DAMAGED;
					break;
				}
				convert(&h, s + 1, h.w, line);
				prev = s + 1;
			}
			if (!row(ud, y, line)) {
				r = PNG_STOPPED;
				break;
			}
		}
	} else {
		unsigned char *s = (unsigned char *) raw.s;
		long w4 = h.w * 4;
		full = calloc((size_t) h.h, (size_t) w4);
		if (full == NULL) {
			r = PNG_MEMORY;
		}
		for (i = 0; i < 7 && r == PNG_OK; i++) {
			long pw = pass_n(h.w, a7[i][0], a7[i][2]), ph = pass_n(h.h, a7[i][1], a7[i][3]);
			long rb = row_bytes(&h, pw), py, x;
			if (pw == 0 || ph == 0)
				continue;
			prev = NULL;
			for (py = 0; py < ph; py++, s += 1 + rb) {
				unsigned char *o, *q;
				if ((long) (s - (unsigned char *) raw.s) >= raw.len)
					break;
				if (!unfilter(s + 1, prev, rb, h.bpp, s[0])) {
					r = PNG_DAMAGED;
					break;
				}
				prev = s + 1;
				convert(&h, s + 1, pw, line);
				o = full + (a7[i][1] + py * a7[i][3]) * w4 + a7[i][0] * 4;
				q = line;
				for (x = 0; x < pw; x++, q += 4, o += a7[i][2] * 4)
					memcpy(o, q, 4);
			}
		}
		for (y = 0; y < h.h && r == PNG_OK; y++)
			if (!row(ud, y, full + y * w4))
				r = PNG_STOPPED;
		free(full);
	}
	free(line);
	sb_free(&raw);
	return r;
}
