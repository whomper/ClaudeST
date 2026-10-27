/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cpucheck.c - the first code of a program built for the 68020 or better
 * (EUPDATE.PRG, EWEB.PRG), from EUpdate's: main() here, built for the
 * 68000 (the app's Makefile gives this file alone -m68000), says on an ST
 * that the program needs a better machine and goes; elsewhere it calls
 * the program's own app_main(). Everything else is built for the 68030,
 * the AES's binding in common's gem.c among them (a bitfield instruction
 * in its first lines), so on a 68000 such a program died at appl_init
 * with an illegal instruction before its own check could say anything
 * (the hub, 27 Oct 2026, on TOS 1.04: a white screen, then the desktop).
 * Here nothing but traps: its own Supexec for the cookie jar and its own
 * AES binding for the alert. The program defines, in its own sources:
 *
 *   const long cpu_need = 20;	(_CPU's least: 20 a 68020, 30 a 68030)
 *   const char cpu_alert[] = "[3][EUpdate needs a 68020 or|better: a Falcon or a TT.][ Quit ]";
 *   int app_main(void);		(what its main() was)
 */

extern const long cpu_need;
extern const char cpu_alert[];
int app_main(void);

/* XBIOS 38, Supexec: f in supervisor mode, its d0 back */
static long supexec(long (*f)(void))
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %1,-(%%sp)\n\t"
			 "move.w #38,-(%%sp)\n\t"
			 "trap #14\n\t"
			 "addq.l #6,%%sp"
			 : "=r"(r) : "g"(f) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

/* _CPU from the cookie jar (_p_cookies at 0x5A0), 0 where there is none:
   TOS 1.x has no jar, and only a 68000 */
static long cpu_cookie(void)
{
	unsigned long at = 0x5A0;
	long *jar;
	__asm__("" : "+r"(at));		/* an address, not an object GCC knows */
	jar = *(long **)at;
	if (!jar)
		return 0;
	for (; jar[0]; jar += 2)
		if (jar[0] == 0x5F435055L)	/* _CPU */
			return jar[1];
	return 0;
}

/* the AES, its arrays and trap #2, for the three calls the alert needs */
static short control[5], global[15], intin[16], intout[7];
static void *addrin[2], *addrout[1];
static void *aespb[6] = { control, global, intin, intout, addrin, addrout };

static short aes(short op, short nin, short nout, short nain)
{
	control[0] = op;
	control[1] = nin;
	control[2] = nout;
	control[3] = nain;
	control[4] = 0;
	__asm__ volatile("move.l %0,%%d1\n\t"
			 "move.w #200,%%d0\n\t"
			 "trap #2"
			 : : "g"(aespb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return intout[0];
}

int main(void)
{
	if (supexec(cpu_cookie) < cpu_need) {
		if (aes(10, 0, 1, 0) >= 0) {	/* appl_init */
			intin[0] = 1;
			addrin[0] = (void *)cpu_alert;
			aes(52, 1, 1, 1);	/* form_alert */
			aes(19, 0, 1, 0);	/* appl_exit */
		}
		return 1;
	}
	return app_main();
}
