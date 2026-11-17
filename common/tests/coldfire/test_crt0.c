/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_crt0.c - crt0.S as ColdFire code under qemu-m68k -cpu cfv4e:
 * started as an application, it gives back all but the basepage, text,
 * data and BSS (Mshrink), runs main on its own stack with _is_acc 0 and
 * ends with main's answer (Pterm); as an accessory (-DACC) _is_acc is 1,
 * nothing is given back and main runs on its stack too */
#include "rt.h"
#include "gemdos.h"

extern long fake_bp[];
extern void *_BasePage;
extern short _is_acc;
static int fails;

static void check(const char *what, int ok)
{
	if (!ok) {
		fails++;
		out("crt0: FAIL "); out(what); out("\n");
	}
}

#ifndef ACC
static int pterm(int code)
{
	check("Pterm gives main's answer", code == 42);
	check("Mshrink: the basepage", gd_mshrink_base == (long)fake_bp);
	check("Mshrink: text + data + BSS + 0x100", gd_mshrink_size == 0x1234 + 0x560 + 0x7a000 + 0x100);
	check("two GEMDOS calls", gd_calls == 2);
	out(fails ? "crt0: failed\n" : "crt0: an application started, shrunk and ended as on TOS\n");
	return fails != 0;
}
#endif

int main(void)
{
	char here;
	check("_BasePage", _BasePage == (void *)fake_bp);
	/* (crt0's stack is the 48 KB after _is_acc in its BSS) */
	unsigned long at = (unsigned long)&here, acc = (unsigned long)&_is_acc;
	check("main on crt0's stack", at > acc && at < acc + 2 + 3 + 49152);
#ifdef ACC
	check("_is_acc 1", _is_acc == 1);
	check("nothing given back", gd_calls == 0);
	out(fails ? "crt0: failed\n" : "crt0: an accessory started as on TOS\n");
	sys_exit(fails != 0);
#else
	check("_is_acc 0", _is_acc == 0);
	gd_pterm = pterm;
	return 42;
#endif
}
