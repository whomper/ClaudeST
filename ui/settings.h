/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.h - the family's Settings dialog (the guidelines, 6.1; Erez, 9 Nov 2026): the one EMail 2.14
 * has, built once for every app. A black title band, the list of pages at the left (the chosen one a
 * black bar), the page's name and a line over its settings, Cancel and OK at the bottom right. The app
 * gives its pages' names and a function for each that adds that page's objects; this does the rest:
 * the list, the page switching, the ticks, the pop-up buttons, the keys (Return OK; Esc and Undo
 * Cancel, Esc empties a text field first: 3.5), the long fields (5.4), the dialog in a window under
 * MiNT and MagiC (dialog.h), the pop-up opened over its button (5.7).
 *
 * The geometry is the guidelines' and is not the app's to change (characters of the system font):
 *	list SW 13 (a parameter, 13 unless an app has long page names), SX = SW + 2 = 15 where a page starts,
 *	page PW 42 (44 in EWeb: a parameter), W = SX + PW, H = height (16 or 17 rows; the tallest page,
 *	its heading, the button row and an empty row);
 *	row 0 the band " Settings"; the list from row 1 down, a one-pixel line at its right edge (column SW),
 *	its names on rows 2, 4, 6 ..., each a half character (gl_hchar / 2) lower than the step before;
 *	a page: its name on row 2 at column SX, a one-pixel line under it on row 3 (PW - 2 wide), its
 *	settings from row 4, values at column SX + 30 (SET_FX);
 *	Cancel at W - 24, OK at W - 12, 10 wide, on row H - 2.
 *
 * How an app uses it (EHypView's General page, and a number, as a complete example):
 *
 *	#include "settings.h"
 *
 *	static short s_when;			the pop-up's choice, 0 to 3
 *	static short s_log;			a tick: 0 or 1
 *	static char s_port[6];			a number field: 5 characters and the end
 *	static char s_dir[100];			a long field: holds 99, shows 30
 *	static const char *const when[] = { "When first opened", "At start", "After a minute", "After 5 minutes" };
 *
 *	static void page_general(SETTINGS *s, short x, short y, void *user)
 *	{
 *		(void)user;				x = 15 (SX), y = 4: the first row
 *		set_text(s, x, y, "The accessory looks through the library:");
 *		set_popup(s, x + 2, set_y(s, 1), 23, when, 4, &s_when);	pop-up button, 23 characters wide
 *		set_text(s, x, set_y(s, 3), "Folder:");
 *		set_long(s, x, set_y(s, 4), s_dir, 30, 99, 0);		framed, 30 shown, 99 held
 *		set_text(s, x, set_y(s, 6), "Port:");
 *		set_field(s, x + SET_FX, set_y(s, 6), s_port, 5, '9');	framed digits, in the values' column
 *		set_tick(s, x, set_y(s, 7), "Write a log", &s_log);
 *	}
 *	static void page_library(SETTINGS *s, short x, short y, void *user) { ... }
 *
 *	int app_settings(void)
 *	{
 *		static const SET_PAGE pages[] = { { "General", page_general }, { "Library", page_library } };
 *		s_when = opt.when; s_log = opt.log;		the values as they are now
 *		snprintf(s_port, sizeof s_port, "%d", opt.port); strcpy(s_dir, opt.dir);
 *		if (!settings_run("Settings", pages, 2, 42, 17, 0))
 *			return 0;			Cancel, Esc, Undo (nothing is kept: see below)
 *		opt.when = s_when; opt.log = s_log;		OK: take them
 *		opt.port = atoi(s_port); strcpy(opt.dir, s_dir);
 *		return 1;
 *	}
 *
 * The values are the app's own variables, passed by pointer; they are changed as the user works. On
 * Cancel, Esc and Undo every tick, pop-up and field is put back as it was when the dialog opened
 * (a text field's bytes: what the buffer held, up to its size), so an app may pass its live options
 * and read nothing back but the return value. Nothing is stored for the app: after OK (1) it takes
 * what it wants from its variables.
 *
 * A page's builder is called once for every page, in order, as the dialog is built (every page is
 * built before any is shown: a page may have what it likes, hidden objects cost nothing to draw). It
 * adds its objects with the set_* calls below, or with the builder of dlgbuild.h (set_db(s) is the DLGB:
 * db_add for anything else; objects added go on the page being built). The y of a call is packed as an
 * object's: set_y(s, n) is row n of the page (4 + n) with the family's air between rows (n * 3/8 of a
 * character and 2 pixels: 6 pixels a row on a 16-pixel font), or 4 + n, or any other packed value
 * (row | pixels << 8): EHypView's pages put a half character between their groups.
 *
 * Buttons that do something (a file selector, a list of time zones, Languages...) call the app back
 * (set_button, set_value); the dialog is drawn again over what they covered. Whether OK is allowed
 * (a folder that isn't there) is SET_DEF.check.
 *
 * Room: the tree, its texts, ticks, long fields and the pop-ups' and buttons' table are one block from the
 * heap while the dialog is open, 8 KB on a 68000 for the default table of SET_OBJECTS (160) objects (EMail 2.14's
 * six pages need 78), and nothing stays in the BSS but the tick box's bitmaps (184 bytes). SET_DEF.max_objects
 * is a bigger table. What doesn't fit is left out and counted (set_db(s)->lost), never written past; Cancel
 * and OK always have room. The page's area, for a page switch, is rows 2 to H - 3: a page's objects stay
 * inside it, above the buttons' row.
 *
 * The keyboard reaches every control (the guidelines' 5.10, D25; 9 Nov 2026). The focus is the text field that has the
 * cursor, one control (a tick, a radio group, a pop-up, a button the app acts on) or the list of pages; it is shown
 * by a thin ring 2 pixels outside it, drawn only once a key has been used (after a click it is gone again, and a page
 * without a text field looks as it always did). Tab and Shift+Tab visit the page's fields and controls in the order
 * they were added, then the list, and round; Space ticks a tick, opens a pop-up, does a button's work; Left, Right,
 * Up and Down choose in a radio group (greyed choices skipped, no wrapping); Up and Down in the list choose the page,
 * and from control to control elsewhere; in a text field every key is as it was (Up and Down go to the next field,
 * Space is a space), Return is OK, Esc and Undo are Cancel. The dialog opens with the first text field as it always did,
 * else with the list. The ring is the app's frame() and clip_on() (gemui.h), drawn over the tree through dlg_more.
 *
 * A page switch draws the two names that changed and the page's area only (9 Nov 2026), each object by itself
 * (dialog.h: dlg_draw_one, dlg_draw_range: no walk of the AES through the dialog's hundred objects), the page in
 * turns of 20 ms with an event call between, then Cancel and OK where the area's margin reached their frames; a
 * click is found by dlg_find, in C. On an 8 MHz ST (TOS 1.04) the longest stretch of a switch went from 280-345 ms to
 * 45 (a program with -DDLG_CPU) or 55 ms, on a stock Falcon (TOS 4.04) from 110-155 ms to 30-45 ms; the pixels
 * are the old ones'. The page's objects follow each other from heading to its last object, on the root alone.
 *
 * Tested in tests/settings (make test: the built tree's geometry and the keys against a stand-in AES;
 * run.py, SETDEMO.PRG: EMail's Connection page and EHypView's General page in Hatari on TOS 4.04 and
 * TOS 1.04, pixel for pixel; --xaaes under FreeMiNT and XaAES).
 */
#ifndef AERA_SETTINGS_H
#define AERA_SETTINGS_H

#include "gem.h"
#include "dlgbuild.h"

#define SET_SW 13		/* the list's width, characters */
#define SET_PW 42		/* a page's width */
#define SET_FX 30		/* the values' column on a page, from SX */
#define SET_MAXPAGES 12
#define SET_OBJECTS 160		/* objects the dialog may have: EMail's six pages take 72 */

typedef struct SETTINGS SETTINGS;

/* adds the objects of the page at column x (SX), first row y (4) */
typedef void (*SET_BUILD)(SETTINGS *s, short x, short y, void *user);
/* what a button or a value button does when clicked */
typedef void (*SET_ACT)(SETTINGS *s, void *user);

typedef struct {
	const char *name;	/* the page's name: one word of at most 11 characters */
	SET_BUILD build;
} SET_PAGE;

typedef struct {
	const char *title;	/* "Settings": the band says " Settings", the window's title Settings */
	const SET_PAGE *pages;
	short npages;		/* 1 to SET_MAXPAGES */
	short list_w;		/* 0: SET_SW */
	short page_w;		/* 0: SET_PW */
	short height;		/* rows: 16 or 17, EWeb 20 */
	short first;		/* the page shown first (0 is the first) */
	short max_objects;	/* 0: SET_OBJECTS; the dialog's object table (a bigger one: the apps with many pages) */
	void *user;		/* given to every page's builder and every callback */
	/* OK was chosen: 1 lets the dialog close, 0 keeps it open (the app says why, an alert, and may
	   move to the page with set_show). Optional */
	int (*check)(SETTINGS *s, void *user);
} SET_DEF;

/* The dialog: 1 for OK, 0 for Cancel, Esc, Undo and the window's closer (the app's variables as they were),
   0 too when there is no memory (an alert has said so). */
int settings_dialog(const SET_DEF *d);
/* the short way: the list and the page at their default widths (list 13); page_width 0 is 42 */
int settings_run(const char *title, const SET_PAGE *pages, short npages, short page_width, short height,
		 void *user);

/* ---- for a page's builder (and its callbacks) */
/* row n of the page, packed for the calls below (see above) */
short set_y(const SETTINGS *s, short n);
/* explaining words (not copied: they must last) */
short set_text(SETTINGS *s, short x, short y, const char *text);
/* the family's tick box and its words (dlgbuild.h's); *var is the app's, 0 or 1. Returns its number
   (set_tick_group, set_tick_enable), -1 if there is no room */
short set_tick(SETTINGS *s, short x, short y, const char *label, short *var);
/* ticks with the same group (not 0) are a set of choices: one is on */
void set_tick_group(SETTINGS *s, short tick, short group);
void set_tick_enable(SETTINGS *s, short tick, int on);
/* a framed text field showing len characters of buf (len + 1 bytes); kind 'X' or '9'. Returns its object */
short set_field(SETTINGS *s, short x, short y, char *buf, short len, char kind);
/* the same without its own frame, for rows inside a frame the page draws (set_frame): EMail's signature, four rows in one box */
short set_long_bare(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask);
/* a thin frame, black on white, w characters by rows (the object a page's own text rows stand on) */
short set_frame(SETTINGS *s, short x, short y, short w, short rows);
/* a framed field that holds more than it shows (5.4): len shown, cap held (buf is cap + 1 bytes), mask '*' for a password */
short set_long(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask);
/* the pop-up button of 5.7: w characters wide, showing choices[*sel] and a down arrow; a click opens
   the choices over it with the one now chosen ticked, a click on another changes *sel at once.
   At most 16 choices of 31 characters (a longer list is a button with "...": set_value). Returns its object */
short set_popup(SETTINGS *s, short x, short y, short w, const char *const *choices, short n, short *sel);
/* radio buttons (5.6, D24): one of up to four choices as AES buttons 10 characters apart, the way EFTP's Site Manager
   and EPlay's Output have them (G_BUTTON, RBUTTON: the 3D look has them pressed in when chosen, flat the chosen one is
   turned over). labels[0..n-1] (not copied), each w characters wide (8 or 9), the first at x, y; *sel (0 to n - 1) is
   the app's, changed at once by a click or the arrow keys; Cancel puts it back. Four are 40 characters and 12
   pixels wide, from the page's column SX: they fit the 42 of the page (not the value column SX + 30). The engine, not
   the AES, keeps the one chosen (the AES would unchoose the buttons of every other group of the page). Returns the
   group's number (its first button's object), 0 if there is no room (none of the buttons is made) */
short set_radio(SETTINGS *s, short x, short y, short w, const char *const *labels, short n, short *sel);
#define SET_RADIOS 4
/* A choice that does not apply is greyed and cannot be chosen, by a click or by a key (D24; EPlay's FDI and DSP on a machine
   without them). set_radio_enable: choice c of the radio group (set_radio's number) normal (on 1) or greyed (0); the
   same for item `item` of a pop-up button (set_popup's object): its line greyed in the pop-up (popup.h's "~"), not
   chosen. A greyed choice that is the chosen one stays chosen: the app decides what to do with it (it may move *sel) */
void set_radio_enable(SETTINGS *s, short radio, short choice, int on);
void set_popup_enable(SETTINGS *s, short popup, short item, int on);
/* ...or a function that says it: when(s, obj, choice, user) is 1 when choice of the group or pop-up obj applies. It is
   asked as the dialog opens and after every change the user makes (a click, a key, a button's work), so a choice follows
   another setting; the buttons whose answer changed are drawn */
typedef int (*SET_WHEN)(SETTINGS *s, short obj, short choice, void *user);
void set_choice_when(SETTINGS *s, short obj, SET_WHEN when, void *user);
/* a button the app acts on: act(s, user) when clicked, then the dialog is drawn again. w characters wide */
short set_button(SETTINGS *s, short x, short y, short w, const char *label, SET_ACT act, void *user);
/* the same, shown as a pop-up button without the arrow (a choice from a long list: its text ends in "...",
   5.7). label is the app's text of w characters (a space, the choice, padded; it may change in act), drawn
   again after act */
short set_value(SETTINGS *s, short x, short y, short w, char *label, SET_ACT act, void *user);
/* page n (0 is the first) shown; the current page */
void set_show(SETTINGS *s, short n);
/* from check, after set_show: the text cursor goes to this field (the object set_field and set_long return) when the dialog
   goes on, so a refused number is where the user types the new one */
void set_focus(SETTINGS *s, short field);
short set_page_now(const SETTINGS *s);
/* the builder, for anything else (db_add adds to the page being built) */
DLGB *set_db(SETTINGS *s);
OBJECT *set_tree(const SETTINGS *s);
/* the objects, for a test or a callback: page k's tab in the list, OK, Cancel, the page's box */
short set_obj_tab(const SETTINGS *s, short k);
short set_obj_ok(const SETTINGS *s);
short set_obj_cancel(const SETTINGS *s);
short set_obj_pages(const SETTINGS *s);

/* optional hooks (NULL): settings_drawn is called after the dialog or its band was drawn (the Hebrew keyboard's
   badge on the band, EMail's band_badge); settings_uncover(x, y, w, h) to draw the app's own windows in an area:
   the pop-up's, when the screen under it could not be saved (no memory), and the whole desktop after a
   button's work, where a file selector or list of the app's covered the screen (TOS; in a window the AES asks
   for it). Our dialog is drawn again over it (EMail's ui_redraws, win_redraw_area) */
extern void (*settings_drawn)(OBJECT *tree, short band);
extern void (*settings_uncover)(short x, short y, short w, short h);

#endif
