/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * panebar.h - a pane's own vertical scroll bar (the guidelines, 5.1; Erez,
 * 24 Oct 2026: "AES look for pane bars"). A window has one set of sliders
 * from the AES, and the family's windows have several panes, so each pane
 * that scrolls has a bar of its own at its right. It is made of AES
 * objects (an arrow box at each end, the track, the slider), drawn by
 * objc_draw, so it takes the AES's look: 3D where the AES draws it and
 * the setting is on (dlg_3d, 5.6), its theme's objects under XaAES or
 * N.AES, plain as TOS's window bars elsewhere. The bar is as wide as the
 * AES's own slider column (wind_calc), its arrows as high as its gadgets.
 *
 * The app keeps the numbers (the rows there are, the rows shown, the
 * first shown) and gives a callback that scrolls; pb_click does the
 * arrows (held: some 15 rows a second), the track (held: a page at a
 * time until the slider reaches the pointer) and the slider (dragged).
 *
 * objc_draw draws on the screen: an app that draws its window off the
 * screen first (ECal's and EContact's way) draws the bars after the copy,
 * in the same rectangles.
 */
#ifndef PANEBAR_H
#define PANEBAR_H

#include "gemui.h"

typedef struct {
	GRECT r;		/* the bar, on the screen (pb_width() wide) */
	long total;		/* rows (or lines, or pixels: any unit) there are */
	long shown;		/* of them in the pane at once */
	long top;		/* the first shown */
} PANEBAR;

/* the bar's width and an arrow box's height: the AES's gadget size */
short pb_width(void);
short pb_arrow_h(void);

/* draw it, within clip (the screen's coordinates), as objc_draw does:
   the caller holds wind_update and has the mouse off, as for any
   redraw */
void pb_draw(const PANEBAR *b, const GRECT *clip);

/* where on the bar a point is */
enum { PB_NONE, PB_UP, PB_DOWN, PB_PAGE_UP, PB_PAGE_DOWN, PB_SLIDER };
int pb_part(const PANEBAR *b, short x, short y);

/* the slider: *sy, *sh (0 when everything is shown) in a track from *ty,
   *th high */
void pb_slider(const PANEBAR *b, short *ty, short *th, short *sy, short *sh);

/* the first row for the slider's top at y (a drag) */
long pb_top_at(const PANEBAR *b, short y);

/* a press on the bar at x, y, its button still down: scroll(n) moves the
   pane by n rows (it updates b->top and draws what moved, the bar
   included: pb_moved says which part of the bar) and returns 0 at an end; goto_top(t)
   puts row t first. Returns when the button is up. */
void pb_click(PANEBAR *b, short x, short y, int (*scroll)(long n), int (*goto_top)(long t));

/* the part of the bar to draw again after top moved from old_top: the
   slider's old and new places (all of it took an 8 MHz ST 70 ms) */
GRECT pb_moved(const PANEBAR *b, long old_top);

#endif
