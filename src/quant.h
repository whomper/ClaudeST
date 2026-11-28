/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * quant.h - a picture's own colours (quant.c): a median cut of its 5-6-5
 * pixels to at most want colours, for the pens a picture may borrow in 256
 * colours (the guidelines 5.2, ui/picpens.h). Run in PIC.OVL (68020 and up)
 * or linked by a program that has the room.
 */
#ifndef AERA_QUANT_H
#define AERA_QUANT_H

/* px: n pixels, 5-6-5 (pic.c's PIC rows, EContact's cells); skip: a value
   left out (transparent: the outside of a round photo), or -1. Up to want
   (1..256) colours into rgb, VDI's 0-1000: their count, 0 with no memory
   or no pixels. Boxes are cut at the median of their widest side, the
   box with the most pixels times its width first; each colour is its
   box's mean */
int quant_565(const unsigned short *px, long n, long skip, int want, short rgb[][3]);

/* The same as a job, for a caller whose turn must stay short: begin keeps
   px until the end (NULL: no memory, no pixels or want < 1); each step
   does about budget pixel visits (a 64 x 64 picture to 63 colours is some
   90,000 in all; one cut is done whole, three visits a pixel of its box)
   and gives -1 while there is more, else the count, its colours in rgb
   (which must stay the same array from step to step); end lets it go,
   done or not */
typedef struct QUANT_JOB QUANT_JOB;
QUANT_JOB *quant_begin(const unsigned short *px, long n, long skip, int want);
int quant_step(QUANT_JOB *j, long budget, short rgb[][3]);
void quant_end(QUANT_JOB *j);

#endif
