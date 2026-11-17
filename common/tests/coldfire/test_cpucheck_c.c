/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_cpucheck_c.c - atari/cpucheck.c (68000 code, its main renamed)
 * run under qemu-m68k -cpu m68030, with a cookie jar at 0x5A0 (page 0
 * mapped) changed between the cases. Supexec (trap #14) and the AES
 * (trap #2), SIGILL here, are stood in for. -DCF_ALERT defines
 * cpu_cf_alert, as EWEB.PRG does; without it nothing does, as EUpdate:
 *   _CPU 0:              cpu_alert, app_main not called, 1 back
 *   _CPU 60, no _CF_:    app_main
 *   _CPU 60, _CF_:       cpu_cf_alert (with CF_ALERT), else app_main
 *   _CPU 60, _CF_ = 0:   the same: _CF_ counts whatever its value */
#include "rt.h"
#include "gemdos.h"

int main_check(void);		/* cpucheck.c's main, renamed */
const long cpu_need = 30;
const char cpu_alert[] = "[3][needs a 68030][ Quit ]";
#ifdef CF_ALERT
const char cpu_cf_alert[] = "[3][run the FireBee edition][ Quit ]";
#endif
static int app_called, ops[8], nops, fails;
static const void *alerted;
int app_main(void) { app_called = 1; return 42; }

static void check(const char *what, int ok)
{
	if (!ok) {
		fails++;
		out("cpucheck.c: FAIL "); out(what); out("\n");
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
		if (control[0] == 52) {
			check("form_alert(1, ...)", intin[0] == 1 && control[1] == 1 && control[3] == 1);
			alerted = addrin[0];
		}
		intout[0] = 1;
	} else {
		out("cpucheck.c: SIGILL at "); out_hex(pc); out("\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc + 2;
}

static long jar[8];

static void run(const char *name, long cpu, int cf, long cfval, const void *want)
{
	long r;
	int n = 0;
	jar[n++] = 0x5f435055L, jar[n++] = cpu;		/* _CPU */
	if (cf)
		jar[n++] = 0x5f43465fL, jar[n++] = cfval;	/* _CF_ */
	jar[n++] = 0, jar[n++] = 8;
	app_called = 0, nops = 0, alerted = 0;
	r = main_check();
	if (want) {
		check(name, !app_called && r == 1 && alerted == want &&
		      nops == 3 && ops[0] == 10 && ops[1] == 52 && ops[2] == 19);
	} else {
		check(name, app_called && r == 42 && nops == 0);
	}
}

int main(void)
{
	long m[6] = { 0, 8192, 3, 0x32 /* MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED */, -1, 0 };
	if (sys_call4(90, (long)m, 0, 0, 0) != 0) {
		out("cpucheck.c: page 0 can't be mapped here\n");
		return 1;
	}
	{
		unsigned long at = 0x5a0;
		__asm__("" : "+r"(at));		/* (an address, not an object GCC knows) */
		*(long **)at = jar;
	}
	sig_install(4, sig);
	run("_CPU 0: cpu_alert", 0, 0, 0, cpu_alert);
	run("_CPU 60, no _CF_: app_main", 60, 0, 0, 0);
#ifdef CF_ALERT
	run("_CPU 60, _CF_: cpu_cf_alert", 60, 1, 0x10000, cpu_cf_alert);
	run("_CPU 60, _CF_ 0: cpu_cf_alert", 60, 1, 0, cpu_cf_alert);
	run("_CPU 0, _CF_: cpu_alert first", 0, 1, 0x10000, cpu_alert);
	out(fails ? "cpucheck.c: failed\n" : "cpucheck.c: with cpu_cf_alert, _CF_ shows it and goes\n");
#else
	run("_CPU 60, _CF_, no cpu_cf_alert: app_main", 60, 1, 0x10000, 0);
	out(fails ? "cpucheck.c: failed\n" : "cpucheck.c: without cpu_cf_alert, _CF_ changes nothing\n");
#endif
	return fails != 0;
}
