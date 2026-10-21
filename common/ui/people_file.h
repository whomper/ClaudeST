/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people_file.h - EContact's CONTACTS.TXT, read when EContact isn't
 * loaded (the guidelines, 4.1: an option the user turns on in EMail's or
 * ECal's Settings, off by default). EContact writes the file in its
 * folder (1.40's format): Atari text, CRLF; "; " lines are notes; a line
 * per address, "P<TAB>name<TAB>email<TAB>label", and one per group with
 * addresses, "G<TAB>group<TAB>Name <email>, Name <email>".
 *
 *   people_file("C:\\ECONTACT", message)   the option on, EContact's folder
 *   people_file(0, 0)                      the option off
 *
 * With it on, people_ask.c answers names from the file when neither
 * edition of EContact is running, and people_pick() opens the people
 * dialog on the file: a Find line and the list of people and groups; a
 * click marks one, a double click takes that one, Return the ones marked
 * (a group gives its people). message(m) is
 * the app's handler for the messages that come while the dialog is open
 * (its windows' redraws); m == 0 now and then with none.
 *
 * The names: the file is read 4 KB a round of questions, opened and
 * closed each time (2.2: a step stays short on an ST), and not again
 * while it is unchanged; nothing of it is kept but the names asked for.
 * EContact writes it anew and renames it into place, so for a moment
 * there is none: once read, a missing file counts as unchanged. The
 * dialog loads it whole while it is open (the file's size, and 9 bytes a
 * line: 600 contacts with two addresses, about 75 KB).
 */
#ifndef PEOPLE_FILE_H
#define PEOPLE_FILE_H

void people_file(const char *folder, void (*message)(short *m));

/* a step of the file's P lines, each to line(): 2 more to come, 1 read
   to its end, 0 unchanged since the last whole read (not read again
   unless force; a missing file once read counts as unchanged), -1 no file */
int people_file_scan(void (*line)(const char *addr, const char *name, void *ud), void *ud, int force);

/* 1 while a reading in steps is under way */
int people_file_busy(void);

#endif
