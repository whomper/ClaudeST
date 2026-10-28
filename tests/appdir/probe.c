/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The program another one starts, for run.py: it says on the console
 * (NatFeats) the folder it was started in and app_dir()'s, "PROBE cur
 * X:\PATH app X:\PATH\", and ends.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	char cur[200], b[400];
	short drv;
	appl_init();
	drv = (short)Dgetdrv();
	cur[0] = (char)('A' + drv);
	cur[1] = ':';
	if (Dgetpath(cur + 2, drv + 1) < 0)
		cur[2] = 0;
	snprintf(b, sizeof(b), "PROBE cur %s app %s\n", cur, app_dir());
	nf_debug(b);
	appl_exit();
	return 0;
}
