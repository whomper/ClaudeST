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
#include <stdlib.h>
#include "inflate.h"

#define FAST 10				/* bits looked up at once */

typedef struct {
	short count[16];		/* codes of each length */
	short sym[320];			/* symbols by code */
	unsigned short fast[1 << FAST];	/* (sym << 4) | len, 0: longer code (2 KB) */
	short first, index;		/* a longer code's search where the table's ends */
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

/* a byte's bits the other way round */
static const unsigned char rev8[256] = {
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
	R6(0), R6(2), R6(1), R6(3)
#undef R2
#undef R4
#undef R6
};

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
	   every slot it prefixes. (A code reversed through rev8: bit by bit,
	   two shifts by a count each, it took 190,000 cycles a table on an
	   8 MHz ST, three tables a stream: EWeb's 16 font faces 1.1 s) */
	memset(h->fast, 0, sizeof(h->fast));
	code = 0;
	{
		int idx = 0;
		for (l = 1; l <= FAST; l++) {
			int k, step = 1 << l;
			for (k = 0; k < h->count[l]; k++, idx++) {
				unsigned c = (unsigned) (code + k) << (16 - l);
				unsigned short e = (unsigned short) (h->sym[idx] << 4 | l);
				unsigned short *f = h->fast + (rev8[c >> 8] | rev8[c & 0xff] << 8);
				unsigned short *fe = h->fast + (1 << FAST);
				for (; f < fe; f += step)
					*f = e;
			}
			code = (code + h->count[l]) << 1;
		}
		h->first = (short) code;
		h->index = (short) idx;
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
			s->bits >>= e & 15;
			s->nbits -= e & 15;
			return e >> 4;
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

/* a code longer than FAST bits (rare: a font face's stream had 1,415 in
   47,500), read off bits a bit at a time from where the table ends (its
   first FAST bits, reversed, are the code so far): its (symbol << 4) |
   length, as the table's entries are; 0 when it isn't one. Apart, so
   that codes' loop keeps its state in registers */
static __attribute__((noinline)) unsigned slow_code(const HUFF *h, unsigned long bits)
{
	unsigned b = (unsigned) bits & ((1 << FAST) - 1);
	int code = (rev8[b & 0xff] << 8 | rev8[b >> 8]) >> (16 - FAST - 1);
	int first = h->first, index = h->index, l, count;
	for (l = FAST + 1; l < 16; l++) {
		code |= (int) (bits >> (l - 1)) & 1;
		count = h->count[l];
		if (code - count < first)
			return (unsigned) (h->sym[index + (code - first)] << 4 | l);
		index += count;
		first = (first + count) << 1;
		code <<= 1;
	}
	return 0;
}

static const unsigned short emask[14] = { 0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191 };

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
	const unsigned short *lf = lc->fast, *df = dc->fast;
	unsigned long bits = s->bits;
	int nbits = s->nbits, pad = 0, r = -1;
	long lim;
	/* w: where the next byte goes; wl: as far as it may go before the
	   output needs room (or is too big) */
	char *w, *wl;

#define NEED(n) \
	while (nbits < (n)) { \
		if (in < end) \
			bits |= (unsigned long) *in++ << nbits; \
		else \
			pad++; \
		nbits += 8; \
	}
#define DECODE(F, H, SYM) \
	do { \
		unsigned e_; \
		NEED(15); \
		e_ = (F)[bits & ((1 << FAST) - 1)]; \
		if (e_ == 0 && (e_ = slow_code(H, bits)) == 0) { \
			SYM = -1; \
			break; \
		} \
		bits >>= e_ & 15; \
		nbits -= e_ & 15; \
		SYM = (int) (e_ >> 4); \
	} while (0)
#define ROOM(n) \
	do { \
		long olen_ = w - o->s; \
		o->len = olen_; \
		if (olen_ + (n) > s->max || !sb_room(o, (n))) { \
			s->bad = 2; \
			goto out; \
		} \
		w = o->s + olen_; \
		lim = o->cap - 1 < s->max ? o->cap - 1 : s->max; \
		wl = o->s + lim; \
	} while (0)

	/* (a buffer to point into: an empty one has none yet) */
	if (o->s == NULL && !sb_room(o, 256)) {
		s->bad = 2;
		return -1;
	}
	w = o->s + o->len;
	lim = o->cap - 1 < s->max ? o->cap - 1 : s->max;
	wl = o->s + lim;
	for (;;) {
		int sym, len, d, x;
		unsigned long dist;
		DECODE(lf, lc, sym);
		if (sym < 0 || (pad && nbits < 8 * pad))
			break;			/* damaged, or past the input's end */
		if (sym < 256) {
			if (w >= wl)
				ROOM(4096);
			*w++ = (char) sym;
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
		x = lext[sym];
		len = lbase[sym] + (int) ((unsigned) bits & emask[x]);
		bits >>= x;
		nbits -= x;
		DECODE(df, dc, d);
		if (d < 0 || d >= 30)
			break;
		NEED(13);
		x = dext[d];
		dist = dbase[d] + ((unsigned) bits & emask[x]);
		bits >>= x;
		nbits -= x;
		if ((pad && nbits < 8 * pad) || dist > (unsigned long) (w - o->s))
			break;
		if (w + len > wl)
			ROOM(len);
		{
			/* the copy may overlap itself: byte by byte (on the 68000
			   a dbra loop, 22 cycles a byte, where GCC's took 46) */
			const char *q = w - dist;
#ifdef __mc68000__
			{
				short k = (short) (len - 1);
				__asm__ volatile ("1:\tmove.b (%1)+,(%0)+\n\tdbra %2,1b"
						  : "+a" (w), "+a" (q), "+d" (k) : : "memory");
			}
#else
			{
				char *e = w + len;
				while (w < e)
					*w++ = *q++;
			}
#endif
		}
	}
out:
	o->len = w - o->s;
#undef ROOM
#undef DECODE
#undef NEED
	if (o->s != NULL)
		o->s[o->len] = 0;
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
static HUFF flit, fdst;			/* the fixed codes (type 1 blocks) */

static void fixed_tables(void)
{
	static int ready;
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
}

static int fixed(ST *s)
{
	fixed_tables();
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

/* ---------------- streaming ----------------
 * DEFLATE from a source a piece at a time into the caller's buffer a piece
 * at a time, through a 32 KB window: a file of any size unpacks in about
 * 40 KB, and the caller takes a turn for GEM between pieces (the
 * guidelines' Speed; EUpdate, 26 Oct 2026: a 210 KB file in one call held
 * a stock Falcon for 3 s, and the whole archive in memory didn't fit
 * beside the accessories). A symbol at a time, so it is slower than
 * inflate_raw's loop; tests/inflate checks the two give the same bytes. */

#define WIN 32768
enum { Z_HEAD, Z_STORED, Z_CODES, Z_END, Z_BAD };

struct inflate {
	int (*get)(void *u, unsigned char *buf, long n);
	void *u;
	unsigned char in[4096];
	int ip, il;			/* the input buffer: next byte, bytes in it */
	unsigned long bits;
	int nbits;
	int state, last;
	long stored;			/* a stored block's bytes left */
	int copy, dist;			/* a match's bytes left, and how far back */
	unsigned long total;		/* bytes put out so far */
	unsigned wpos;			/* the window's next place */
	HUFF lit, dst;
	const HUFF *lc, *dc;
	unsigned char win[WIN];
};

static int z_byte(INFLATE *z)
{
	if (z->ip >= z->il) {
		long n = z->get(z->u, z->in, sizeof(z->in));
		if (n <= 0)
			return -1;
		z->il = (int)n;
		z->ip = 0;
	}
	return z->in[z->ip++];
}

/* a whole byte: from the bits read ahead first (a stored block after a
   compressed one: dropping them lost data, as inflate_raw once did) */
static int z_abyte(INFLATE *z)
{
	if (z->nbits >= 8) {
		int c = (int)(z->bits & 255);
		z->bits >>= 8;
		z->nbits -= 8;
		return c;
	}
	return z_byte(z);
}

static int z_need(INFLATE *z, int n)
{
	while (z->nbits < n) {
		int c = z_byte(z);
		if (c < 0)
			return 0;
		z->bits |= (unsigned long)c << z->nbits;
		z->nbits += 8;
	}
	return 1;
}

static long z_take(INFLATE *z, int n)
{
	long v;
	if (!n)
		return 0;
	if (!z_need(z, n))
		return -1;
	v = (long)(z->bits & ((1UL << n) - 1));
	z->bits >>= n;
	z->nbits -= n;
	return v;
}

static int z_decode(INFLATE *z, const HUFF *h)
{
	int code = 0, first = 0, index = 0, l, count;
	/* the table when the bits are there (topped up as far as the input
	   goes: near its end a short code still decodes bit by bit) */
	while (z->nbits < FAST) {
		int c = z_byte(z);
		if (c < 0)
			break;
		z->bits |= (unsigned long)c << z->nbits;
		z->nbits += 8;
	}
	if (z->nbits >= FAST) {
		unsigned e = h->fast[z->bits & ((1 << FAST) - 1)];
		if (e) {
			z->bits >>= e & 15;
			z->nbits -= e & 15;
			return e >> 4;
		}
	}
	for (l = 1; l < 16; l++) {
		long b = z_take(z, 1);
		if (b < 0)
			return -1;
		code |= (int)b;
		count = h->count[l];
		if (code - count < first)
			return h->sym[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

static int z_dynamic(INFLATE *z)
{
	static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	unsigned char len[320];
	long nlen, ndist, ncode;
	int i;
	nlen = z_take(z, 5);
	ndist = z_take(z, 5);
	ncode = z_take(z, 4);
	if (nlen < 0 || ndist < 0 || ncode < 0)
		return -1;
	nlen += 257;
	ndist += 1;
	ncode += 4;
	if (nlen > 286 || ndist > 30)
		return -1;
	memset(len, 0, sizeof(len));
	for (i = 0; i < ncode; i++) {
		long v = z_take(z, 3);
		if (v < 0)
			return -1;
		len[order[i]] = (unsigned char)v;
	}
	if (build(&z->lit, len, 19))
		return -1;
	for (i = 0; i < nlen + ndist;) {
		int sym = z_decode(z, &z->lit), val = 0;
		long rep;
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
			rep = z_take(z, 2);
			rep = rep < 0 ? -1 : rep + 3;
		} else if (sym == 17) {
			rep = z_take(z, 3);
			rep = rep < 0 ? -1 : rep + 3;
		} else {
			rep = z_take(z, 7);
			rep = rep < 0 ? -1 : rep + 11;
		}
		if (rep < 0 || i + rep > nlen + ndist)
			return -1;
		while (rep--)
			len[i++] = (unsigned char)val;
	}
	if (!len[256] || build(&z->lit, len, (int)nlen) || build(&z->dst, len + nlen, (int)ndist))
		return -1;
	z->lc = &z->lit;
	z->dc = &z->dst;
	return 0;
}

/* a block's header read: its type set up */
static int z_head(INFLATE *z)
{
	long last = z_take(z, 1), type = z_take(z, 2);
	if (last < 0 || type < 0)
		return -1;
	z->last = (int)last;
	if (type == 0) {
		int b[4], i;
		z->bits >>= z->nbits & 7;	/* to the byte: those bits are padding */
		z->nbits -= z->nbits & 7;
		for (i = 0; i < 4; i++)
			if ((b[i] = z_abyte(z)) < 0)
				return -1;
		if (((b[0] | b[1] << 8) ^ 0xffff) != (b[2] | b[3] << 8))
			return -1;
		z->stored = b[0] | b[1] << 8;
		z->state = Z_STORED;
		return 0;
	}
	if (type == 1) {
		fixed_tables();
		z->lc = &flit;
		z->dc = &fdst;
	} else if (type != 2 || z_dynamic(z)) {
		return -1;
	}
	z->state = Z_CODES;
	return 0;
}

static void z_put(INFLATE *z, unsigned char c)
{
	z->win[z->wpos] = c;
	z->wpos = (z->wpos + 1) & (WIN - 1);
	z->total++;
}

/* k bytes (at most the window's size) into the window, as z_put does */
static void win_put(INFLATE *z, const unsigned char *p, unsigned k)
{
	unsigned a = WIN - z->wpos;
	if (a > k)
		a = k;
	memcpy(z->win + z->wpos, p, a);
	memcpy(z->win, p + a, k - a);
	z->wpos = (z->wpos + k) & (WIN - 1);
	z->total += k;
}

INFLATE *inflate_open(int (*get)(void *u, unsigned char *buf, long n), void *u)
{
	INFLATE *z = malloc(sizeof(INFLATE));
	if (!z)
		return 0;
	memset(z, 0, sizeof(*z) - sizeof(z->win));
	z->get = get;
	z->u = u;
	z->state = Z_HEAD;
	return z;
}

/* a code longer than the table's, from the bits there (15 or more):
   its symbol, and its length in *used; -1 when there is none */
static int long_code(const HUFF *h, unsigned long bits, unsigned *used)
{
	unsigned e = slow_code(h, bits);
	if (e == 0)
		return -1;
	*used = e & 15;
	return (int) (e >> 4);
}

long inflate_read(INFLATE *z, unsigned char *out, long n)
{
	long done = 0;
	while (done < n) {
		if (z->copy) {				/* a match, maybe overlapping itself */
			unsigned wpos = z->wpos, from = (wpos - z->dist) & (WIN - 1);
			long k = n - done < z->copy ? n - done : z->copy, i;
			unsigned char *win = z->win, *o = out + done;
			for (i = 0; i < k; i++) {
				unsigned char c = win[from];
				from = (from + 1) & (WIN - 1);
				win[wpos] = c;
				wpos = (wpos + 1) & (WIN - 1);
				o[i] = c;
			}
			z->wpos = wpos;
			z->total += (unsigned long) k;
			z->copy -= (int) k;
			done += k;
			continue;
		}
		if (z->state == Z_HEAD) {
			if (z_head(z)) {
				z->state = Z_BAD;
				return done ? done : -1;
			}
		} else if (z->state == Z_STORED) {
			/* a stored block (EWEB.FNT is nearly all stored blocks): the
			   bytes read ahead in the bits one by one, then the input
			   buffer's in a copy each to the output and the window. A
			   call a byte (z_abyte) took a stock Falcon 28 to 33 s for
			   EWEB.FNT, by where the code lay (the 68030's 256-byte
			   cache missed it and inflate_read's loop in turn) */
			while (z->stored && done < n && z->nbits >= 8) {
				unsigned char c = (unsigned char) z->bits;
				z->bits >>= 8;
				z->nbits -= 8;
				z_put(z, c);
				out[done++] = c;
				z->stored--;
			}
			while (z->stored && done < n) {
				long k;
				if (z->ip >= z->il) {
					long got = z->get(z->u, z->in, sizeof(z->in));
					if (got <= 0) {
						z->state = Z_BAD;
						return done ? done : -1;
					}
					z->il = (int) got;
					z->ip = 0;
				}
				k = z->il - z->ip;
				if (k > z->stored)
					k = z->stored;
				if (k > n - done)
					k = n - done;
				memcpy(out + done, z->in + z->ip, (size_t) k);
				win_put(z, z->in + z->ip, (unsigned) k);
				z->ip += (int) k;
				z->stored -= k;
				done += k;
			}
			if (!z->stored)
				z->state = z->last ? Z_END : Z_HEAD;
		} else if (z->state == Z_CODES) {
			int sym, d;
			long e;
			int pend = 0;		/* a length taken, its distance not yet */
			/* literals and matches straight from the tables while the
			   input buffer has the bits (all but the odd long code): no
			   calls, and the bits and the window kept in locals */
			{
				const unsigned short *fast = z->lc->fast, *dfast = z->dc->fast;
				unsigned long bits = z->bits;
				int nbits = z->nbits, bad = 0;
				unsigned wpos = z->wpos;
				unsigned char *win = z->win;
				const unsigned char *in = z->in + z->ip, *iend = z->in + z->il;
				long k = done;
				while (k < n) {
					unsigned v, l, x;
					int len, dist;
					while (nbits <= 24 && in < iend) {
						bits |= (unsigned long) *in++ << nbits;
						nbits += 8;
					}
					if (nbits < FAST)
						break;
					v = fast[bits & ((1 << FAST) - 1)];
					if (v != 0) {
						l = v & 15;
						v >>= 4;
					} else {
						int c;
						if (nbits < 15 || (c = long_code(z->lc, bits, &l)) < 0)
							break;
						v = (unsigned) c;
					}
					if (v < 256) {
						bits >>= l;
						nbits -= l;
						win[wpos] = (unsigned char) v;
						wpos = (wpos + 1) & (WIN - 1);
						out[k++] = (unsigned char) v;
						continue;
					}
					if (v == 256 || v >= 286)
						break;		/* the block's end, or damaged: below */
					v -= 257;
					x = lext[v];
					if (nbits < (int) (l + x))
						break;
					bits >>= l;
					len = lbase[v] + (int) (bits & ((1UL << x) - 1));
					bits >>= x;
					nbits -= (int) (l + x);
					while (nbits <= 24 && in < iend) {
						bits |= (unsigned long) *in++ << nbits;
						nbits += 8;
					}
					v = nbits >= FAST ? dfast[bits & ((1 << FAST) - 1)] : 0;
					if (v != 0) {
						l = v & 15;
						v >>= 4;
					} else {
						int c = nbits >= 15 ? long_code(z->dc, bits, &l) : -1;
						v = c < 0 ? 30 : (unsigned) c;
					}
					if (v >= 30 || nbits < (int) (l + dext[v])) {
						pend = len;
						break;
					}
					x = dext[v];
					bits >>= l;
					dist = dbase[v] + (int) (bits & ((1UL << x) - 1));
					bits >>= x;
					nbits -= (int) (l + x);
					if ((unsigned long) dist > z->total + (unsigned long) (k - done) || dist > WIN) {
						bad = 1;
						break;
					}
					{
						unsigned from = (wpos - (unsigned) dist) & (WIN - 1);
						long m = n - k < len ? n - k : len, i;
						if (from + m <= WIN && wpos + m <= WIN) {
							/* neither end wraps (most): byte by byte, as
							   the copy may overlap itself */
							const unsigned char *q = win + from;
							unsigned char *p = win + wpos, *o = out + k;
							for (i = 0; i < m; i++)
								o[i] = p[i] = q[i];
							wpos = (wpos + (unsigned) m) & (WIN - 1);
						} else {
							for (i = 0; i < m; i++) {
								unsigned char c = win[from];
								from = (from + 1) & (WIN - 1);
								win[wpos] = c;
								wpos = (wpos + 1) & (WIN - 1);
								out[k + i] = c;
							}
						}
						k += m;
						if (m < len) {
							z->copy = len - (int) m;	/* the rest next time */
							z->dist = dist;
							break;
						}
					}
				}
				z->ip = (int) (in - z->in);
				z->bits = bits;
				z->nbits = nbits;
				z->wpos = wpos;
				z->total += (unsigned long) (k - done);
				done = k;
				if (bad)
					goto bad;
				if (z->copy || done >= n)
					continue;
				if (pend) {
					z->copy = pend;
					goto dist;
				}
			}
			sym = z_decode(z, z->lc);
			if (sym < 0)
				goto bad;
			if (sym < 256) {
				z_put(z, (unsigned char)sym);
				out[done++] = (unsigned char)sym;
				continue;
			}
			if (sym == 256) {
				z->state = z->last ? Z_END : Z_HEAD;
				continue;
			}
			sym -= 257;
			if (sym >= 29 || (e = z_take(z, lext[sym])) < 0)
				goto bad;
			z->copy = lbase[sym] + (int)e;
		dist:
			d = z_decode(z, z->dc);
			if (d < 0 || d >= 30 || (e = z_take(z, dext[d])) < 0)
				goto bad;
			z->dist = dbase[d] + (int)e;
			if ((unsigned long)z->dist > z->total || z->dist > WIN)
				goto bad;
		} else if (z->state == Z_END) {
			break;
		} else {
			return -1;
		}
	}
	return done;
bad:
	z->state = Z_BAD;
	z->copy = 0;
	return done ? done : -1;
}

void inflate_close(INFLATE *z)
{
	free(z);
}
