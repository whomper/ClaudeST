/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * av.h - the AV protocol (the AERA guidelines, 4.1): how the AERA
 * programs, the desktop and other programs hand each other work.
 *
 *   av_init("ECONTACT|ECONTST");   at start-up (an accessory again when it
 *                                  opens): tells the desktop's AV server
 *                                  (AVSERVER, else Thing, jinnee or Gemini)
 *                                  that we take VA_START (AV_PROTOKOLL),
 *                                  under the name the AES knows us by
 *   switch (av_message(msg, &s))   each message from the AES
 *   case AV_START:                 VA_START, answered with AV_STARTED: s is
 *                                  "" (open or top the window), file names,
 *                                  or a command such as "mailto:..."
 *   av_exit();                     when a program quits (AV_EXIT)
 *   av_window(h, 1);               a window opened (AV_ACCWINDOPEN; 0: closed,
 *                                  AV_ACCWINDCLOSED): the desktop then drops
 *                                  files on it with VA_DRAGACCWIND (dd.c)
 *
 * av_arg() takes the next file name off a VA_START string, without its
 * quotes ('...' around a name with spaces, '' for a quote in it).
 * av_send() hands another program a string with VA_START, in memory it
 * may read. av_message() also answers our accessory's question to the
 * program (twin.h's TWIN_ASK).
 */
#ifndef AERA_AV_H
#define AERA_AV_H

enum { AV_NONE, AV_START, AV_DONE };

/* our names as the AES may know us, by the file we were started as:
   "ECONTACT|ECONTST" (the one appl_find gives our id is used) */
void av_init(const char *names);
void av_exit(void);
void av_window(short h, int open);
int av_message(const short *msg, const char **s);	/* AV_NONE: not the AV protocol's */
const char *av_arg(const char *p, char *out, long n);	/* 0 when none is left */

/* the command line the program was started with (the desktop's way to
   open a file with it), as a VA_START string: "" for none */
const char *av_command(void);

/* text for the program called name, with VA_START. 1 when sent, 0 when it
   isn't running */
int av_send(const char *name, const char *text);

/* memory other programs may read: global under MiNT and MagiC (Mxalloc
   mode 0x2B), else local (static: an accessory has nothing else) */
char *av_shared(char *local, long size);

#endif
