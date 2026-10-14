/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * CRASHT.PRG - checks the shared crash catcher (atari/crash.c) in an
 * emulator. C:\CRASHT.INF says what to do:
 *   b  a bus error in the program        a  an address error (68000)
 *   i  an illegal instruction            z  a division by zero
 *   s  a bus error in supervisor mode (in the program's own code): the
 *      bombs, which TOS records; the run after the next reset finds the
 *      record and, since it was in the program, says so (crash_previous)
 * The catcher writes C:\CRASH.LOG; this program notes on the emulator's
 * console (NatFeats) what it saw, and crash_cleanup and crash_report
 * leave C:\CLEANUP.TXT and C:\REPORT.TXT. run.py drives it in Hatari.
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "crash.h"

static char kind;

static void put(const char *path, const char *s, long n)
{
	long h = Fcreate(path, 0);
	if (h >= 0) {
		Fwrite((short)h, n, s);
		Fclose((short)h);
	}
}

static void cleanup(const char *what)
{
	char s[100];
	snprintf(s, sizeof(s), "cleanup: %s\r\n", what);
	put("C:\\CLEANUP.TXT", s, strlen(s));
	nf_debug("CRASHT cleanup\n");
}

static void report(const char *text, long n)
{
	put("C:\\REPORT.TXT", text, n);
}

static void sup_bus(void)
{
	volatile long *p = (volatile long *)0xFFFFFA40L;	/* nothing there */
	(void)*p;
}

static volatile long sink;

int main(void)
{
	char inf[8];
	long h = Fopen("C:\\CRASHT.INF", 0);
	const char *prev;
	if (h < 0) {
		nf_debug("CRASHT no CRASHT.INF\n");
		return 1;
	}
	inf[0] = 0;
	Fread((short)h, 1, inf);
	Fclose((short)h);
	kind = inf[0];

	crash_install("C:\\", "CrashTest", "1.0");
	crash_cleanup = cleanup;
	crash_report = report;
	prev = crash_previous();
	if (Fopen("C:\\STAGE1.TXT", 0) >= 0) {		/* after the bombs and a reset */
		char s[100];
		snprintf(s, sizeof(s), "CRASHT previous %s\n", prev ? prev : "none");
		nf_debug(s);
		crash_remove();
		return 0;
	}
	crash_note("about to crash");
	nf_debug("CRASHT crashing\n");
	switch (kind) {
	case 'b': {
		static volatile long *volatile low;
		low = (volatile long *)0x10L;			/* user mode: bus error */
		*low = 0;
		break;
	}
	case 'a': {
		static const char b[4];
		sink = *(volatile const short *)(b + 1);	/* odd: address error on a 68000 */
		break;
	}
	case 'i': {
		static const unsigned short ill[] = { 0x4afc };
		((void (*)(void))ill)();
		break;
	}
	case 'z': {
		volatile short d = 0;
		long q;
		__asm__ volatile("move.l #100,%0\n\tdivs.w %1,%0" : "=&d"(q) : "d"(d));
		sink = q;
		break;
	}
	case 's':
		put("C:\\STAGE1.TXT", "s\r\n", 3);
		Supexec(sup_bus);
		break;
	}
	nf_debug("CRASHT no crash\n");
	crash_remove();
	return 2;
}
