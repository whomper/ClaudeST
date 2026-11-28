/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic_ovl.h - PIC.OVL from the program (atari/pic_ovl.c, src/picovl.h).
 */
#ifndef PIC_OVL_H
#define PIC_OVL_H
#include "pic.h"

/* PIC.OVL is beside TLS.OVL (tls_ovl_path's folder) */
/* a step a turn towards the DSP for pal's pictures: -1 a step was made
   (call again next turn), 1 the DSP is ready, 0 the 68030 dithers (an
   ST, no PIC.OVL, no DSP, or its check failed). near: the file pal's
   table is kept in, key: the palette's sum */
int pic_ovl_step(PIC_PAL *pal, const char *near, long key);
/* PIC.OVL's dither_cells once pic_ovl_step said 1, and its chunky8 once
   it is loaded (src/picovl.h): 1 done, 0 the program does it */
int pic_ovl_cells(const unsigned short *cells, int cw, int ch, int w, int h, int cx,
		  const short *half, PIC_PAL *pal, unsigned char *out, unsigned char *used);
int pic_ovl_chunky8(const unsigned char *pix, int w, int h, long key, short vdi,
		    int sx, int sy, int sw, int sh, int dx, int dy);
/* pal made again for another palette: its table read (when loaded) */
void pic_ovl_palette(PIC_PAL *pal, const char *near, long key);
/* pal's table written, if it learnt more (at the window's close) */
void pic_ovl_near_save(PIC_PAL *pal, const char *near, long key);
void pic_ovl_unload(void);
/* chunky8's copies for key made beforehand, a step a call (pix kept until
   done), so that its draw only copies: 1 ready, -1 call again, 0 not */
int pic_ovl_chunky8_prepare(const unsigned char *pix, int w, int h, long key, short vdi);		/* its memory back (an accessory's store_release) */
/* PIC.OVL 1.4: a picture's own colours in pens 192-255 (the guidelines
   5.2, decided 22 Nov 2026; src/quant.h, ui/picpens.h). quant: up to want
   colours of n 5-6-5 pixels (skip: a value left out, -1 none), their
   count, 0 without PIC.OVL 1.4. pens_take: those n (1..64) into pens 192
   on, 1 done; the caller checks the screen has 8 planes and its window is
   on top. pens_give: back as they were, on losing the top, close, quit,
   AC_CLOSE and in crash_cleanup (pic_ovl_unload calls it too); pens_held:
   1 while held. A picture is drawn again after each take and give */
int pic_ovl_quant(const unsigned short *px, long n, long skip, int want, short rgb[][3]);
/* PIC.OVL 1.5: quant as a job (src/quant.h): begin (NULL with 1.4 or no
   memory; px kept until the end), a step a turn of about budget pixel
   visits (some 20 68030 instructions a visit: 5,000 is about 25 ms on a
   16 MHz Falcon) giving -1 while there is more, else the count with the
   colours in rgb (the same array each step), and end, done or not. The
   job must end before pic_ovl_unload */
void *pic_ovl_quant_begin(const unsigned short *px, long n, long skip, int want);
int pic_ovl_quant_step(void *job, long budget, short rgb[][3]);
void pic_ovl_quant_end(void *job);
int pic_ovl_pens_take(short vdi, int n, const short rgb[][3]);
void pic_ovl_pens_give(void);
int pic_ovl_pens_held(void);
#endif
