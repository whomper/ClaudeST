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
   centred (form_center) on a multiple of 16 pixels across (its text on a
   byte), the screen under it saved (dlg_under), drawn, dlg_do, the screen
   put back, the exit object deselected; returns it without the
   double-click bit (or a cancel below 0 as it is). On TOS the windows
   under it get no WM_REDRAW (the guidelines' Speed; Erez, 26 Oct 2026),
   except when there was no memory to save it: an app whose window's
   content changed while the box showed draws it again itself. */
short dlg_modal(OBJECT *tree, short start, short cancel);

/* the same (it was dlg_modal with the screen saved, before every dialog
   saved it) */
short dlg_modal_save(OBJECT *tree, short start, short cancel);

/* the screen under a box (x, y, w, h, and 4 pixels round it for the
   outline and shadow) kept in memory, as form_alert does; 0 when there's
   no memory for it (then form_dial(FMD_FINISH) as before). dlg_under_back
   puts it back and lets it go. dlg_open, dlg_modal, popups and the About
   box use them on TOS. */
void *dlg_under(short x, short y, short w, short h);
void dlg_under_back(void *saved);

/* ---- a dialog in a window, where the AES runs several programs at once
   (MiNT with XaAES, N.AES or MyAES; MagiC: the guidelines, 3.5; Erez,
   25 Oct 2026). There the AES draws the frame, the dialog can be moved and
   closed with its closer (as Esc), and other programs go on running. On
   TOS and EmuTOS it is drawn over the screen as before.

     dlg_open(tree, &x, &y, &w, &h);	in place of form_center, wind_update
					(BEG_UPDATE, BEG_MCTRL), form_dial
					(FMD_START) and objc_draw
     ... dlg_do(tree, 0, CANCEL), or the app's own loop with dlg_evnt in
     place of evnt_multi_, and dlg_draw for an object that changed ...
     dlg_close();			in place of form_dial(FMD_FINISH) and
					wind_update's ends

   They nest (a list over the settings): each dlg_open has its dlg_close.
   x, y, w, h are where the dialog is; it may be moved, so draw through
   dlg_draw, or dlg_area for where it is now. The screen is the
   dialog's (BEG_UPDATE) between them, except while dlg_evnt waits. */
short dlg_open(OBJECT *tree, short *x, short *y, short *w, short *h);
/* a dialog the app draws itself (VDI), w by h, centred: draw(x, y, w, h)
   draws that part of it, clipped, when the AES asks */
short dlg_open_area(void (*draw)(short x, short y, short w, short h),
		    short *x, short *y, short *w, short *h);
void dlg_close(void);
/* evnt_multi_ for a dialog's loop: in a window it also takes the AES's
   messages for it (drawn again, moved, brought to the front; the closer
   comes back as Esc), hands WM_REDRAW for the app's other windows to
   dlg_redraw_hook, and gives the key or click only when the dialog is in
   front (a click outside it rings the bell). Elsewhere evnt_multi_. */
short dlg_evnt(short flags, short clicks, short mask, short state,
	       unsigned long timer, short *msg, AEVENT *e);
/* what the app draws itself over the open dialog's tree (lists, a
   sample, with the VDI): draw(x, y, w, h) draws that piece of it again,
   called after the tree when the AES asks for a piece of the window (on
   TOS the app draws it as before) */
void dlg_more(void (*draw)(short x, short y, short w, short h));
/* the app's own drawing in a dialog keeps within what it may draw now:
   x, y, w, h cut to the piece being drawn again; 0 when nothing of it
   may be drawn (the dialog behind another program's window: the AES is
   then asked to have it drawn where it shows). Everything when no
   dialog window is open. Use it before clip_on and for lines. */
int dlg_cut(short *x, short *y, short *w, short *h);
/* an object of the open dialog drawn again, through what shows of it */
void dlg_draw(OBJECT *tree, short obj);
/* where the open dialog is now */
void dlg_area(short *x, short *y, short *w, short *h);
/* 1 where dialogs open in a window (aes_count() != 1, or MagiC);
   dlg_window_on 1 or 0 forces it (tests), -1 (the default) asks the AES */
int dlg_windowed(void);
extern int dlg_window_on;
/* 1 while the innermost open dialog is in a window */
int dlg_in_window(void);
/* the dialog windows' title: the app's name (set it at start) */
extern const char *dlg_title;
/* the next dlg_open's window title, the dialog's own name ("Font",
   "Account"); the app's name (dlg_title) when none is given. The string
   must last while the dialog is open */
void dlg_name(const char *name);
/* the next dlg_open's own title band (the black bar with the dialog's
   name at its top): in a window, where the window's title bar names it,
   it is hidden and what was below it moves up by its height, the box as
   much shorter (all put back at dlg_close); the window is named from its
   text when dlg_name gave none. Over the screen (TOS) nothing changes */
void dlg_band(short obj);
/* the app's redraw for its own windows while a dialog window is open
   (msg is WM_REDRAW's); without it the AES's requests are kept, a
   window's areas as one, and given back to the app as WM_REDRAW when the
   last dialog window closes. So are the app's other messages that come
   while a dialog loop that takes none waits (a PEOPLE question, VA_START,
   AP_TERM): up to eight, given back as they came */
extern void (*dlg_redraw_hook)(const short *msg);

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

/* the pen of the first npens (16 at most: the UI's) whose colour is nearest
   r, g, b (VDI's 0-1000), read with vq_color; text: one dark enough to read
   on white, where there is one. A UI colour in 16 colours or fewer is never
   a fixed pen number: on a TT pens 9-14 are light (the guidelines 5.x) */
short pen_nearest(short h, short npens, short r, short g, short b, int text);

#endif
