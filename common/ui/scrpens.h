/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * scrpens.h - the screen's colours by hardware register, as its bitmaps
 * hold them (a pixel's planes give the register, not the VDI pen): for a
 * dither straight into the screen's bitmap (pic.c's PIC_PAL with these
 * colours, its pens then the registers) and for pictures made ready for
 * one screen (EPDF's Faster display). From EPDF (19 Nov 2026).
 */
#ifndef SCRPENS_H
#define SCRPENS_H

/* VDI pen n's colour register (TOS's table for 2 to 16 colours; with 256
   the first 16 so, pen 1 the last register and pen 255 register 15, the
   rest as they are; mono: the pen) */
int scr_pen_reg(int pen, int planes);
/* the screen's 1 << planes registers' colours (0-255, vq_color's 0-1000
   scaled) into rgb, by register; mono is white and black without asking
   the VDI (EmuTOS's answers there ran a dither away). Returns the
   registers' count, 0 for more than 8 planes (rgb untouched) */
int scr_pens(short vdi, int planes, unsigned char rgb[256][3]);

#endif
