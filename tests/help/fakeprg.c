/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * A stand-in EHYPVIEW.PRG for prg_help.py: it says the command line it
 * was started with on the console (NatFeats), "FAKEPRG args TEXT", and
 * ends.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	char b[200];
	appl_init();
	snprintf(b, sizeof(b), "FAKEPRG args %s\n", cmd_line());
	nf_debug(b);
	appl_exit();
	return 0;
}
