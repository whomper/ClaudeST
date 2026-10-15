/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dd.h - things dropped on our windows (the AERA guidelines, 4.2): files
 * and text by drag and drop (AP_DRAGDROP, under MiNT and MagiC, through
 * the pipe U:\PIPE\DRAGDROP.xx), and files from the desktop on an
 * accessory's window (VA_DRAGACCWIND, under any TOS: av_window() tells
 * the desktop about the window).
 *
 *   DROP d;
 *   switch (dd_message(msg, DD_WANT_FILES | DD_WANT_TEXT, &d)) {
 *   case DD_FILES:   d.data: the file names, quoted as VA_START's
 *                    (av_arg() takes them one by one)
 *   case DD_TEXT:    d.data: the text, d.len bytes, lines ending '\n'
 *   }
 *   dd_done(&d);     the data let go
 *
 * d.win is the window it was dropped on (-1: none in particular), d.x,
 * d.y where, d.kstate the shift keys then. What a program doesn't want
 * it refuses, and the sender is told so.
 */
#ifndef AERA_DD_H
#define AERA_DD_H

enum { DD_NONE, DD_FILES, DD_TEXT, DD_REFUSED };
#define DD_WANT_FILES 1
#define DD_WANT_TEXT  2
#define DD_MAX 32000L			/* longer is refused */

typedef struct {
	short win, x, y, kstate;
	char *data;
	long len;
} DROP;

int dd_message(const short *msg, int want, DROP *d);	/* DD_NONE: not a drop */
void dd_done(DROP *d);

#endif
