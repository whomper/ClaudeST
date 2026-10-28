/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The program another one starts, for run.py: it says on the console
 * (NatFeats) the folder it was started in and app_dir()'s, "PROBE cur
 * X:\PATH app X:\PATH\", then the screen it got (Getrez, the VDI's width),
 * and ends.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	char cur[200], b[400];
	short drv, d, work_out[57], vh, w = 0;
	appl_init();
	vh = v_opnvwk_(graf_handle(&d, &d, &d, &d), work_out);
	if (vh > 0) {
		w = work_out[0] + 1;
		v_clsvwk(vh);
	}
	drv = (short)Dgetdrv();
	cur[0] = (char)('A' + drv);
	cur[1] = ':';
	if (Dgetpath(cur + 2, drv + 1) < 0)
		cur[2] = 0;
	snprintf(b, sizeof(b), "PROBE cur %s app %s rez %ld width %d\n", cur, app_dir(), trap14_w(4), w);
	nf_debug(b);
	appl_exit();
	return 0;
}
