/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* scrsize.c - the room this machine gives a window (see scrsize.h) */
#include "gemui.h"
#include "scrsize.h"

void scr_work_size(short kind, short margin, short *w, short *h)
{
	short dx, dy, dw, dh, x, y, ww, wh, cell = gl_wchar > 0 ? gl_wchar : 8;

	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	wind_calc(WC_WORK, kind, dx, dy, dw, dh, &x, &y, &ww, &wh);
	ww = (short)((ww - 2 * margin) / cell * cell);
	wh = (short)(wh - 2 * margin);
	*w = ww < 64 ? 64 : ww;
	*h = wh < 64 ? 64 : wh;
}
