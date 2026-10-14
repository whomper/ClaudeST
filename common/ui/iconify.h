/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * iconify.h - iconified windows (the AERA guidelines, 5.1), ECal's: the
 * iconifier on a window where the AES has it (AES 4.1, MagiC, XaAES,
 * N.AES), and the iconified window drawn as the app's icon with a short
 * status under it when there is room (today's date, unread mail, how far
 * a transfer has got). Drawn with the app's own primitives (gemui.h).
 *
 *   wind_create(icon_kind(NAME | CLOSER | ...), ...);
 *   case WM_ICONIFY: case WM_ALLICONIFY:
 *       wind_set(h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]); ...
 *   case WM_UNICONIFY:
 *       wind_set(h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]); ...
 *   WM_REDRAW, iconified: icon_redraw(h, &area, icon_bits, icon16_px, "3 new");
 */
#ifndef AERA_ICONIFY_H
#define AERA_ICONIFY_H

#include "gem.h"

/* kind, with the iconifier (SMALLER) when the AES can iconify */
short icon_kind(short kind);

/* the iconified window's work area (in clip): the 32x32 icon, in colour
   with 16 colours or more (icon16 may be 0), and status under it in the
   6x6 font (an iconified window is usually 72x72) */
void icon_draw(const GRECT *work, const GRECT *clip, const unsigned short *icon,
	       const unsigned char *icon16, const char *status);

/* the iconified window h drawn where the AES says (area: its WM_REDRAW
   rectangle, 0 for all of it): its rectangle list over its work area as
   it is now, not as it was before it shrank */
void icon_redraw(short h, const GRECT *area, const unsigned short *icon,
		 const unsigned char *icon16, const char *status);

#endif
