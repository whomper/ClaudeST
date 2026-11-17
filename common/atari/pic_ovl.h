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
void pic_ovl_unload(void);		/* its memory back (an accessory's store_release) */
#endif
