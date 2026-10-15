/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * LATER.PRG - starts a program some seconds after XaAES has come up, as a
 * person would, once the desk accessories are in: run.py --later. C:\LATER.TXT
 * says how long to wait and what: "15 C:\ECAL.PRG".
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	static char t[200], tail[2];
	short msg[8];
	long h, n, secs = 0;
	char *p;
	AEVENT ev;
	appl_init();
	h = Fopen("C:\\LATER.TXT", 0);
	n = h >= 0 ? Fread((short)h, sizeof(t) - 1, t) : 0;
	if (h >= 0)
		Fclose((short)h);
	t[n > 0 ? n : 0] = 0;
	for (p = t; *p >= '0' && *p <= '9'; p++)
		secs = secs * 10 + *p - '0';
	while (*p == ' ')
		p++;
	p[strcspn(p, "\r\n")] = 0;
	for (n = 0; n < secs * 2; n++)
		evnt_multi_(MU_TIMER, 0, 0, 0, 500, msg, &ev);
	/* a GEM program (1), beside the others (1) */
	if (shel_write(1, 1, 1, p, tail) > 0)
		nf_debug("LATER started it\n");
	else
		nf_debug("LATER could not start it\n");
	appl_exit();
	return 0;
}
