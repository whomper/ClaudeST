/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * inflate.c - gzip/DEFLATE decompression (RFC 1951, 1952), with a limit
 * on what it unpacks (a few KB of gzip can claim gigabytes). ECal's and
 * EContact's, with EWeb's limit; PNG pictures (png.c) unpack with it too.
 *
 * Every byte from a server is decrypted on the 68030, and that costs far
 * more than unpacking: CalDAV answers and web pages shrink four to eight
 * times when the server compresses them. The decoder is table driven: a
 * 10-bit lookup for most codes, the longer ones read off the bits already
 * there, its state in locals (codes). It reads a byte at a time, so it
 * runs on a 68000 too.
 */
#include <string.h>
#include "inflate.h"

#define FAST 10				/* bits looked up at once */

typedef struct {
	short count[16];		/* codes of each length */
	short sym[320];			/* symbols by code */
	unsigned short fast[1 << FAST];	/* (len << 9) | sym, 0: longer code (2 KB) */
} HUFF;

typedef struct {
	const unsigned char *in, *end;
	unsigned long bits;
	int nbits;
	SBUF *out;
	long max;			/* what out may grow to */
	int bad;			/* 1 damaged, 2 too big */
} ST;

static int need(ST *s, int n)
{
	while (s->nbits < n) {
		if (s->in >= s->end) {
			s->bad = 1;
			return 0;
		}
		s->bits |= (unsigned long)*s->in++ << s->nbits;
		s->nbits += 8;
	}
	return 1;
}

static unsigned take(ST *s, int n)
{
	unsigned v;
	if (!n)
		return 0;
	if (!need(s, n))
		return 0;
	v = (unsigned)(s->bits & ((1UL << n) - 1));
	s->bits >>= n;
	s->nbits -= n;
	return v;
}

/* the canonical code for lengths len[0..n): 0 or -1 when it isn't one */
static int build(HUFF *h, const unsigned char *len, int n)
{
	short offs[16];
	int i, l, code, left = 1;
	memset(h->count, 0, sizeof(h->count));
	for (i = 0; i < n; i++)
		h->count[len[i]]++;
	if (h->count[0] == n)
		return 0;			/* no codes: legal for distances */
	for (l = 1; l < 16; l++) {
		left = (left << 1) - h->count[l];
		if (left < 0)
			return -1;
	}
	offs[1] = 0;
	for (l = 1; l < 15; l++)
		offs[l + 1] = offs[l] + h->count[l];
	for (i = 0; i < n; i++)
		if (len[i])
			h->sym[offs[len[i]]++] = (short)i;
	/* the lookup table: each code of up to FAST bits, bit-reversed, in
	   every slot it prefixes */
	memset(h->fast, 0, sizeof(h->fast));
	code = 0;
	{
		int idx = 0;
		for (l = 1; l <= FAST; l++) {
			int k;
			for (k = 0; k < h->count[l]; k++, idx++) {
				int rev = 0, b, c = code + k, fill;
				for (b = 0; b < l; b++)
					rev |= ((c >> b) & 1) << (l - 1 - b);
				for (fill = rev; fill < (1 << FAST); fill += 1 << l)
					h->fast[fill] = (unsigned short)((l << 9) | h->sym[idx]);
			}
			code = (code + h->count[l]) << 1;
		}
	}
	return 0;
}

static int decode(ST *s, const HUFF *h)
{
	int code, first, index, l, count;
	if (s->nbits < FAST) {
		/* top up if the input allows; near the end, fall back below */
		while (s->nbits < FAST && s->in < s->end) {
			s->bits |= (unsigned long)*s->in++ << s->nbits;
			s->nbits += 8;
		}
	}
	if (s->nbits >= FAST) {
		unsigned e = h->fast[s->bits & ((1 << FAST) - 1)];
		if (e) {
			s->bits >>= e >> 9;
			s->nbits -= e >> 9;
			return e & 511;
		}
	}
	/* a long code, or the last few bits: one bit at a time */
	code = first = index = 0;
	for (l = 1; l < 16; l++) {
		code |= (int)take(s, 1);
		if (s->bad)
			return -1;
		count = h->count[l];
		if (code - count < first)
			return h->sym[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	s->bad = 1;
	return -1;
}

static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
				 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
					3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
					  257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
					  8193, 12289, 16385, 24577 };
static const unsigned char dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
					7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* a block's codes to the end of the block. The hot loop: the bits and
   the output kept in locals, each symbol looked up in the 10-bit table or,
   for a longer code, read off the bits already there (a font face of
   EWeb's or a PNG has many); reading past the input's end only as
   zeros, counted, so that a block that ends near it decodes and one that
   runs past it is damaged */
static int codes(ST *s, const HUFF *lc, const HUFF *dc)
{
	SBUF *o = s->out;
	const unsigned char *in = s->in, *end = s->end;
	unsigned long bits = s->bits;
	int nbits = s->nbits, pad = 0, r = -1;
	long olen = o->len, lim;
	char *op = o->s;

#define NEED(n) \
	while (nbits < (n)) { \
		if (in < end) \
			bits |= (unsigned long) *in++ << nbits; \
		else \
			pad++; \
		nbits += 8; \
	}
#define DECODE(H, SYM) \
	do { \
		unsigned e_; \
		NEED(15); \
		e_ = (H)->fast[bits & ((1 << FAST) - 1)]; \
		if (e_) { \
			bits >>= e_ >> 9; \
			nbits -= e_ >> 9; \
			SYM = (int) (e_ & 511); \
		} else { \
			int code_ = 0, first_ = 0, index_ = 0, l_, count_; \
			SYM = -1; \
			for (l_ = 1; l_ < 16; l_++) { \
				code_ |= (int) (bits >> (l_ - 1)) & 1; \
				count_ = (H)->count[l_]; \
				if (code_ - count_ < first_) { \
					SYM = (H)->sym[index_ + (code_ - first_)]; \
					bits >>= l_; \
					nbits -= l_; \
					break; \
				} \
				index_ += count_; \
				first_ = (first_ + count_) << 1; \
				code_ <<= 1; \
			} \
		} \
	} while (0)

	/* lim: where the output may go before it needs room (or is too big) */
	lim = o->cap - 1 < s->max ? o->cap - 1 : s->max;
	for (;;) {
		int sym, len, d;
		unsigned long dist;
		DECODE(lc, sym);
		if (sym < 0 || nbits < 8 * pad)
			break;			/* damaged, or past the input's end */
		if (sym < 256) {
			if (olen >= lim) {
				o->len = olen;
				if (olen >= s->max || !sb_room(o, 4096)) {
					s->bad = 2;
					break;
				}
				op = o->s;
				lim = o->cap - 1 < s->max ? o->cap - 1 : s->max;
			}
			op[olen++] = (char) sym;
			continue;
		}
		if (sym == 256) {
			r = 0;
			break;
		}
		sym -= 257;
		if (sym >= 29)
			break;
		NEED(5);
		len = lbase[sym] + (int) (bits & ((1UL << lext[sym]) - 1));
		bits >>= lext[sym];
		nbits -= lext[sym];
		DECODE(dc, d);
		if (d < 0 || d >= 30)
			break;
		NEED(13);
		dist = dbase[d] + (bits & ((1UL << dext[d]) - 1));
		bits >>= dext[d];
		nbits -= dext[d];
		if (nbits < 8 * pad || dist > (unsigned long) olen)
			break;
		if (olen + len > lim) {
			o->len = olen;
			if (olen + len > s->max || !sb_room(o, len)) {
				s->bad = 2;
				break;
			}
			op = o->s;
			lim = o->cap - 1 < s->max ? o->cap - 1 : s->max;
		}
		{
			/* the copy may overlap itself: byte by byte */
			char *p = op + olen, *q = p - dist, *e = p + len;
			while (p < e)
				*p++ = *q++;
			olen += len;
		}
	}
#undef DECODE
#undef NEED
	o->len = olen;
	if (op != NULL)
		op[olen] = 0;
	/* the zeros read past the end given back: they were never there */
	if (nbits < 8 * pad) {
		s->bad = s->bad ? s->bad : 1;
		r = -1;
	} else {
		nbits -= 8 * pad;
		bits &= nbits ? (1UL << nbits) - 1 : 0;
	}
	s->in = in;
	s->bits = bits;
	s->nbits = nbits;
	return r;
}

static HUFF lit, dst;

static int fixed(ST *s)
{
	static int ready;
	static HUFF flit, fdst;
	if (!ready) {
		unsigned char l[320];
		int i;
		for (i = 0; i < 144; i++)
			l[i] = 8;
		for (; i < 256; i++)
			l[i] = 9;
		for (; i < 280; i++)
			l[i] = 7;
		for (; i < 288; i++)
			l[i] = 8;
		build(&flit, l, 288);
		for (i = 0; i < 30; i++)
			l[i] = 5;
		build(&fdst, l, 30);
		ready = 1;
	}
	return codes(s, &flit, &fdst);
}

static int dynamic(ST *s)
{
	static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	unsigned char len[320];
	int nlen, ndist, ncode, i;
	nlen = (int)take(s, 5) + 257;
	ndist = (int)take(s, 5) + 1;
	ncode = (int)take(s, 4) + 4;
	if (s->bad || nlen > 286 || ndist > 30)
		return -1;
	memset(len, 0, sizeof(len));
	for (i = 0; i < ncode; i++)
		len[order[i]] = (unsigned char)take(s, 3);
	if (s->bad || build(&lit, len, 19))
		return -1;
	for (i = 0; i < nlen + ndist;) {
		int sym = decode(s, &lit), rep = 0, val = 0;
		if (sym < 0)
			return -1;
		if (sym < 16) {
			len[i++] = (unsigned char)sym;
			continue;
		}
		if (sym == 16) {
			if (!i)
				return -1;
			val = len[i - 1];
			rep = 3 + (int)take(s, 2);
		} else if (sym == 17) {
			rep = 3 + (int)take(s, 3);
		} else {
			rep = 11 + (int)take(s, 7);
		}
		if (s->bad || i + rep > nlen + ndist)
			return -1;
		while (rep--)
			len[i++] = (unsigned char)val;
	}
	if (!len[256] || build(&lit, len, nlen) || build(&dst, len + nlen, ndist))
		return -1;
	return codes(s, &lit, &dst);
}

static int stored(ST *s)
{
	unsigned n, nn;
	/* to the byte boundary: the whole bytes the decoder read ahead given
	   back (dropping them lost data when a stored block followed a
	   compressed one) */
	s->in -= s->nbits / 8;
	s->bits = 0;
	s->nbits = 0;
	if (s->end - s->in < 4)
		return -1;
	n = s->in[0] | (s->in[1] << 8);
	nn = s->in[2] | (s->in[3] << 8);
	s->in += 4;
	if ((n ^ 0xffff) != nn || (unsigned long)(s->end - s->in) < n)
		return -1;
	if (s->out->len + (long)n > s->max || !sb_add(s->out, (const char *)s->in, n)) {
		s->bad = 2;
		return -1;
	}
	s->in += n;
	return 0;
}

int inflate_raw(const unsigned char *in, long n, SBUF *out, long max)
{
	ST s;
	int last;
	memset(&s, 0, sizeof(s));
	if (n < 0)
		return 0;
	s.in = in;
	s.end = in + n;
	s.out = out;
	s.max = max;
	do {
		int type, r;
		last = (int)take(&s, 1);
		type = (int)take(&s, 2);
		if (s.bad)
			return 0;
		r = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
		if (r < 0)
			return s.bad == 2 ? -1 : 0;
	} while (!last);
	return 1;
}

int inflate_gzip(const unsigned char *in, long n, SBUF *out, long max)
{
	const unsigned char *p = in, *end = in + n;
	int flg;
	if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8)
		return 0;
	flg = p[3];
	p += 10;
	if (flg & 4) {				/* FEXTRA */
		unsigned x;
		if (end - p < 2)
			return 0;
		x = p[0] | (p[1] << 8);
		p += 2 + x;
	}
	if (flg & 8)				/* FNAME */
		while (p < end && *p++)
			;
	if (flg & 16)				/* FCOMMENT */
		while (p < end && *p++)
			;
	if (flg & 2)				/* FHCRC */
		p += 2;
	if (end - p < 8)
		return 0;
	return inflate_raw(p, end - p - 8, out, max);
}

/* "deflate" in HTTP is zlib's wrapper around raw DEFLATE (usually) */
int inflate_zlib(const unsigned char *in, long n, SBUF *out, long max)
{
	if (n > 6 && (in[0] & 0x0f) == 8 && ((in[0] << 8) | in[1]) % 31 == 0)
		return inflate_raw(in + 2, n - 6, out, max);
	return inflate_raw(in, n, out, max);
}
