/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * popup.h - right-click menus and choice pop-ups (popup.c, the guidelines' 3.3):
 * the chosen item, or -1.
 *
 * A label: "-" a line, "~Text" greyed, "\x08Text" ticked (the Atari check
 * mark), anything else a plain item. The box is as wide as the longest label
 * plus 4 cells; a key typed into a label ("Open   ^O") is the app's own
 * spaces, so the labels of one menu have equal length.
 *
 * NEW CONTRACT, popup_m() (D15, 9 Nov 2026): a label is just its words, and
 * the box adds the margin: every text starts 2 cells in and the tick is in
 * the margin (half a cell in), ticked or not, so the rows line up:
 *
 *   static const char *const lab[] = { "Open", "-", "~Rename", "\x08Hidden" };
 *   switch (popup_m(mx, my, lab, 4)) { ... }
 *
 *   |  Open     |     text at 2 cells, nothing in the margin
 *   |  ---------|
 *   |  Rename   |     (grey)
 *   |v Hidden   |     the tick (v) half a cell in, the text where all text is
 *
 * For a choice pop-up: "\x08" in front of the current choice only (in C,
 * "\x08" "At start": a word starting with 0-9, A-F or a-f would be read
 * into the escape). No
 * leading spaces, no trailing space after the words. A greyed item is not
 * ticked. popup_m does not align keys: an app that shows a key in the label
 * ("Open   ^O") pads its labels to one length itself, so spaces after an
 * item without a key are the app's column, not a trailing space.
 *
 * OLD CONTRACT, popup() (until an app drops its padding): the label carries
 * the margin itself, two spaces ("  Open", "\x08 Hidden"), and the text is 4
 * cells in. Do not write "\x08Hidden" for popup(): the tick takes the place
 * of the first character, so that text sits a cell to the right of its
 * unticked neighbours (the audit's MG14); it is the look of popup_m(), not
 * of popup().
 *
 * HOW AN APP MOVES OVER: popup() stays exactly as it was. When the app's
 * next release syncs common/, it changes each popup()/popup_alt() call to
 * popup_m()/popup_m_alt() in the same commit that drops the leading
 * spaces and the "\x08 " / "  " pairs from its labels (an app that never
 * padded has only the call to change). Text moves from 4 cells to 2 for
 * the apps that padded, and stays at 2 for those that did not (a ticked
 * label no longer moves right). When every app has moved, popup() will be
 * made the new contract and popup_m() kept as its alias for a release.
 */
#ifndef AERA_POPUP_H
#define AERA_POPUP_H

#include "gem.h"

short popup(short x, short y, const char *const *labels, short n);	/* old contract */
short popup_m(short x, short y, const char *const *labels, short n);	/* new contract */
/* the same; with no room below y, it ends above alt_y (the top of the
   row it is for) instead of above y (Claude ST's list menus) */
short popup_alt(short x, short y, short alt_y, const char *const *labels, short n);
short popup_m_alt(short x, short y, short alt_y, const char *const *labels, short n);
/* a choice pop-up OVER its button (D27, 5.7): the row `sel` (the ticked one) lies over the button whose
   top-left is (x, y), as the AES's own pop-ups and Settings do; popup_m's contract (labels are words). The
   box is kept on the desktop, not flipped above. Settings and the Add files dialog use it; so should every
   app's choice pop-up (EFTP, EContact, ECal, EHypView, EMail: C8) instead of working the place out */
short popup_over(short x, short y, const char *const *labels, short n, short sel);
extern GRECT popup_area;	/* where the last popup was: what the app repaints */
extern short popup_put_back;	/* 1: the screen under it was put back as it closed
				   (dlg_under): nothing to repaint; 0: the AES was asked to */

#endif
