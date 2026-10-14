/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * help.h - the Help key (the AERA guidelines, 4.3). help_show() opens
 * the app's hypertext in the viewer that runs: EHypView first, then
 * ST-Guide and HypView, with VA_START "C:\FOLDER\NAME.HYP page" (or
 * "*:\NAME.HYP page", which the viewer looks for along its own paths).
 *
 * The strings go in memory the other program may read: global memory
 * (Mxalloc mode 0x2B) under MiNT and MagiC, whose memory protection
 * would stop it otherwise; a static buffer on plain TOS, where an
 * accessory has no memory of its own after start-up and nothing is
 * protected. From EContact's shared_buf and ECal's help_open.
 */
#ifndef AERA_HELP_H
#define AERA_HELP_H

/* at start-up (an accessory must: it can't take memory later) */
void help_init(void);

/* NAME.HYP from dir (0 or "": only "*:\NAME.HYP"), at page (0: its main
   page). 1 when a viewer got it, 0 when none runs: the app then shows
   its keys or says how to get a viewer */
int help_show(const char *dir, const char *hyp, const char *page);

/* av_send(), for another program: av.h */
#include "av.h"

#endif
