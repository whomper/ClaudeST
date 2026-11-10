/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * filepick.h - the family's file dialog: several files (and folders)
 * chosen at once, which TOS's file selector can't give. From EPlay's File >
 * Open... (Add files; Erez, 4 Nov 2026). A dialog of the AES's own objects
 * (3D where the AES has it, in a window on MiNT and MagiC), Selectric's
 * width and ways: the drive in one pop-up at the folder line's start, the
 * last folders, the type, the sort, a name mask, each file's size and
 * date, a selection (a click, Shift-click, a drag, Control+A, All and
 * None), the selected files' count and bytes. Return or a double click
 * opens a folder alone; Up goes up, a double click on it to the root.
 *
 * Built on dialog.c (dlg_open, dlg_evnt with idle, dlg_more, dlg_cut),
 * popup.c and click.c, and src/dirlist.c for the folder; it draws with
 * the app's own drawing (gemui.h: fill, frame, line_col, text_at,
 * clip_on, clip_off). 42 x 24 cells of the system font.
 *
 *     static const DL_TYPE types[] = { { "Music files", "MP3 WAV AIF AIFF AIFC M3U" },
 *                                      { "MP3", "MP3" }, { "All files", "" } };
 *     static const FP_SPEC spec = { "Add files", "Add", "No folder added from yet",
 *                                   "EPLAY", types, 3 };
 *     n = filepick_run(&spec, dir, recent, take_one, feed_music);
 */
#ifndef AERA_FILEPICK_H
#define AERA_FILEPICK_H

#include "dirlist.h"

#define FP_RECENT 10		/* the Folders pop-up's: the last folders taken from */
#define FP_PATH 128

typedef struct {
	const char *title;	/* the band's and the dialog window's: "Add files" */
	const char *ok;		/* the default button (8 cells): "Add" */
	const char *none;	/* the Folders pop-up with none yet (37 at most) */
	const char *log;	/* the rigs' picture times on the console, "EPLAY
				   picture folder 45 ms"; 0: none */
	const DL_TYPE *types;	/* the Type pop-up (names of 13 at most), the first
				   the dialog's own; 0: All files alone */
	int ntypes;		/* DL_TYPES at most */
} FP_SPEC;

/* The app's setting "Use the system file selector" (Erez, 9 Nov 2026; off by default: this dialog is the default):
   when set (a global, not a field of FP_SPEC, which the apps keep const), filepick_run() doesn't open this dialog but
   the AES's own file selector through fsel_exinput, titled with spec->title where the AES is 1.4 or newer. It gives
   ONE file at a time on plain TOS (and on MiNT's and MagiC's, which are the same call): take() is called once, with
   the folder and the name, and 1 is returned, or 0 on Cancel. The folder is left in dir and put first in recent[]
   as the dialog does. The selector shows every file in the folder (the AES has one mask; the app's types are not
   applied), and idle() is not called while it is open. A folder cannot be chosen. The app sets the flag from its
   Settings before each call, or once at its start. */
extern short filepick_system;

/* the dialog, at folder dir (ending in '\'; "" for the current drive's
   root) and left in it; recent[] the last folders, the newest first, kept
   up to date (each FP_PATH long, "" where none). Each file chosen goes to
   take() in the list's order, a folder with a '\' at its end; idle() is
   called every 20 ms or so while it is open (music fed, a transfer on).
   The number taken, 0 when cancelled */
int filepick_run(const FP_SPEC *spec, char *dir, char recent[FP_RECENT][FP_PATH], void (*take)(const char *path),
		 void (*idle)(void));

#endif
