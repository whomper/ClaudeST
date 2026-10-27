/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * notify.h - notifications through BGEM.NG (the AERA guidelines, 4.5; a
 * tester, Erez 27 Oct 2026). BGEM.NG (Jo Even Skarstein,
 * https://atari.joska.no/bgem.ng/) is BubbleGEM made again for MiNT with
 * XaAES or N.AES: besides help bubbles it shows notifications, small
 * windows stacked at the desktop's edge that close by themselves, and
 * tells the program which of their buttons was clicked.
 *
 * Where BGEM.NG runs, an app says "new mail", "a reminder" or "updates
 * are ready" with notify_send; elsewhere (TOS, MagiC, an old BubbleGEM)
 * notify_send gives 0 and the app does what it always did.
 *
 *   notify_init("EMAIL   ")  at start-up, after appl_init (an accessory
 *        must: the text goes in global memory, taken then). The name is
 *        the app's AES name, 8 characters, which BGEM.NG shows as the
 *        sender.
 *   notify_message(msg)      in the app's message loop, for every
 *        message: 1 when it was BGEM.NG's (its answer, a notification
 *        shown, a button clicked), which the app then leaves alone.
 *   notify_send(title, text, buttons, id)
 *        title: up to 32 characters, or 0; text: what it says ('|'
 *        breaks a line); buttons: "Open|Later", or 0 for none; id: the
 *        app's number for it, given back with the button. 1 when it went
 *        to BGEM.NG (or waits for its answer to our question: then it
 *        goes when that comes); 0 where there is no BGEM.NG.
 *   notify_clicked           the app's function for a button:
 *        notify_clicked(id, button), button 1 the leftmost.
 *   notify_ok()              1 once BGEM.NG said it shows notifications.
 */
#ifndef AERA_NOTIFY_H
#define AERA_NOTIFY_H

#define BUBBLEGEM_SHOW 0xBABB
#define BUBBLEGEM_ACK  0xBABC
#define BGEMNG_INQUIRE 0xABAB

void notify_init(const char *aes_name);
int notify_message(const short *msg);
int notify_send(const char *title, const char *text, const char *buttons, short id);
int notify_ok(void);
extern void (*notify_clicked)(short id, short button);

/* the text BGEM.NG is given: the header, then "[0][text][buttons]", with
   '[' and ']' in the app's text made '(' and ')'. n bytes at most (the
   host tests) */
void notify_format(char *out, long n, const char *from, const char *title, const char *text,
		   const char *buttons);

#endif
