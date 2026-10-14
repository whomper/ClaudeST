/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * crash.h - the crash catcher (EMail's, shared by the AERA programs). A
 * bus error, address error, illegal instruction and the like in the
 * program (the 2 to 8 bombs) no longer leave the bombs and a shaky
 * machine: it writes what happened to CRASH.LOG, says so and quits. A
 * crash in the system or a driver still gives the bombs; TOS keeps a
 * record of them, which the program adds to CRASH.LOG the next time it
 * starts. Plain TOS only: MiNT and MagiC catch crashes themselves. Not
 * in a desk accessory, which runs inside other programs and must never
 * end: crash_install does nothing there.
 */
#ifndef CRASH_H
#define CRASH_H

/* install the handlers; workdir is where CRASH.LOG goes; app and
   version name the program in the report ("EMail", "1.30") and must
   stay where they are */
void crash_install(const char *workdir, const char *app, const char *version);
/* put the vectors back (also done by itself however the program ends) */
void crash_remove(void);
/* what the program was doing, for the report (the status line's text) */
void crash_note(const char *what);
/* after a crash, with the report written: tell the user and close GEM
   down (set by the UI) */
extern void (*crash_cleanup)(const char *what);
/* the report, once it is in CRASH.LOG, for anywhere else it should go
   (EMail adds it to its protocol log); optional */
extern void (*crash_report)(const char *text, long n);
/* a crash TOS recorded before this run: its report is in CRASH.LOG; the
   bombs' name when the crash was in the program's code, else 0 */
const char *crash_previous(void);
/* where CRASH.LOG is ("" before crash_install, or when it does nothing) */
const char *crash_log(void);
/* test builds (-DCRASH_TEST) only: F1 to F4 (the scancode) crash on
   purpose: a bus error in the program, one in supervisor mode, an
   illegal instruction, an address error (68000). 0 for other keys */
int crash_test(short scan);

#endif
