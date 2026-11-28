/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* bell.h - a soft bell for the family's apps (bell.c): EMail's new mail
   first (decided 27 Nov 2026). Linked with --gc-sections, an app that never
   calls it carries none of it. */
#ifndef BELL_H
#define BELL_H

/* once at the start; in an accessory before its first event (2.2): it may
   take the sample's ST-RAM copy then */
void bell_init(void);
/* rings now and returns at once (on = the app's setting: 0 logs "BELL
   silent: off"). Silent, with the reason logged, when another program
   holds the sound or the machine has none */
void bell_ring(short on);
/* each event turn (a timer's will do): once the bell has ended, the sound
   settings it changed go back and the sound is unlocked. Nothing to do
   costs a compare */
void bell_poll(void);
/* at a program's end: a bell still ringing stopped and put back, the
   ST-RAM copy given back (never in an accessory) */
void bell_free(void);

#endif
