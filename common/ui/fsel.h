/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fsel.h - what to do after the file selector (EConvert's first, 5 Nov
 * 2026).
 *
 * A double click in a file selector can close it on its first click; its
 * second, and the release, then come to the program's own window as
 * clicks of their own. A window that takes a button on a press acted on
 * one: EConvert's Clear emptied the list of files chosen ("it forgets the
 * file and says there are 0 files to convert", Erez). fsel_settle() waits
 * for the button to be let go and takes off the button events the
 * selector left queued; call it as fsel_exinput (or fsel_input) returns.
 */
#ifndef FSEL_H
#define FSEL_H

void fsel_settle(void);

#endif
