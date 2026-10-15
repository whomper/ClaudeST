/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mbar.h - the menu bar, built at run time from the app's table (the
 * AERA guidelines, 3.1): no resource file, the system font's size,
 * every item "  Text      ^K " with the shortcuts right-aligned. From
 * EHypView's src/gem/menu.c.
 *
 * mbar_build() only fills in an OBJECT tree; it makes no AES calls, so
 * tests/menu checks the tree on the build machine.
 *
 *   static const MBAR_ITEM desk[] = { { "About ECal...", 0, C_ABOUT }, { 0 } };
 *   static const MBAR_ITEM file[] = { { "Open...", "^O", C_OPEN }, { "-" },
 *                                     { "Quit", "^Q", C_QUIT }, { 0 } };
 *   static const MBAR_TITLE bar[] = { { " Desk ", desk }, { " File ", file }, { 0 } };
 *   MBAR m;
 *   if (mbar_build(&m, bar, cw, ch, scr_w, scr_h) == 0)
 *       menu_bar(m.tree, 1);
 *   ... MN_SELECTED: command(mbar_command(&m, msg[4]));
 */
#ifndef AERA_MBAR_H
#define AERA_MBAR_H

#include "gem.h"

typedef struct {
	const char *text;	/* 0 ends a menu; "-" is a separator */
	const char *key;	/* the shortcut shown on the right ("^O", "\x01^F"), or 0 */
	int cmd;		/* the app's command for it; 0 for none */
} MBAR_ITEM;

typedef struct {
	const char *title;	/* " File ", a space either side; 0 ends the bar */
	const MBAR_ITEM *items;
} MBAR_TITLE;

typedef struct {
	OBJECT *tree;
	char *pool;		/* the item texts */
	int *cmd_of;		/* the command of each object */
	short nobjects, ntitles;
} MBAR;

/* The tree for character cells cw x ch on a screen of screen_w x
   screen_h pixels. The first title is the Desk menu: its items (About
   first), then a separator and the six slots the AES fills with the
   accessories' names are added. 0, or -1 when out of memory. */
int mbar_build(MBAR *m, const MBAR_TITLE *titles, short cw, short ch, short screen_w, short screen_h);
void mbar_free(MBAR *m);

/* the object of a command (for menu_ienable, menu_icheck), or -1 */
short mbar_object(const MBAR *m, int cmd);

/* the command of a chosen item (MN_SELECTED's msg[4]), or 0 */
int mbar_command(const MBAR *m, short item);

#endif
