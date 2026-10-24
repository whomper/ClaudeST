/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic.h - pictures for the family's programs (pic.c): what kind a file
 * is, its size from its header, and the picture decoded straight to the
 * size it is shown at, in the Falcon's 16-bit colour with its
 * transparency; then made into the screen's own colours (Floyd-Steinberg)
 * for the modes with a palette.
 *
 * JPEG: ChaN's TJpgDec (third_party/tjpgd), unpacked at 1/2, 1/4 or 1/8
 * when the picture is shown that small, which is much quicker; the kinds
 * it won't take (progressive, SOF1, CMYK...) from their blocks' averages
 * (jpegdc.c), an eighth of their size. PNG: png.c. GIF: NetSurf's
 * libnsgif (third_party/nsgif), its first frame. WebP (a build with
 * PIC_LIBWEBP: EWeb's): Google's libwebp (third_party/libwebp), lossy and
 * lossless, with alpha; not an animation.
 */
#ifndef PIC_H
#define PIC_H

/* what kind of picture */
enum { PIC_NONE, PIC_JPEG, PIC_PNG, PIC_GIF, PIC_BMP, PIC_WEBP, PIC_AVIF, PIC_SVG, PIC_ICO };
int pic_kind(const unsigned char *d, long n);
/* the name of a kind, for messages: "JPEG" */
const char *pic_kind_name(int kind);

/* 1 and its size in pixels, from its header (JPEG, PNG, GIF; WebP
   with PIC_LIBWEBP); 0 when it can't be told */
int pic_size(const unsigned char *d, long n, long *w, long *h);

typedef struct {
	int w, h;
	unsigned short *px;		/* w * h, rows top to bottom, RGB 5-6-5 */
	unsigned char *alpha;		/* w * h, 0 clear .. 255 solid; NULL: all solid */
	int how;			/* PIC_JPEG ...; PIC_JPEG_AVG: from jpegdc */
	int scale;			/* JPEG: unpacked at 1 / (1 << scale) */
} PIC;
#define PIC_JPEG_AVG 100

/* why not */
enum { PIC_OK, PIC_DAMAGED, PIC_UNSUPPORTED, PIC_MEMORY, PIC_TOO_BIG, PIC_STOPPED };
const char *pic_why_text(int why);

/* the picture at w x h (shrunk by averaging, enlarged by repeating);
   NULL, and why. A picture with more than max_px pixels in its file is
   not taken (its memory: four bytes a pixel for a GIF or an interlaced
   PNG); it is checked before anything is decoded. */
PIC *pic_decode(const unsigned char *d, long n, int w, int h, long max_px, int *why);
void pic_free(PIC *p);
/* called now and then while decoding; 0 stops it (PIC_STOPPED). NULL:
   never */
extern int (*pic_poll)(void);

/* ---- a baseline JPEG decoded on the Falcon's DSP (PIC_LIBJPEG builds;
   dsp/jpeg.a56) ---- */
typedef struct {
	/* get the DSP and start a program on it (PIC_DSP_JPEG: dsp/jpeg.a56,
	   PIC_DSP_DITHER: dsp/dither.a56; its bootstrap part and the rest):
	   1 loaded now, 2 it was there already (left clean); 0 if the DSP
	   couldn't be had */
	int (*begin)(int prog);
	/* let it go; clean: the program is back at its commands (the next
	   picture needn't load it again) */
	void (*end)(int clean);
	void (*put)(const unsigned long *w, long n);	/* n words to the DSP */
	/* n words of 16 bits to the DSP (the scan's bytes, two to a word: as
	   $00xxxx there; a port write of 2 bytes, not 3) */
	void (*put16)(const unsigned short *w, long n);
	/* n pixels to the DSP: each $00vvvv, or with an alpha (not NULL)
	   $01vvvv where it is under 128 */
	void (*putpx)(const unsigned short *px, const unsigned char *alpha, long n);
	/* an MCU's word from the DSP: 0, and then rows of cols words, pitch
	   apart, their low 16 bits (pitch in pixels) or their 3 bytes (pitch
	   in bytes), read into d; else the word (the DSP asks for data) */
	unsigned long (*get16)(unsigned short *d, int cols, int rows, long pitch);
	unsigned long (*get24)(unsigned char *d, int cols, int rows, long pitch);
} PIC_DSP;
#define PIC_DSP_JPEG   0
#define PIC_DSP_DITHER 1
/* set by the program where it may use the DSP (atari/dspjpeg.c's
   dspjpeg_port), NULL: the 68030 does it all. The first JPEG decoded
   with it checks it on a known picture; if the DSP gets it wrong, or
   ever stops answering, the 68030 does them from then on */
extern const PIC_DSP *pic_dsp;
/* put and the gets set it when the DSP stops answering, and then do
   nothing */
extern int pic_dsp_failed;
/* 1 (as it starts): the DSP shrinks a JPEG shown smaller too, sending
   only the pixels shown; 0: it sends them all, for the scaler (the tests
   compare the two) */
extern int pic_dsp_shrink;
/* 1 (as it starts): pic_dither has the DSP dither too (16 colours and
   more, pictures big enough to be worth it); 0: the 68030 (the tests
   compare the two) */
extern int pic_dsp_dither;
/* where pictures are dithered (16 colours and more): "DSP", "68030"
   (not to be had, or it failed its check or stopped answering), "-" (not
   tried yet) */
const char *pic_dither_state(void);
/* where JPEGs are transformed: "DSP", "68030" (not to be had, or it
   failed), "-" (not tried yet); "68000" in TJpgDec's builds */
const char *pic_dsp_state(void);

/* ---- the screen's colours ---- */
typedef struct {
	int n;				/* pens, 2 .. 256 */
	unsigned char rgb[256][3];	/* each pen's colour */
	/* 32 x 32 x 32: a colour to its nearest pen + 1 (0: not worked out
	   yet), each component from -128 to 383 in steps of 16 (past the
	   cube's edges too, for the dither's errors); only for more than 4
	   pens (a monochrome ST's 32 KB back), NULL: each worked out */
	unsigned char *near;
	unsigned char order[256];	/* the pens sorted by their red */
} PIC_PAL;
/* n pens of these colours. A PIC_PAL starts zeroed (static, or memset)
   and may be made again; pic_pal_free gives its table back */
void pic_pal_init(PIC_PAL *pal, int n, const unsigned char rgb[][3]);
void pic_pal_free(PIC_PAL *pal);
/* the pen nearest a colour (to 4 bits a component; -128..383 each) */
int pic_pal_nearest(PIC_PAL *pal, int r, int g, int b);
/* the picture as pens, Floyd-Steinberg (serpentine; a colour wanted past
   the cube's edges finds the pen that lies that way); out has w * h
   bytes. The pixels the alpha leaves less than half solid are given pen
   0 (they aren't drawn) and carry no error */
void pic_dither(const PIC *p, PIC_PAL *pal, unsigned char *out);

#endif
