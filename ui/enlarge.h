/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * enlarge.h - the family's enlarge mark (Erez, 5 Nov 2026: "Click to
 * enlarge should be an icon not a text. Place it as a visible watermark at
 * the top right of the picture"; EPDF's first).
 *
 * ENLARGE_SIZE pixels square: a white square, a black edge and a
 * two-headed arrow from its lower left to its upper right, drawn in pens
 * 0 and 1 (white and black on every screen). enlarge_mark(h, x, y) draws
 * it with its top left at x, y, inside the clip the caller set; put it 3
 * pixels in from a picture's top right corner (x = right - 14, y = top +
 * 3). It leaves the fill solid in MD_REPLACE.
 */
#ifndef ENLARGE_H
#define ENLARGE_H

#define ENLARGE_SIZE 11

void enlarge_mark(short h, short x, short y);

#endif
