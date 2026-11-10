/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * arrowed.h - which pane a window's WM_ARROWED scrolls (the guidelines' 5.1, UI standards WINDOWS.md; D37,
 * 9 Nov 2026). One rule for the family: the window's arrows scroll the scrolling pane under the pointer,
 * else the pane that owns the window's sliders. Some AESes send a mouse wheel as WM_ARROWED (the pointer
 * is then over a pane); a click on the window's own arrow or track has the pointer on the widget, outside
 * every pane, and goes to the slider's pane as it always did. No other wheel message is handled (no wheel
 * unless Erez asks).
 *
 * The app lists its scrolling panes, each with the rectangle it owns on the screen and an id of its own
 * (a pane number, a pointer; 0 or more), and asks when the message comes:
 *
 *	static const ARROW_PANE panes[] = { { list_rect, PANE_LIST }, { text_rect, PANE_TEXT } };
 *	case WM_ARROWED:
 *		scroll_pane(arrow_target(panes, 2, PANE_TEXT), msg[4]);	msg[4] is the action (WA_*)
 *
 * The rectangles are the panes' areas as drawn now (not the whole window): a pane with no scrolling to do
 * (it fits, or it has no bar) is left out of the list, so the arrows go on to the slider's pane.
 */
#ifndef AERA_ARROWED_H
#define AERA_ARROWED_H

#include "gem.h"

typedef struct {
	GRECT r;	/* the pane's area on the screen */
	short id;	/* the app's name for it */
} ARROW_PANE;

/* the id of the pane that has the pointer (graf_mkstate), else slider_id. n panes in the list */
short arrow_target(const ARROW_PANE *panes, short n, short slider_id);

/* the same for a pointer the app already read (x, y): no AES call */
short arrow_target_at(const ARROW_PANE *panes, short n, short slider_id, short x, short y);

#endif
