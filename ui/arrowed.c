/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * arrowed.c - which pane a window's arrows scroll (arrowed.h, D37).
 */
#include "gemui.h"
#include "arrowed.h"

short arrow_target_at(const ARROW_PANE *panes, short n, short slider_id, short x, short y)
{
	short i;
	for (i = 0; panes && i < n; i++) {
		const GRECT *r = &panes[i].r;
		if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
			return panes[i].id;
	}
	return slider_id;
}

short arrow_target(const ARROW_PANE *panes, short n, short slider_id)
{
	short x, y, b, k;
	if (!panes || n <= 0)
		return slider_id;		/* nothing to choose from: no AES call */
	graf_mkstate(&x, &y, &b, &k);
	return arrow_target_at(panes, n, slider_id, x, y);
}
