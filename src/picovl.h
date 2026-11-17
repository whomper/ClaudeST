/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * picovl.h - PIC.OVL, the Falcon's DSP for pictures in a file of its own
 * (common/picovl): pic.c's dither on the DSP (dsp/dither.a56, the same
 * pens as the 68030's to the bit), built for the 68030. A program that
 * wants it carries the file next to it, as TLS.OVL; an ST never loads
 * it, a Falcon with a DSP the first time a picture is dithered (the
 * program's side: atari/pic_ovl.c). Without it, or when the DSP can't be
 * had, the 68030 does the work as before.
 *
 * As tlsovl.h: two tables, each with its size first, only ever growing at
 * the end; a change that can't be made by adding raises PICOVL_MAJOR.
 */
#ifndef PICOVL_H
#define PICOVL_H
#include <stddef.h>
#include "pic.h"

#define PICOVL_MAGIC 0x5049434FUL	/* "PICO" */
#define PICOVL_MAJOR 1

/* the VDI's memory form (GEM's MFDB, the same layout) */
typedef struct {
	void *fd_addr;
	short fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} PIC_MFDB;

/* what the program lends PIC.OVL: its memory, clock, log, files and VDI */
typedef struct {
	long size;			/* sizeof(PIC_HOST) as the program knows it */
	void *(*malloc)(size_t n);
	void *(*calloc)(size_t n, size_t m);
	void *(*realloc)(void *p, size_t n);
	void (*free)(void *p);
	unsigned long (*pf_ms)(void);
	void (*conn_log)(const char *who, const char *dir, const char *text, long n);
	long (*supexec)(void *f);	/* tos.h's tos_supexec */
	long (*get_cookie)(long id);
	int (*pf_open)(const char *path, int mode);	/* for near_load and near_save */
	long (*pf_read)(int h, void *buf, long n);
	long (*pf_write)(int h, const void *buf, long n);
	void (*pf_close)(int h);
	void (*vr_trnfm)(short h, PIC_MFDB *src, PIC_MFDB *dst);	/* for chunky8 */
	void (*vro_cpyfm)(short h, short mode, const short *pxy, PIC_MFDB *src, PIC_MFDB *dst);
} PIC_HOST;

/* what PIC.OVL offers */
typedef struct {
	unsigned long magic;		/* PICOVL_MAGIC */
	short major;			/* PICOVL_MAJOR */
	short size;			/* sizeof(PIC_OVL) as the overlay knows it */
	char version[8];		/* PIC.OVL's release, "1.0" */
	/* 1 when it can work with this program's PIC_HOST (a 68020 or later;
	   with or without a DSP); first, once */
	int (*init)(const PIC_HOST *h);
	/* the DSP made ready for pal's pens: its program loaded and checked
	   on a known picture against the 68030's (some 100 ms on a stock
	   Falcon, once): 1 ready, 0 the program dithers on the 68030 (no DSP,
	   busy, 2 pens, or the check failed) */
	int (*prepare)(PIC_PAL *pal);
	/* the picture's pens on the DSP (pic_dither's, the same pens): 1 done,
	   0 the program does it on the 68030. poll: pic_poll's, every 4 rows
	   (NULL: none) */
	int (*dither)(const PIC *p, PIC_PAL *pal, unsigned char *out, int (*poll)(void));
	const char *(*dither_state)(void);	/* "DSP", "68030" or "-" (not tried yet) */
	/* pal's table of nearest pens (pal->near, 32 KB) from path / to path,
	   kept for the palette whose sum is key: the 68030's dither and the
	   check find in it what they'd look for (half of a first photo's
	   dither in 256 colours). near_save writes only when the table knows
	   more than when it was read */
	void (*near_load)(PIC_PAL *pal, const char *path, long key);
	void (*near_save)(PIC_PAL *pal, const char *path, long key);
	/* picture cells (cw x ch, 5-6-5) shown at w x h (each pixel the cell
	   under it, no smoothing) dithered on the DSP: 1 done, 0 the program
	   does it. The mask, if any, is a span on each row, centre column cx
	   and half[y] each side (an ellipse, a round photo, any shape
	   symmetric about cx); outside it out gets 255 and no error is carried.
	   used (256 bytes, may be NULL) gets 1 for each pen drawn. Who may use
	   it: any program with PIC.OVL, for a small picture shown smaller or
	   larger than it is kept (EContact's contact photos) */
	int (*dither_cells)(const unsigned short *cells, int cw, int ch, int w, int h, int cx,
			    const short *half, PIC_PAL *pal, unsigned char *out, unsigned char *used);
	/* an 8-bit chunky picture (w x h bytes, VDI pens; 0 and 255 not
	   drawn: what is under them stays) to the screen of an 8-plane
	   workstation vdi, its part from sx, sy (sw x sh) at dx, dy, in two
	   copies: a mask clears its pixels, the picture is ORed in (a copy a
	   pen was some 200 calls for a photo in 256 colours: 385 ms under
	   NVDI on a 4 MB Falcon, 70 this way). The two are made once and kept
	   for key, w and h (the caller's: another picture, another key). 1
	   done, 0 not (its memory not ST RAM, which the blitter can't reach,
	   or none): the program draws it its own way. Who may use it: any
	   program with PIC.OVL drawing a small picture in 256 colours,
	   transparent where it isn't (EContact's round photos) */
	int (*chunky8)(const unsigned char *pix, int w, int h, long key, short vdi,
		       int sx, int sy, int sw, int sh, int dx, int dy);
} PIC_OVL;

#endif
