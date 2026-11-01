/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * offdraw.h - a small piece of TOS's own screen drawn off it and shown at
 * once, so what changes on it changes in one step, with no flicker.
 *
 * A piece drawn again in place (a transfer's progress row every second, a
 * pane's header with its count) showed its ground for a frame or two on a
 * stock Falcon before its words came back (Erez, 1 Nov 2026: "the
 * progress displays flicker every second"). Between off_begin and
 * off_end the logical screen is a buffer holding the piece's rows, the
 * screen's own bytes first; whatever is drawn goes there (the VDI, the
 * AES's objc_draw, text.c's drawing into Logbase), and off_end copies the
 * piece to the screen in one memory copy, each pixel once.
 *
 *     if (off_begin(vdi_h, x, y, w, h)) { draw ...; off_end(); }
 *     else draw ...                       (on the screen, as before)
 *
 * Only under TOS's own single-tasking AES with the logical and shown
 * screens the same, and once a test pixel has landed in the buffer (the
 * first time: the VDI follows the logical screen; NVDI may not), and for
 * a piece of at most OFF_ROWS rows. The caller holds wind_update and has
 * the mouse hidden from off_begin to off_end (its pointer is drawn into
 * the logical screen). off_begin sets vh's clip to the piece; the caller
 * may narrow it, never widen it, and clips everything else it draws in
 * between to the piece: objc_draw's clip, its own raster copies (the
 * VDI's ignore the clip). A draw outside the piece's rows goes to the
 * memory round the buffer. The buffer, in ST RAM, is taken at the first
 * use and kept.
 */
#ifndef OFFDRAW_H
#define OFFDRAW_H

#define OFF_ROWS 64

int off_begin(short vh, short x, short y, short w, short h);
void off_end(void);

/* the tests': 1 the VDI draws into the logical screen, -1 not, 0 not
   tried yet */
int off_state(void);

#endif
