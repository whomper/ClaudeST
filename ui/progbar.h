/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The family's progress bar (the guidelines 5.9; Erez, 3 Nov 2026): one
 * bar for a whole job, filling from left to right, its colours fixed
 * along its width.
 *   - 256 colours: blue, dark navy at the left edge to light sky blue at
 *     the right, in PB_SHADES steps;
 *   - 16 colours: pens 1 to 15 in order, left to right, less any pen the
 *     colour of the bar's own ground;
 *   - any other screen (mono, 4 colours, true colour): black.
 * Filling reveals the colours where they lie, so a bar nearly full shows
 * them all. The words beside it say what is being done.
 *
 * progbar_pens() finds the pens once, for the screen and the palette
 * loaded: at the start, and again after the app changes pens 16 and up
 * (EWeb's pictures' palette). progbar_fill() draws a part of the bar,
 * from..to in 1024ths of its width: an app draws what is new since the
 * last turn (the guidelines' Speed), and the ground beyond the filled part
 * itself.
 */
#ifndef PROGBAR_H
#define PROGBAR_H

#define PB_SHADES 16	/* the 256-colour bar's steps */
#define PB_FULL 1024	/* the whole bar */

/* the bar's pens on this screen, for a bar on ground (a pen); the
   planes are gemui.h's */
void progbar_pens(short h, short ground);

/* the bar's part from..to (0 to PB_FULL), in the box x, y, w, hh */
void progbar_fill(short h, short x, short y, short w, short hh, short from, short to);

/* the pens found, left to right (for the tests) */
short progbar_npens(void);
short progbar_pen(short i);

#endif
