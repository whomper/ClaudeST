/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aerapens.c - a picture made into an Atari screen's pens, on the Pi's
 * gateway (AERA-Gateway's webgate), with the very code the Atari runs:
 * common/src/pic.c's decode, scaler and dither, in both of EWeb's
 * builds: as EWEB.PRG decodes (libjpeg-turbo, -DPIC_LIBJPEG; the 68030's
 * way, the DSP's dither being the same pens to the bit) and as
 * EWEBST.PRG does (TJpgDec, jpegdc; pic.c built again with pens_tj.h).
 * The pens are the Atari's byte for byte (tests/pic/pens.py runs it
 * against the same code as 68000 and 68030 code under QEMU).
 *
 *   aerapens [-v] < REQUEST > ANSWER
 *
 * It reads one request on stdin and writes its answer on stdout, and
 * opens nothing else (webgate runs it behind a socket, one a request,
 * with little memory and time). -v: a line on stderr first, its builds
 * and limits. All numbers big-endian. The request:
 *
 *   0  "AEPQ", 1 (the form), the edition ('l': EWEB.PRG's, 't':
 *      EWEBST.PRG's; 'v': nothing but the answer's line, for webgate's
 *      log), the screen's pens N - 1, 0
 *   8  the width and height shown (2 bytes each; height 0: the width at
 *      most, its own if smaller, as high as its shape says), the
 *      picture's length (4 bytes)
 *   16 the N colours, R G B each; then the picture (JPEG, PNG or GIF)
 *
 * The answer: a line "aerapens COMMON BUILD" (the AERA-Atari commit it
 * was made from and its own build's name), then EWeb's pens:
 *
 *   0  "EWPN", 1 (the form), the bits a pen (1, 2, 4, 8), flags (1: a
 *      mask follows the pens), N - 1
 *   8  width, height, the picture's own width and height (2 bytes each)
 *   16 the colours' CRC-32 (zlib's), 4 bytes
 *   20 the pens, a row at a time, high bits first, each row whole bytes;
 *      then, with flag 1, the mask the same way (a bit a pixel: 1 where
 *      it is shown, its alpha 128 or more)
 *
 * Limits: a picture of at most PENS_IN_PX pixels (from its header,
 * before anything is made), made at most PENS_OUT_PX, each side 4096.
 * Exit 0, or 1 (a request not of this form), 2 (not made: a kind it
 * isn't, damaged, too big), 3 (out of memory); a line on stderr says why.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pic.h"

#ifndef PENS_COMMON
#define PENS_COMMON "unknown"
#endif
#ifndef PENS_BUILD
#define PENS_BUILD "unknown"
#endif
#define PENS_IN_PX  16000000L		/* the picture's own pixels, at most */
#define PENS_OUT_PX 4194304L		/* the pens made, at most (EWeb's own limit) */
#define PENS_IN_MAX (32L << 20)		/* the request's bytes, at most */

/* pic.c's EWEBST.PRG build (pens_tj.h) */
int tj_pic_size(const unsigned char *d, long n, long *w, long *h);
PIC *tj_pic_decode(const unsigned char *d, long n, int w, int h, long max_px, int *why);
void tj_pic_free(PIC *p);
void tj_pic_pal_init(PIC_PAL *pal, int n, const unsigned char rgb[][3]);
void tj_pic_pal_free(PIC_PAL *pal);
void tj_pic_dither(const PIC *p, PIC_PAL *pal, unsigned char *out);

typedef struct {
	int (*size)(const unsigned char *d, long n, long *w, long *h);
	PIC *(*decode)(const unsigned char *d, long n, int w, int h, long max_px, int *why);
	void (*free)(PIC *p);
	void (*pal_init)(PIC_PAL *pal, int n, const unsigned char rgb[][3]);
	void (*pal_free)(PIC_PAL *pal);
	void (*dither)(const PIC *p, PIC_PAL *pal, unsigned char *out);
} BUILD;

static const BUILD lj = { pic_size, pic_decode, pic_free, pic_pal_init, pic_pal_free, pic_dither };
static const BUILD tj = { tj_pic_size, tj_pic_decode, tj_pic_free, tj_pic_pal_init, tj_pic_pal_free, tj_pic_dither };

static const char answer_line[] = "aerapens " PENS_COMMON " " PENS_BUILD "\n";

static unsigned long crc32(const unsigned char *d, long n)
{
	unsigned long c = 0xFFFFFFFFUL;
	long i;
	int k;
	for (i = 0; i < n; i++) {
		c ^= d[i];
		for (k = 0; k < 8; k++)
			c = c & 1 ? (c >> 1) ^ 0xEDB88320UL : c >> 1;
	}
	return c ^ 0xFFFFFFFFUL;
}

static long be(const unsigned char *d, int n)
{
	long v = 0;
	while (n-- > 0)
		v = v << 8 | *d++;
	return v;
}

static void put16(unsigned char *o, long v)
{
	o[0] = (unsigned char) (v >> 8);
	o[1] = (unsigned char) v;
}

/* the request in (n bytes) made into the pens' answer: 0 and *out
   (malloc'ed, *len bytes, the line first), or the exit status with why */
int pens_make(const unsigned char *in, long n, unsigned char **out, long *len, const char **why)
{
	const BUILD *b;
	long w, h, np, plen, sw, sh, row, mrow, size, y, x, line = (long) sizeof answer_line - 1;
	unsigned char *pens, *o, *ans;
	unsigned long crc;
	int depth, pw;
	PIC_PAL pal;
	PIC *p;
	*out = NULL;
	*len = 0;
	if (n < 16 || memcmp(in, "AEPQ\1", 5) != 0 || (in[5] != 'l' && in[5] != 't' && in[5] != 'v')) {
		*why = "not a request of this form";
		return 1;
	}
	if (in[5] == 'v') {
		/* which it is: the line alone */
		if ((*out = malloc((size_t) line)) == NULL) {
			*why = "out of memory";
			return 3;
		}
		memcpy(*out, answer_line, (size_t) line);
		*len = line;
		return 0;
	}
	b = in[5] == 'l' ? &lj : &tj;
	np = in[6] + 1;
	w = be(in + 8, 2);
	h = be(in + 10, 2);
	plen = be(in + 12, 4);
	if (np < 2 || w < 1 || w > 4096 || h > 4096 || plen < 8 || n != 16 + np * 3 + plen) {
		*why = "its sizes aren't right (2 to 256 pens, 1 to 4096 wide, its length)";
		return 1;
	}
	in += 16;
	if (!b->size(in + np * 3, plen, &sw, &sh) || sw < 1 || sh < 1) {
		*why = "not a picture it takes";
		return 2;
	}
	if (sw > 0xFFFF || sh > 0xFFFF || sw > PENS_IN_PX / sh) {
		*why = "too many pixels";
		return 2;
	}
	if (h == 0) {
		/* W wide at most, its shape kept */
		if (w > sw)
			w = sw;
		h = (sh * w + sw / 2) / sw;
		if (h < 1)
			h = 1;
	}
	if (h > 4096 || w * h > PENS_OUT_PX) {
		*why = "too big to make";
		return 2;
	}
	memset(&pal, 0, sizeof pal);
	b->pal_init(&pal, (int) np, (const unsigned char (*)[3]) in);
	crc = crc32(in, np * 3);
	p = b->decode(in + np * 3, plen, (int) w, (int) h, PENS_IN_PX, &pw);
	if (p == NULL) {
		b->pal_free(&pal);
		*why = pic_why_text(pw);
		return pw == PIC_MEMORY ? 3 : 2;
	}
	depth = np <= 2 ? 1 : np <= 4 ? 2 : np <= 16 ? 4 : 8;
	row = (w * depth + 7) / 8;
	mrow = p->alpha != NULL ? (w + 7) / 8 : 0;
	size = line + 20 + h * (row + mrow);
	pens = malloc((size_t) (w * h));
	ans = calloc(1, (size_t) size);
	if (pens == NULL || ans == NULL) {
		free(pens);
		free(ans);
		b->free(p);
		b->pal_free(&pal);
		*why = "out of memory";
		return 3;
	}
	b->dither(p, &pal, pens);
	b->pal_free(&pal);
	memcpy(ans, answer_line, (size_t) line);
	o = ans + line;
	memcpy(o, "EWPN", 4);
	o[4] = 1;
	o[5] = (unsigned char) depth;
	o[6] = p->alpha != NULL ? 1 : 0;
	o[7] = (unsigned char) (np - 1);
	put16(o + 8, w);
	put16(o + 10, h);
	put16(o + 12, sw);
	put16(o + 14, sh);
	o[16] = (unsigned char) (crc >> 24);
	o[17] = (unsigned char) (crc >> 16);
	o[18] = (unsigned char) (crc >> 8);
	o[19] = (unsigned char) crc;
	o += 20;
	for (y = 0; y < h; y++, o += row) {
		const unsigned char *s = pens + y * w;
		for (x = 0; x < w; x++) {
			long bit = x * depth;
			o[bit >> 3] |= (unsigned char) (s[x] << (8 - depth - (bit & 7)));
		}
	}
	if (p->alpha != NULL)
		for (y = 0; y < h; y++, o += mrow)
			for (x = 0; x < w; x++)
				if (p->alpha[y * w + x] >= 128)
					o[x >> 3] |= (unsigned char) (0x80 >> (x & 7));
	free(pens);
	b->free(p);
	*out = ans;
	*len = size;
	return 0;
}

#ifdef PENS_FUZZ
/* libFuzzer (tests/pic/Makefile, make fuzzpens): any request, under
   AddressSanitizer and UBSan */
int LLVMFuzzerTestOneInput(const unsigned char *d, size_t n)
{
	unsigned char *out;
	long len;
	const char *why;
	if (pens_make(d, (long) n, &out, &len, &why) == 0)
		free(out);
	return 0;
}
#else
int main(int argc, char **argv)
{
	unsigned char head[16], *in, *out;
	long np, plen, n, len;
	const char *why = "";
	int r;
	if (argc > 1 && strcmp(argv[1], "-v") == 0)
		fprintf(stderr, "aerapens: AERA-Atari %s, build %s; pictures of %ld pixels at most, made %ld at most\n",
			PENS_COMMON, PENS_BUILD, PENS_IN_PX, PENS_OUT_PX);
	/* the head, then exactly the rest it says */
	if (fread(head, 1, 16, stdin) != 16 || memcmp(head, "AEPQ\1", 5) != 0) {
		fprintf(stderr, "aerapens: not a request of this form\n");
		return 1;
	}
	np = head[6] + 1;
	plen = be(head + 12, 4);
	if (head[5] == 'v') {
		np = plen = 0;		/* the head alone */
	} else if (plen < 8 || plen > PENS_IN_MAX) {
		fprintf(stderr, "aerapens: a picture of %ld bytes\n", plen);
		return 2;
	}
	n = 16 + np * 3 + plen;
	in = malloc((size_t) n);
	if (in == NULL) {
		fprintf(stderr, "aerapens: out of memory\n");
		return 3;
	}
	memcpy(in, head, 16);
	if (fread(in + 16, 1, (size_t) (n - 16), stdin) != (size_t) (n - 16)) {
		fprintf(stderr, "aerapens: the request was cut short\n");
		return 1;
	}
	r = pens_make(in, n, &out, &len, &why);
	free(in);
	if (r != 0) {
		fprintf(stderr, "aerapens: %s\n", why);
		return r;
	}
	if (fwrite(out, 1, (size_t) len, stdout) != (size_t) len || fflush(stdout) != 0) {
		fprintf(stderr, "aerapens: can't write\n");
		return 1;
	}
	free(out);
	return 0;
}
#endif
