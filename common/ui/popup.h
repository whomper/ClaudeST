/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * popup.h - right-click menus (popup.c): the chosen item, or -1.
 *   static const char *const lab[] = { "Open", "-", "~Rename", "\x08Hidden" };
 *   switch (popup(mx, my, lab, 4)) { ... }
 * '-' a separator, '~' disabled, '\x08' (the Atari check mark) checked.
 */
#ifndef AERA_POPUP_H
#define AERA_POPUP_H

#include "gem.h"

short popup(short x, short y, const char *const *labels, short n);
/* the same; with no room below y, it ends above alt_y (the top of the
   row it is for) instead of above y (Claude ST's list menus) */
short popup_alt(short x, short y, short alt_y, const char *const *labels, short n);
extern GRECT popup_area;	/* where the last popup was: what the app repaints */

#endif
