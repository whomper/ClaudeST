/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * twin.h - NAME.PRG and NAME.ACC on one machine (the AERA guidelines,
 * 2.3): one of them in use at a time, so that the two never write the
 * same files at once.
 *
 *   twin_init("ECAL|ECALST")   at start-up: the names the editions run
 *                              under, as av_init's
 *   if (twin_handover(cmd))    the PRG, before it opens a window: where
 *       quit now;              the AES lists its programs (MagiC, MiNT),
 *                              its ACC running is handed cmd (VA_START;
 *                              "" for none) and opens instead
 *   if (twin_prg() >= 0)       the ACC: its PRG runs. The ACC leaves the
 *       twin_pass(s);          files alone while it does (no background
 *                              work) and hands it AC_OPEN ("") and
 *                              VA_START: the PRG's window comes to the
 *                              front, or the PRG opens the files
 *   twin_reset()               the ACC, at AC_CLOSE: the program in front
 *                              has changed
 *
 * Under plain TOS the program in front is ap_id 0 and the AES can't list
 * the accessories, so the PRG can't see its ACC: it runs, and the ACC
 * stands aside. To be sure the name the AES gives ap_id 0 isn't a
 * leftover, the ACC asks it (TWIN_ASK); our programs answer in
 * av_message() (TWIN_HERE). A program still starting answers late:
 * av_message() hands that TWIN_HERE to twin_answered(), and until it
 * comes (or 30 s) the program in front is taken for ours. Opened from
 * the Desk menu meanwhile, the ACC asks again and waits a second.
 */
#ifndef AERA_TWIN_H
#define AERA_TWIN_H

#define TWIN_ASK	0x4145		/* "are you NAME.PRG?" (msg[1]: the ACC) */
#define TWIN_HERE	0x4146		/* "yes" */

void twin_init(const char *names);
int twin_handover(const char *cmd);	/* 1: the ACC has it, the PRG quits */
short twin_prg(void);			/* -1: not running */
int twin_pass(const char *s);		/* 1 when the PRG has it */
void twin_reset(void);
void twin_answered(short id);		/* a TWIN_HERE from id (av_message) */

#endif
