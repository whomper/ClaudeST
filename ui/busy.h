/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * busy.h - the busy bee, and Esc or Undo to stop what the network is
 * doing meanwhile (the AERA guidelines, 7.1), ECal's, EContact's and
 * EMail's (EFTP has a Stop button and its own). While the bee shows,
 * busy_idle, the program's pf_idle (it runs while conn waits for the
 * server), takes the keys: Esc and Undo set conn_stop, and the
 * operation ends as it would on a timeout ("stopped"); any other key is
 * kept, and the main loop has it afterwards, as if typed then.
 *
 *   void busy(int on) { busy_bee(on); }
 *   pf_idle = busy_idle;
 *   main loop:
 *       conn_stop = 0;                   (a stop is for one operation)
 *       which = busy_key(&ev) ? MU_KEYBD : evnt_multi_(..., msg, &ev);
 *
 * With busy_message set to the app's message handler, busy_idle also
 * reads messages while the network works: a PEOPLE answer (av.c) is
 * taken at once, everything else goes to the app, which may redraw its
 * windows and must refuse what can't happen now (menus, drops: "not
 * now"). Unset, messages wait in the AES's queue as before.
 */
#ifndef AERA_BUSY_H
#define AERA_BUSY_H

extern void (*busy_message)(const short *msg);

#include "gem.h"

/* the bee (on) or the arrow, and whether keys are taken meanwhile */
void busy_bee(int on);
/* a short pause for the system; while busy, the keys */
void busy_idle(void);
/* a key typed while busy, into ev->kstate and ev->kreturn: 1, or 0 */
int  busy_key(AEVENT *ev);

#endif
