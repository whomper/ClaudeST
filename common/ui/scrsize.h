/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * scrsize.h - the room this machine gives a window (decided 5 Nov 2026: no
 * hard-coded 640 pixels; the room the user's machine has, found and
 * adapted to; EConvert's first).
 *
 * scr_work_size(kind, margin, &w, &h): the inside of a window of that kind
 * (wind_create's) the size of the desktop's work area, less margin pixels
 * at each side, its width in whole cells of the system font: the widest a
 * program's text or picture can be shown on this screen, as EPDF lays out
 * its text column (EConvert makes its pages that wide). At least 64 x 64.
 */
#ifndef SCRSIZE_H
#define SCRSIZE_H

void scr_work_size(short kind, short margin, short *w, short *h);

#endif
