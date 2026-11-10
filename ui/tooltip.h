/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tooltip.h - the family's tooltip (the guidelines' 3.4, MENUS.md section 4; D18, 9 Nov 2026): what
 * EMail, EFTP, EUpdate and ECal each carried as tip_show, and EHypView's BubbleGEM tip, made once.
 *
 * The rules, kept here so an app cannot get them wrong:
 *	- the tip comes after the pointer has rested TIP_DELAY (700 ms, tos_ms) on one place; it never comes
 *	  after a key or a click until the pointer has moved;
 *	- it is a short sentence with the key in brackets, "Stop (Esc)", one line, or two with '|' between
 *	  them (the second dim), 60 characters a line at most;
 *	- BubbleGEM (BGEM.NG under XaAES) first, when it runs: it shows the text at the pointer and takes it
 *	  away itself ('|' goes to it as a line break); else a white box with a 1-pixel frame, in the system
 *	  font, one cell right of the pointer and a line and 2 pixels below it, to the left or above at the
 *	  desktop's edge, its text on a byte (a multiple of 8: the 68000's fast text), drawn once, all in one
 *	  clip that is the box itself (nothing outside it is touched);
 *	- any event takes it away: a move of the pointer, a key, a click, a message. The box is taken away by
 *	  the app drawing the area under it again (the callback of tip_init, or the AES's redraw).
 *
 * What the app gives: tip_init once after appl_init (an accessory too: BubbleGEM reads the text from
 * global memory, taken then), and its text function: for the pointer at (x, y) the sentence, or 0 for
 * "no tip here" (not over a button, a name that is not cut). The function does the app's own checks
 * (the window on top, the button under (x, y), a name too long for its column) and may use any time
 * it needs, it runs once per rest.
 *
 * The app's event loop (EFTP's, as a complete example; evnt_set_m1 wakes the call when the pointer
 * leaves its 1 x 1 rectangle, so a still pointer costs nothing):
 *
 *	#include "tooltip.h"
 *
 *	static int win_tip(short x, short y, char *out, int size)
 *	{
 *		int b = toolbar_hit(x, y);			which button, -1 none
 *		if (b < 0)
 *			return 0;
 *		snprintf(out, size, "%s", button_tip[b]);	"Stop (Esc)", or "Stop|Esc"
 *		return 1;
 *	}
 *
 *	static void redraw_area(const GRECT *r) { win_redraw_area(r); }	the app's own repaint of r
 *
 *	main:
 *		appl_init();
 *		tip_init(redraw_area);
 *	loop:
 *		short hx = -1, hy = -1, k;			the pointer, from the last move
 *		while (!quit) {
 *			evnt_set_m1(1, hx, hy, 1, 1);
 *			which = evnt_multi_(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER | MU_M1, 0x102, 3, 0,
 *					    tip_wait(1000), msg, &ev);		1000 ms, or sooner when a tip is due
 *			if ((which & MU_M1) && !iconified)
 *				graf_mkstate(&hx, &hy, &k, &k);
 *			tip_tick(which, iconified ? -1 : hx, hy, win_tip);	before the app handles the event
 *			...the app handles the event as it always did...
 *		}
 *
 * An app that polls the pointer instead (a timer every 100 ms, graf_mkstate each time) passes the
 * position it just read; tip_tick sees the move itself. A modal loop (a dialog, a popup) calls
 * tip_hide() before it starts.
 */
#ifndef AERA_TOOLTIP_H
#define AERA_TOOLTIP_H

#include "gem.h"

#define TIP_DELAY 700		/* ms the pointer rests before the tip (D18) */
#define TIP_LINE  60		/* characters a line, at most */

/* the app's text for the pointer at (x, y): 1 with the sentence in out (size bytes; '|' splits two
   lines), 0 for no tip there */
typedef int (*tip_text_fn)(short x, short y, char *out, int size);

/* once, after appl_init. repaint(r): the app draws r again (the window under the box was changed);
   0: the AES is asked (form_dial) and the app gets WM_REDRAW */
void tip_init(void (*repaint)(const GRECT *r));

/* the time to give evnt_multi_'s timer: other (ms, the app's own), or less when a tip is due sooner */
unsigned long tip_wait(unsigned long other);

/* after each evnt_multi_, with its result `which` (MU_* bits) and the pointer at (x, y) now (x < 0:
   no pointer to show a tip for: iconified, a modal job). A key, a click, a message or a move takes the
   tip away and starts the wait again; a timer alone that finds the pointer rested TIP_DELAY on a
   place that has a tip shows it, once */
void tip_tick(short which, short x, short y, tip_text_fn text);

/* takes the tip away now (the app is about to open a dialog, hide its window...) */
void tip_hide(void);

/* 1 with our box's rectangle in *r (its frame included; r may be 0) while the tip is up, 0 when there is
   none, or it is BubbleGEM's (which keeps itself clear). For an app that draws by itself while a tip is up
   (a clock, a progress bar, a position bar): it leaves that rectangle out of its drawing, else its
   drawing runs through the box and nothing takes it away until the pointer moves (D18, EPlay's finding) */
int tip_area(GRECT *r);

/* optional: 1 for a line that reads right to left (Hebrew): drawn right-aligned (TX_RIGHT, where the
   app's ui.h has it) */
extern int (*tip_rtl)(const char *s, short n);

#endif
