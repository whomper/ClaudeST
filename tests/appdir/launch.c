/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The program that starts another one, for run.py, as EMail starts
 * EWeb: A:\APP made the current folder, then shel_write(1, ...) with
 * A:\APP\PROBE.PRG, then it ends and the AES starts the probe.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	static char tail[2] = { 0, 0 };
	short ok;
	appl_init();
	Dsetdrv(0);
	Dsetpath("\\APP");
	ok = shel_write(1, 1, 0, "A:\\APP\\PROBE.PRG", tail);
	nf_debug(ok ? "LAUNCH shel_write ok\n" : "LAUNCH shel_write failed\n");
	appl_exit();
	return 0;
}
