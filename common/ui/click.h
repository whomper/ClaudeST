/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * click.h - a press on a list's row, or on a button the program draws
 * itself, selected on press and done on release (the guidelines, 5.8;
 * Erez, 1 Nov 2026), while the program's own work goes on.
 *
 * The event loop asks evnt_multi for one click, so the AES hands a press
 * over at once (asking for two held every single press back for the
 * double-click time). On a left press at (x, y) on an object (a row, a
 * button):
 *
 *     dbl = press_double(id, x, y);     the second of a double click?
 *     ... draw the row selected, the button pressed ...
 *     switch (press_track(&r, x, y, idle, msg, &dx, &dy)) {
 *     case PRESS_IN:   let go over r: its action (dbl: the double click's)
 *     case PRESS_OUT:  let go elsewhere: nothing more
 *     case PRESS_DRAG: moved CLICK_SLOP pixels or more, the button still
 *                      down: a drag (4.2), with no action after it
 *     }
 *
 * press_track waits for the release with evnt_multi (the left button
 * up, the mouse leaving a box round the press, a timer of 20 ms, the
 * messages), calling idle() each turn (a slice of background work) and
 * msg() for each message that comes meanwhile (WM_REDRAW and the rest,
 * handled as the loop would); either may be 0. It sets evnt_multi's first
 * rectangle, which the loop sets again for its own use.
 *
 * press_double times the double click itself, from the desktop's setting
 * (evnt_dclick): 1 when the last press_track that came back PRESS_IN was
 * on the same id, within CLICK_SLOP pixels, and let go no longer ago than
 * the double-click time. It is called before press_track for each press,
 * which then keeps its id for the next one.
 */
#ifndef CLICK_H
#define CLICK_H

#include "gemui.h"

#define PRESS_OUT 0		/* let go elsewhere */
#define PRESS_IN 1		/* let go over r */
#define PRESS_DRAG 2		/* moved while held, the button still down */

#define CLICK_SLOP 4		/* pixels the mouse may move while held, and between a double click's presses */

int press_double(short id, short x, short y);
int press_track(const GRECT *r, short x, short y, void (*idle)(void), void (*msg)(short *m), short *dx,
		short *dy);

/* the double-click time, ms: the desktop's (evnt_dclick) unless set; and
   the last press forgotten (a list rebuilt under it) */
void press_set_dclick(unsigned long ms);
unsigned long press_dclick(void);
void press_forget(void);

#endif
