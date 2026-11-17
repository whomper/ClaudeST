/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * field.h - a dialog's text field that holds more than it shows (the AERA
 * guidelines, 5.4: Erez, 22 Oct 2026). The field shows a window of its
 * text and scrolls it with the cursor, so a GitHub token of 93
 * characters, a long folder or an address goes into a field 30 wide.
 * GEM's own fields (TOS 1 to 4, EmuTOS) hold only what they show.
 *
 *   LFIELD f;
 *   lf_init(&f, tree, obj, text, cap, 0)
 *        obj is a G_FTEXT or G_FBOXTEXT whose template is as wide as it
 *        shows; text (cap + 1 bytes) is the app's and holds the whole
 *        text from then on: the field shows f.view. mask '*' shows a
 *        password as stars (lf_mask to show it in clear). A mask also
 *        sets f.secret: a password is never copied or cut out, even while
 *        the eye shows it in clear (4.4; C12). A field that is not a
 *        password but must not be copied sets f.secret itself.
 *   lf_of(tree, obj)        the long field on obj, or 0
 *   lf_forget(tree)         the dialog is gone: its fields are let go
 *
 * In the dialog's key loop, for the field the cursor is in:
 *   lf_enter(f, &idx)       in place of objc_edit's ED_INIT
 *   lf_key(f, key, ch, kstate, &idx)  1 when it was the field's key: the
 *        letters (ch, the character to type: the key's own, or the Hebrew
 *        keyboard's), Backspace, Delete, the cursor keys (Shift: to the
 *        start or the end), ClrHome, Esc (empties it, as GEM's fields), ^C ^X ^V.
 *        Return, Tab, the arrows up and down and Undo are the dialog's (0).
 * dlg_do (dialog.c) does both for the fields given to lf_init.
 *
 * lf_edit() is the editing alone, without the AES (tests/field).
 */
#ifndef AERA_FIELD_H
#define AERA_FIELD_H

#define LF_VIEW 80			/* the widest a field shows */

typedef struct {
	void *tree;			/* OBJECT * */
	short obj;
	char *text;			/* the whole text */
	short cap;			/* the characters it holds */
	short show;			/* the characters the field shows */
	short cur, off;			/* the cursor in text; the first shown */
	char mask;			/* shown as this; 0: as it is */
	char secret;			/* a password: never copied or cut out, shown or not (4.4) */
	char clear;			/* the eye is crossed out: a password shown in clear */
	short eye;			/* the object of its eye button (db_pass), 0 none */
	char view[LF_VIEW + 1];		/* what the field's TEDINFO shows */
} LFIELD;

int lf_edit(LFIELD *f, short key, short ch, short kstate);
void lf_view(LFIELD *f);
void lf_end(LFIELD *f);			/* the cursor to the end */

#ifdef GEM_H
void lf_init(LFIELD *f, OBJECT *tree, short obj, char *text, short cap, char mask);
LFIELD *lf_of(OBJECT *tree, short obj);
void lf_forget(OBJECT *tree);
void lf_enter(LFIELD *f, short *idx);
int lf_key(LFIELD *f, short key, short ch, short kstate, short *idx);
void lf_mask(LFIELD *f, char mask, short *idx, int has_cursor);
/* the eye next to a password (db_pass makes both): lf_eye_image gives the image (a BITBLK, open or
   crossed out as the field shows its text) for a G_IMAGE TOUCHEXIT object, 0 when the four eyes a
   dialog may have are taken; lf_eye_set names the object. dlg_do calls lf_eye_click for every
   TOUCHEXIT it ends on: 1 when obj was an eye (the password shown in clear or as stars, the eye
   drawn again, the cursor where it was) and the dialog goes on */
void *lf_eye_image(LFIELD *f);
void lf_eye_set(LFIELD *f, short obj);
int lf_eye_click(OBJECT *tree, short obj, short *idx, short edit);
#endif

#endif
