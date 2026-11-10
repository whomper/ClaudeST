/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dlgbuild.h - the object-tree builder the AERA programs' dialogs are made
 * with (the guidelines, 5.6, 5.7, 6.1): EMail's d_begin, d_add, d_text,
 * d_field, d_long, d_framed, d_tick, d_button, d_band and d_end, lifted out
 * of the five dialogs.c files that each had a copy (Erez, 9 Nov 2026). No
 * .RSC file: a dialog is built in code, then shown by dlg_modal or
 * dlg_open and dlg_do (dialog.h), which take Esc, Undo, Return and the long
 * fields' keys.
 *
 * Places and sizes are in characters of the AES's system font, as an object
 * has them before rsrc_obfix: the low byte characters, the high byte pixels
 * (x = 3 | (4 << 8) is 3 characters and 4 pixels in), so a row can be a
 * few pixels lower than the last (the rows of a Settings page: 6 pixels
 * apart on a 16-pixel font). The tree is fixed (rsrc_obfix_all, no AES call
 * an object) and given its 3D look (dlg_3d) by db_end.
 *
 *   DLGB b;
 *   static char name[31], port[6];
 *   static short on;
 *   short x, y, w, h, f, ok, cancel, r, k;
 *   if (!db_alloc(&b, 40, 20, 4, 4, 0))		objects, texts, ticks, long fields, extra bytes
 *	return 0;				no memory
 *   db_begin(&b, 40, 10);			a dialog 40 by 10 characters
 *   db_text(&b, 2, 1, "Server");		a G_STRING; db_add() for any other object
 *   db_text(&b, 2, 3, "Name:");
 *   f = db_framed(&b, db_long(&b, 10, 3, name, 20, 30, 0));	shows 20 characters, holds 30
 *   db_text(&b, 2, 4, "Port:");
 *   db_field(&b, 10, 4, port, 5, '9');				a framed number field
 *   db_tick(&b, 2, 6, "Use TLS", &on);				the family's tick box and its words
 *   cancel = db_button(&b, 16, 8, 10, "Cancel", EXIT);
 *   ok = db_button(&b, 28, 8, 10, "OK", EXIT | DEFAULT);
 *   db_end(&b);					fixed to the screen's font, in 3D where the AES has it
 *   dlg_name("Server");				the window's name where the AES has windows
 *   dlg_open(b.tree, &x, &y, &w, &h);		dialog.h: over the screen on TOS, a window on MiNT, MagiC
 *   for (;;) {
 *	r = dlg_do(b.tree, f, cancel) & 0x7fff;	Esc, Undo: cancel; Return: OK; the long field's keys
 *	if ((k = db_tick_hit(&b, r)) >= 0) {	a click on the tick's box or its words
 *		db_tick_flip(&b, k);		turned over and drawn again
 *		continue;
 *	}
 *	break;					OK or Cancel
 *   }
 *   dlg_close();
 *   db_free(&b);
 *   if (r == ok) ...				name, port and on are the app's
 *
 * Settings dialogs are not built like this by hand: settings.h does all of it
 * (the list of pages, the buttons, the loop). A dialog that stays open while
 * its own ticks and buttons act uses db_tick_hit and db_tick_flip as above.
 *
 * Room: db_alloc takes the objects (the tree's root, every object a call
 * adds, one spare), texts (one a text field, button with a text of its
 * own, band), ticks (a tick box is two objects) and long fields from the
 * heap in one block, db_free gives it back. What doesn't fit is left out,
 * never written past the end: `lost` counts it, the dialog is still whole
 * (the guidelines' 6.5: a bigger dialog than the array is a mistake the
 * tests see, not a crash).
 */
#ifndef AERA_DLGBUILD_H
#define AERA_DLGBUILD_H

#include "gem.h"
#include "field.h"

/* one tick box: its image is the object `box`, its words the next one */
typedef struct {
	short box;
	short *var;		/* the app's variable, 0 or 1 */
	short was;		/* its value when the dialog opened (settings.c puts it back on Cancel) */
	short group;		/* 0, or the group of ticks of which one is chosen (a radio set) */
} DB_TICK;

typedef struct {
	OBJECT *tree;
	short omax, nobj, last;		/* objects the array holds, made, the last one linked */
	TEDINFO *ted;
	short tmax, nted;
	DB_TICK *tick;
	short kmax, nticks;
	LFIELD *lf;
	short lmax, nlf;
	short keep;			/* objects kept back from db_add for the last ones (settings.c: Cancel and OK) */
	short band;			/* db_band's object, 0 none */
	short lost;			/* what didn't fit: objects, texts, ticks, long fields */
	short wc, hc;			/* the AES's character cell, read by db_begin */
	void *mem;			/* db_alloc's block */
	void *extra;			/* its last part: db_alloc's `extra` bytes, zeroed */
	void (*draw)(OBJECT *tree, short obj);	/* how a tick box is drawn again (NULL: dlg_draw; settings.c's, alone over the ground) */
} DLGB;

/* The arrays are the caller's (a static dialog)... */
void db_init(DLGB *b, OBJECT *tree, short omax, TEDINFO *ted, short tmax, DB_TICK *tick, short kmax,
	     LFIELD *lf, short lmax);
/* ...or from the heap, in one block, for as long as the dialog is up: 1, or 0 when there is no memory.
   `extra` more bytes (zeroed) are at b->extra, for what the caller keeps with the dialog */
int db_alloc(DLGB *b, short omax, short tmax, short kmax, short lmax, long extra);
/* the long fields let go (lf_forget), the heap block back */
void db_free(DLGB *b);

/* the root: an OUTLINED G_BOX w by h characters, the dialog's frame (5.6) */
void db_begin(DLGB *b, short w, short h);
/* one more object in the root, last in the list; its index. A full tree gives the spare one, which
   isn't in the tree: the caller may write to it, nothing shows */
short db_add(DLGB *b, short type, short flags, short state, long spec, short x, short y, short w, short h);
/* the next n objects are not for db_add: the last ones of the dialog (its buttons) always find room; 0 gives it back.
   db_spare(b, i): 1 when object i is the spare, which is not in the tree */
void db_keep(DLGB *b, short n);
int db_spare(const DLGB *b, short i);
/* a G_STRING (the text isn't copied: it must last while the dialog is up) */
short db_text(DLGB *b, short x, short y, const char *s);
/* a TEDINFO for a G_BOXTEXT or G_TEXT: text (not copied), justification (0 left), colour word
   (0x1180 black on white, 0x1071 white on black). The text's length is as it is now */
TEDINFO *db_ted(DLGB *b, const char *text, short just, short color);
/* an editable field showing len characters (80 at most) of buf (len + 1 bytes, the text at most
   len); kind 'X' any character, '9' digits. Unframed: db_framed puts the frame on */
short db_edit(DLGB *b, short x, short y, char *buf, short len, char kind);
/* object i, a field, in the family's thin frame (5.7: every field you type into has one) */
short db_framed(DLGB *b, short i);
/* db_framed(db_edit()) */
short db_field(DLGB *b, short x, short y, char *buf, short len, char kind);
/* a field that holds more than it shows (field.c, 5.4): shows len characters of text (cap + 1
   bytes, the app's), mask '*' shows stars. Unframed: db_framed puts the frame on. dlg_do and
   settings.c take its keys */
short db_long(DLGB *b, short x, short y, char *text, short len, short cap, char mask);
/* a G_BUTTON, SELECTABLE | flags (EXIT, DEFAULT, TOUCHEXIT) */
short db_button(DLGB *b, short x, short y, short w, const char *label, short flags);
/* the title band: the first row, w characters wide, white words on black (G_BOXTEXT 0x1071), the
   text as given (" Settings"). Remembered in b->band, for dlg_band (3.5) */
short db_band(DLGB *b, short w, const char *title);
/* the family's tick box (the guidelines' 5.6: a box crossed when on; the 16 by 16 pixel drawing of
   EMail's), then its words 3 characters on; clicking either one turns it over. *var is the app's
   (0 or 1). Returns the tick's number, or -1 when the table is full */
short db_tick(DLGB *b, short x, short y, const char *label, short *var);
/* the tick that object obj is the box or the words of, else -1 */
short db_tick_hit(const DLGB *b, short obj);
/* tick k turned over and its box drawn again (dlg_draw: where the dialog is now). A tick in a group
   chooses itself and clears the others of its group (drawn too); a chosen one stays */
void db_tick_flip(DLGB *b, short k);
/* tick k in a radio set: those with the same group (not 0) are choices of one */
void db_tick_group(DLGB *b, short k, short group);
/* a tick greyed (on 0) or normal; a greyed one doesn't turn over */
void db_tick_enable(DLGB *b, short k, int on);
/* the dialog finished: the list ended, the objects fixed to the screen's font (rsrc_obfix_all)
   and the AES's 3D look where it has one (dlg_3d) */
void db_end(DLGB *b);

/* the tick box's drawing: the bitmaps for the font's height, 0 before the first db_tick */
extern BITBLK db_tick_on, db_tick_off;

#endif
