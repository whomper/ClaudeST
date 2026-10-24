/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * av.h - the AV protocol (the AERA guidelines, 4.1): how the AERA
 * programs, the desktop and other programs hand each other work.
 *
 *   av_init("ECONTACT");   at start-up (an accessory again when it
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

enum { AV_NONE, AV_START, AV_DONE, AV_PEOPLE };

/* our names as the AES may know us, by the file we were started as:
   "EWEB|EWEBST" (the one appl_find gives our id is used) */
void av_init(const char *names);
void av_exit(void);
void av_window(short h, int open);
int av_message(const short *msg, const char **s);	/* AV_NONE: not the AV protocol's */
const char *av_arg(const char *p, char *out, long n);	/* 0 when none is left */

/* the command line the program was started with (the desktop's way to
   open a file with it), as a VA_START string: "" for none */
const char *av_command(void);

/* text for the program called names, with VA_START: the first running of
   its editions, "EWEB|EWEBST" ("EWEB" is padded as appl_find wants
   it). 1 when sent, 0 when none is running */
int av_send(const char *names, const char *text);

/* PEOPLE (0x4E43, guideline 4.1): the family's one message besides the AV
   protocol's, for asking EContact about people (people.c builds and reads
   the strings). Laid out as VA_START and answered with AV_STARTED;
   av_message() hands a PEOPLE string over as AV_PEOPLE (the sender is
   msg[1]). One goes at a time: av_people() gives 0 while the last isn't
   answered, -1 for no such program; after 3 s without AV_STARTED the last
   counts as lost (av_people_lost() counts them) and the next may go.
   Sending is in av_people.c, linked only by the apps that ask (people_ask.c)
   or answer (EContact), so the others don't carry its 1K buffer. */
#define AV_PEOPLE_MSG	0x4E43
int av_people(short id, const char *text);
int av_people_ready(void);
long av_people_lost(void);
/* between av.c and av_people.c: a message out, and our PEOPLE's
   AV_STARTED back (1 when p was ours) */
void av_post(short id, short what, short w3, const char *p);
extern int (*av_people_started)(const char *p);
/* a TWIN_HERE that came after twin.c stopped waiting (twin_init sets it) */
extern void (*av_twin_here)(short id);
/* in a loop of the shared code's own (busy.c): a PEOPLE goes to
   av_people_hook (people_init sets it) and our PEOPLE's AV_STARTED is
   taken; 1 when the message was one of those, 0 for the app's */
extern void (*av_people_hook)(const char *s, short from);
int av_modal_message(const short *msg);
/* the name the AES knows us by, without its padding ("ECAL", "EWEBST") */
const char *av_name(void);

/* memory other programs may read: global under MiNT and MagiC (Mxalloc
   mode 0x2B), else local (static: an accessory has nothing else) */
char *av_shared(char *local, long size);

#endif
