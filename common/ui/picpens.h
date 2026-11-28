/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * picpens.h - a picture's own colours in pens 192-255 (the guidelines 5.2,
 * decided 22 Nov 2026), for a 256-colour screen: held only while the app's
 * window is on top. picpens_take saves what the pens hold and sets them;
 * picpens_give puts the saved colours back. The app gives them back on
 * close, quit, AC_CLOSE, as soon as another window is on top, and in its
 * crash_cleanup; it draws its picture again after each take and give.
 */
#ifndef AERA_PICPENS_H
#define AERA_PICPENS_H

#define PICPENS_FIRST 192
#define PICPENS_MAX 64

/* n colours (VDI 0-1000) into pens 192 on: 1 done, 0 not (not 8 planes,
   n out of 1..64). Taking again while held sets the new colours, the
   first saved colours kept */
int picpens_take(short vdi, int n, const short rgb[][3]);
/* the saved colours back (nothing when not held) */
void picpens_give(short vdi);
/* the pens held now (0: none) */
int picpens_held(void);
/* 1 when window wh is on top: wind_get(WF_TOP), for an app that
   has no WM_UNTOPPED from its AES */
int picpens_on_top(short wh);

#endif
