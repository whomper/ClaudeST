/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* gemdos.c - a stand-in for GEMDOS under qemu-m68k: Linux gives trap #1
 * as SIGILL, and this handler takes it as GEMDOS would (Mshrink noted,
 * Pterm the exit), so that crt0.S runs as TOS starts it */
#include "rt.h"
#include "gemdos.h"

void sig_entry(void);
extern void (*sig_handler)(int, void *, void *);

long gd_mshrink_base, gd_mshrink_size, gd_calls;
int (*gd_pterm)(int code);


static void handler(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long sp = uc->gregs[15], pc = uc->gregs[16];
	unsigned short fn = *(unsigned short *)sp;
	(void)sig; (void)info;
	if (*(unsigned short *)pc == 0x4e41)	/* (at the trap: past it) */
		pc += 2;
	else if (*(unsigned short *)(pc - 2) != 0x4e41) {
		out("gemdos: SIGILL at "); out_hex(pc); out(", not a trap #1\n");
		sys_exit(3);
	}
	gd_calls++;
	switch (fn) {
	case 0x4a:			/* Mshrink(0, base, size) */
		gd_mshrink_base = *(long *)(sp + 4);
		gd_mshrink_size = *(long *)(sp + 8);
		uc->gregs[0] = 0;
		break;
	case 0x4c:			/* Pterm(code) */
		sys_exit(gd_pterm(*(short *)(sp + 2)));
	case 0x00:			/* Pterm0 */
		sys_exit(gd_pterm(0));
	default:
		out("gemdos: function "); out_hex(fn); out(" not stood in for\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc;
}

void sig_install(int sig, void (*h)(int, void *, void *))
{
	struct {
		void *handler;
		unsigned long flags;
		void *restorer;
		unsigned long mask[2];
	} sa;
	memset(&sa, 0, sizeof sa);
	sig_handler = h;
	sa.handler = (void *)sig_entry;
	sa.flags = 4;			/* SA_SIGINFO */
	if (sys_call4(174, sig, (long)&sa, 0, 8) != 0) {	/* rt_sigaction */
		out("gemdos: rt_sigaction failed\n");
		sys_exit(3);
	}
}

void gd_install(void)
{
	sig_install(4, handler);	/* SIGILL */
}
