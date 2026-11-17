/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* machine.h's block: written line by line, each risky read guarded */
#include <tos.h>

#include "machine.h"

extern long guard_addr, guard_what, guard_val, guard_ok;
long guard_run(void);

long mb_cpu, mb_fpu, mb_mch, mb_cacr;
int mb_cacr_ok;

/* one guarded read: 1 and *v its value, or 0 (no answer) */
static int peek(long what, unsigned long addr, long *v)
{
	guard_what = what;
	guard_addr = (long) addr;
	Supexec(guard_run);
	*v = guard_ok ? guard_val : 0;
	return guard_ok != 0;
}

/* a cookie's value: 1 found, 0 not there, -1 no answer */
static int cookie(unsigned long id, long *v)
{
	long jar, name, k;
	*v = 0;
	if (!peek(0, 0x5A0, &jar))
		return -1;
	if (!jar)
		return 0;
	for (k = 0; k < 1024; k++) {
		if (!peek(0, (unsigned long) jar + k * 8, &name))
			return -1;
		if (!name)
			return 0;
		if ((unsigned long) name == id)
			return peek(0, (unsigned long) jar + k * 8 + 4, v) ? 1 : -1;
	}
	return 0;
}

static const char *said(int r)
{
	return r < 0 ? "no answer" : r == 0 ? "none" : "";
}

void machine_block(void (*out)(const char *fmt, ...))
{
	long v, os, ver, date;
	int r;
	out("machine (each line as it is read; \"no answer\" where it refused):\n");
	r = cookie(0x5F4D4348L, &mb_mch);			/* _MCH */
	if (r > 0)
		out("  _MCH %08lx\n", mb_mch);
	else
		out("  _MCH %s\n", said(r));
	r = cookie(0x5F435055L, &mb_cpu);			/* _CPU */
	if (r > 0)
		out("  _CPU %ld\n", mb_cpu);
	else
		out("  _CPU %s\n", said(r));
	r = cookie(0x5F465055L, &mb_fpu);			/* _FPU */
	if (r > 0)
		out("  _FPU %08lx\n", mb_fpu);
	else
		out("  _FPU %s\n", said(r));
	r = cookie(0x5F56444FL, &v);				/* _VDO */
	if (r > 0)
		out("  _VDO %08lx\n", v);
	else
		out("  _VDO %s\n", said(r));
	r = cookie(0x5F43465FL, &v);				/* _CF_ (FireTOS) */
	if (r > 0)
		out("  _CF_ %08lx\n", v);
	else
		out("  _CF_ %s\n", said(r));
	r = cookie(0x4D694E54L, &v);				/* MiNT */
	if (r > 0)
		out("  MiNT %ld.%ld\n", (v >> 8) & 0xff, v & 0xff);
	else
		out("  MiNT %s\n", r < 0 ? "no answer" : "not running");
	/* the OS header (_sysbase): its version and date */
	if (peek(0, 0x4F2, &os) && os && peek(2, (unsigned long) os + 2, &ver) &&
	    peek(0, (unsigned long) os + 0x18, &date))
		out("  TOS %lx.%02lx, dated %02lx/%02lx/%04lx (mm/dd/yyyy)\n", (ver >> 8) & 0xff, ver & 0xff,
		    (date >> 24) & 0xff, (date >> 16) & 0xff, date & 0xffff);
	else
		out("  TOS header: no answer\n");
	/* the CACR: only a 68020 or better has one; a ColdFire's can't be
	   read (guarded all the same) */
	mb_cacr_ok = 0;
	if (mb_cpu >= 20 && peek(1, 0, &mb_cacr)) {
		mb_cacr_ok = 1;
		out("  CACR %04lx: instructions %s, data %s\n", mb_cacr & 0xffff,
		    mb_cacr & 0x0001 ? "on" : "off", mb_cacr & 0x0100 ? "on" : "off");
	} else {
		out("  CACR %s\n", mb_cpu >= 20 ? "no answer" : "not read (no 68020 or better)");
	}
}
