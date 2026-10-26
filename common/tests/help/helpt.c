/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * HELPT.PRG: an app that presses its Help key once it is up (help.c), for
 * prg_help.py. It says "HELPT shown N" on the console (NatFeats): 1 when a
 * viewer got the request or EHYPVIEW.PRG was started, 0 when none.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "help.h"

int main(void)
{
	char b[40];
	short r;
	appl_init();
	help_init();
	evnt_timer_(2000);
	r = (short)help_show("C:\\", "TEST.HYP", "Keys");
	snprintf(b, sizeof(b), "HELPT shown %d\n", r);
	nf_debug(b);
	evnt_timer_(8000);
	appl_exit();
	return 0;
}
