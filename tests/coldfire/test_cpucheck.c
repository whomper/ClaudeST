/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_cpucheck.c - cpucheck_cf.S (the same bytes for the 68000 and the
 * ColdFire) run under qemu-m68k: with a cookie jar at 0x5A0 (page 0
 * mapped) holding _CF_, as ColdFire code (-cpu cfv4e), it calls app_main;
 * with one without it, as 68030 code, it shows cpu_alert (appl_init,
 * form_alert, appl_exit) and gives 1. Supexec (trap #14) and the AES
 * (trap #2), SIGILL here, are stood in for. -DWANT_CF: the first */
#include "rt.h"
#include "gemdos.h"

int main_check(void);		/* cpucheck_cf.S's main, renamed */
const char cpu_alert[] = "[3][needs a ColdFire][ Quit ]";
static int app_called, ops[8], nops, fails;
int app_main(void) { app_called = 1; return 42; }
#ifdef ACC
/* an accessory on the wrong machine: never ends, says nothing at the boot, its entry (ACC_NAME) and the
   alert when it is opened; the stand-in AES ends the run at its third evnt_mesag */
short _is_acc = 1;
#ifdef ACC_NAME
const char cpu_acc_name[] = "  Test";
#endif
#ifdef ACC_ALERT
const char cpu_acc_alert[] = "[3][put the other .ACC in its place][ OK ]";
#endif
static int mesags;
static void acc_end(void);
#endif

static void check(const char *what, int ok)
{
	if (!ok) {
		fails++;
		out("cpucheck: FAIL "); out(what); out("\n");
	}
}

static void sig(int s, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long pc = uc->gregs[16], sp = uc->gregs[15];
	unsigned short op;
	(void)s, (void)info;
	if (*(unsigned short *)(pc - 2) >> 4 == 0x4e4)		/* (past the trap) */
		pc -= 2;
	op = *(unsigned short *)pc;
	if (op == 0x4e4e && *(short *)sp == 38) {		/* Supexec(f) */
		long (*f)(void) = *(long (**)(void))(sp + 2);
		uc->gregs[0] = (unsigned long)f();
	} else if (op == 0x4e42 && uc->gregs[0] == 200) {	/* the AES */
		void **pb = (void **)uc->gregs[1];
		short *control = pb[0], *intin = pb[2], *intout = pb[3];
		void **addrin = pb[4];
		if (nops < 8)
			ops[nops++] = control[0];
		if (control[0] == 52)
#ifdef ACC_ALERT
			check("form_alert(1, cpu_acc_alert)", intin[0] == 1 && addrin[0] == (void *)cpu_acc_alert &&
			      control[1] == 1 && control[3] == 1);
#else
			check("form_alert(1, cpu_alert)", intin[0] == 1 && addrin[0] == (void *)cpu_alert &&
			      control[1] == 1 && control[3] == 1);
#endif
#ifdef ACC
		if (control[0] == 23) {
			short *msg = addrin[0];
			if (++mesags == 1)
				msg[0] = 40;		/* AC_OPEN */
			else if (mesags == 2)
				msg[0] = 41;		/* AC_CLOSE: nothing to do */
			else
				acc_end();
		}
#endif
		intout[0] = 1;
	} else {
		out("cpucheck: SIGILL at "); out_hex(pc); out("\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc + 2;
}

#ifdef ACC
static void acc_end(void)
{
	int i;
#ifdef ACC_NAME
	static const int want[6] = { 10, 35, 23, 52, 23, 23 };
	check("accessory: appl_init, menu_register, evnt_mesag, form_alert at AC_OPEN, evnt_mesag on", nops == 6);
	for (i = 0; i < 6 && i < nops; i++)
		check("accessory: the calls in order", ops[i] == want[i]);
#else
	static const int plain[4] = { 10, 23, 23, 23 };
	check("accessory without a name: appl_init, then evnt_mesag only", nops == 4);
	for (i = 0; i < 4 && i < nops; i++)
		check("accessory without a name: the calls in order", ops[i] == plain[i]);
#endif
	check("accessory: app_main never called", !app_called);
	out(fails ? "cpucheck: failed\n" : "cpucheck: an accessory on the wrong machine waits, its alert at AC_OPEN\n");
	sys_exit(fails != 0);
}
#endif

int main(void)
{
	/* page 0, for the cookie jar's pointer at 0x5A0 (old_mmap's six longs) */
	long m[6] = { 0, 8192, 3, 0x32 /* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED */, -1, 0 };
	long jar[8], r;
	if (sys_call4(90, (long)m, 0, 0, 0) != 0) {
		out("cpucheck: page 0 can't be mapped here\n");
		return 1;
	}
	jar[0] = 0x5f435055L, jar[1] = 60;	/* _CPU */
#ifdef WANT_CF
	jar[2] = 0x5f43465fL, jar[3] = 0;	/* _CF_ */
#else
	jar[2] = 0x5f534e44L, jar[3] = 0x1f;	/* _SND: a Falcon */
#endif
	jar[4] = 0, jar[5] = 8;
	{
		unsigned long at = 0x5a0;
		__asm__("" : "+r"(at));		/* (an address, not an object GCC knows) */
		*(long **)at = jar;
	}
	sig_install(4, sig);
	r = main_check();
#ifdef WANT_CF
	check("a ColdFire: app_main called, its answer back", app_called && r == 42 && nops == 0);
	out(fails ? "cpucheck: failed\n" : "cpucheck: on a ColdFire (_CF_) it goes on to app_main\n");
#else
	check("not a ColdFire: app_main not called", !app_called);
	check("appl_init, form_alert, appl_exit", nops == 3 && ops[0] == 10 && ops[1] == 52 && ops[2] == 19);
	check("and 1 back", r == 1);
	out(fails ? "cpucheck: failed\n" : "cpucheck: on a 68030 without _CF_ it shows the alert and goes\n");
#endif
	return fails != 0;
}
