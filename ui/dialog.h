/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialog.h - the AERA programs' dialog loop (the guidelines, 3.5): form_do
 * written out with the AES's own calls, so that Esc and Undo close the
 * dialog as Cancel does. TOS's and EmuTOS's form_do hand Esc to the text
 * field, which it clears.
 *
 *   r = dlg_modal(tree, 0, CANCEL);	centred, drawn, run and put away
 *   if (r == CANCEL) ...		Esc and Undo come here too
 *
 * Or, for a dialog the app draws itself, dlg_do(tree, 0, CANCEL) in place
 * of form_do(tree, 0).
 */
#ifndef AERA_DIALOG_H
#define AERA_DIALOG_H

#include "gem.h"

/* form_do: the cursor starts in field start (0: the first editable one);
   Return picks the DEFAULT object, Tab and the arrows go from field to
   field, clicks go through form_button; the exit object comes back, with
   the high bit set for a double click on a TOUCHEXIT one, and it is left
   SELECTED as form_do leaves it. Esc and Undo end it at once with cancel
   (drawn deselected), the field's text as it was. A dialog with no button
   that does nothing (EFTP's Skip|Resume|Replace while a transfer runs,
   where Esc stops the transfer) passes -1: Esc and Undo then give -1,
   told apart from every button, and no object is drawn or changed. */
short dlg_do(OBJECT *tree, short start, short cancel);

/* the whole of it: the screen and mouse kept (wind_update), the tree
   centred (form_center), the space taken (form_dial), drawn, dlg_do,
   the space given back (the windows under it get WM_REDRAW), the exit
   object deselected; returns it without the double-click bit (or a
   cancel below 0 as it is) */
short dlg_modal(OBJECT *tree, short start, short cancel);

/* as dlg_modal, but the screen under the box is saved and put back, as
   form_alert does, rather than redrawn through WM_REDRAW, when there is
   memory for it: for a small box that may come while the app isn't
   reading its messages (alert() uses it) */
short dlg_modal_save(OBJECT *tree, short start, short cancel);

/* 1 for Esc and Undo, as evnt_multi gives them (scan code in the high
   byte) */
int dlg_esc(short key);

/* the AES's own 3D look (the guidelines, 5.x; Erez, 24 Oct 2026), where it
   has one (AES 3.4 and newer: TOS 4, MagiC, N.AES, XaAES; asked through
   appl_getinfo(13) where it can be): the box and its text on the 3D
   background, exit buttons as activators, radio buttons and toggles as
   indicators, editable fields left white. Call it once a tree is built,
   before it is drawn; dlg_modal calls it itself. Nothing changes on an
   AES without 3D (TOS 1 to 3; EmuTOS in fewer than 16 colours). */
void dlg_3d(OBJECT *tree);
/* 1 where the AES draws 3D objects: the app's Settings tick is greyed
   where it doesn't */
int dlg_3d_ok(void);
/* the app's setting, DIALOGS3D= in its INF (1, the default: 3D where the
   AES has it; 0: flat everywhere), taken by every dlg_3d after it is set */
extern int dlg_3d_on;

#endif
