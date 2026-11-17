/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * offpage.h - a screen-sized EdDI off-screen bitmap (NVDI 3 and later,
 * fVDI), so that a page of a pane is drawn there and copied to the screen
 * in one go instead of appearing row by row (Erez, 13 Nov 2026). As
 * ECal's and EContact's ui/win.c (their EdDI way): used only when a test
 * at the start finds a line drawn on it really in its memory. Without
 * EdDI (TOS's and EmuTOS's own VDI) nothing changes. From EMail 2e9ae4bb;
 * the names are offpage_* here (offdraw.h has off_begin, off_end, off_state).
 */
#ifndef OFFPAGE_H
#define OFFPAGE_H

#include "gem.h"

/* after the VDI is open and the font set: the screen's handle, its size
   and planes, and the memory that must stay free beside the bitmap (a
   screen's worth, w x hgt x planes bits, is taken); 1 if a bitmap is
   there */
int offpage_init(short screen, short w, short hgt, short nplanes, long spare);
/* the bitmap's VDI handle, 0 when there is none */
short offpage_handle(void);
/* GDOS fonts loaded on the screen's workstation since: on the bitmap too */
void offpage_fonts(void);
/* the box x, y, w, hgt (screen coordinates, as drawn on the bitmap)
   copied to the screen through the screen's handle h; the caller hides
   the mouse and holds wind_update */
void offpage_copy(short h, short x, short y, short w, short hgt);
/* at the end: the bitmap closed, its memory given back */
void offpage_exit(void);
/* what the start-up test found, for the test build's trace */
const char *offpage_state(void);

#endif
