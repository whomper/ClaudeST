/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
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
 *
 * The terminate vector (etv_term, vector number 0x102): the catcher puts
 * crash_term on it, which puts the exception vectors back however the
 * program ends and goes on to the handler it found. On TOS that vector is
 * $408, written in supervisor mode. Under MiNT it is the process's own and
 * only Setexc(0x102, ...) sets it (a write to $408 is never called, even
 * at a Pterm); crash_install puts crash_term there with Setexc, and there
 * it restores nothing (the exception vectors are MiNT's) and just passes
 * on. A program's own handler for when it ends (EPlay's: its vectors, the
 * DMA, the matrix) is put on top of it, the same way on both:
 *
 *     old = Setexc(0x102, -1);		(crash_term, once crash_install ran)
 *     Setexc(0x102, my_term);		(a user mode call on TOS too)
 *
 * and my_term keeps every register, does its work and ends by going on to
 * `old` (move.l old,-(sp); rts), as crash_term does. Called by Pterm on
 * TOS (supervisor mode) and, under MiNT, in the process when it ends by
 * Pterm, by a signal such as SIGTERM or SIGBUS, never for SIGKILL
 * (tests/xaaes/termtest.c). crash_term is global, and crash_old_term the
 * handler it goes on to (0: none), for a chain that goes on to them by name.
 */
#ifndef CRASH_H
#define CRASH_H

/* install the handlers; workdir is where CRASH.LOG goes; app and
   version name the program in the report ("EMail", "1.30") and must
   stay where they are */
void crash_install(const char *workdir, const char *app, const char *version);
/* put the vectors back (also done by itself however the program ends); under MiNT crash_term comes off etv_term
   if it is the one on it */
void crash_remove(void);
/* the terminate handler (crash.S) and the one it goes on to */
void crash_term(void);
extern long crash_old_term;
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
/* the vectors the catcher left to another handler or chained after one,
   once installed: "" when each is ours over TOS's own, else a line for
   the app's log ("line F chained after XBRA 'XXXX' ($123456)") */
const char *crash_vectors(void);
/* where CRASH.LOG is ("" before crash_install, or when it does nothing) */
/* after crash_remove: who had hooked one of the catcher's vectors over it
   ("bus error, 2 bombs: NVDI", by XBRA name), and whether the catcher
   could be taken out of that chain; "" when every vector was still its */
const char *crash_over(void);
const char *crash_log(void);
/* test builds (-DCRASH_TEST) only: F1 to F5 (the scancode) crash on
   purpose: a bus error in the program, one in its code in supervisor
   mode (a Supexec of its), an illegal instruction, an address error
   (68000), a bus error in supervisor mode outside the program (TOS's
   bombs, recorded for the next start). 0 for other keys */
int crash_test(short scan);

#endif
