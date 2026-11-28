/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* aesprobe.c - what an AES shows of itself before any form_alert, on the
   emulator's console ("AESPROBE ..." lines): the cookie jar's ids, the AES
   version and ap_count, appl_getinfo 0, 4 and 96 to 99, appl_find of
   "MYAES", "XAAES" and "ALERT", appl_search's names; again 5 s later, then
   "AESPROBE done". Run under each AES to tell them apart (EPlay, 27 Nov
   2026: MyAES 0.99 has the cookies MAS1 and MAS2 from boot and no ALERT
   running before its first alert; XaAES has nAES):
     run.py WORK EMUTOS.IMG build/AESPROBE.PRG [--aes myaes] --machine ct60
            --steps "until AESPROBE done 240" */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"

long nf_on = 1;
static long jar;
#pragma GCC diagnostic ignored "-Warray-bounds"
static long rd_jar(void) { jar = *(volatile long *)0x5a0L; return 0; }

static void report(const char *when)
{
	char b[200], ids[160];
	short o1, o2, o3, o4, k, t, id;
	char nm[12];
	int n = 0;
	Supexec(rd_jar);
	ids[0] = 0;
	if (jar) {
		long *c = (long *)jar;
		for (; c[0] && n < 30; c += 2, n++) {
			char s[6];
			s[0] = (char)(c[0] >> 24); s[1] = (char)(c[0] >> 16); s[2] = (char)(c[0] >> 8); s[3] = (char)c[0];
			s[4] = ' '; s[5] = 0;
			strncat(ids, s, sizeof(ids) - strlen(ids) - 1);
		}
	}
	snprintf(b, sizeof(b), "AESPROBE %s cookies: %s\n", when, ids);
	nf_debug(b);
	snprintf(b, sizeof(b), "AESPROBE %s global0 %04x count %d\n", when, (unsigned short)aes_version(), aes_count());
	nf_debug(b);
	{
		static const short ty[] = { 0, 4, 96, 97, 98, 99 };
		for (k = 0; k < 6; k++) {
			o1 = o2 = o3 = o4 = -99;
			short r = appl_getinfo(ty[k], &o1, &o2, &o3, &o4);
			snprintf(b, sizeof(b), "AESPROBE %s getinfo %d: r %d %d %d %d %d\n", when, ty[k], r, o1, o2, o3, o4);
			nf_debug(b);
		}
	}
	snprintf(b, sizeof(b), "AESPROBE %s find MYAES %d XAAES %d ALERT %d\n", when, appl_find("MYAES   "),
		 appl_find("XAAES   "), appl_find("ALERT   "));
	nf_debug(b);
	for (k = 0; k < 40; k++) {
		memset(nm, 0, sizeof(nm));
		if (!appl_search(k ? 1 : 0, nm, &t, &id))
			break;
		snprintf(b, sizeof(b), "AESPROBE %s search '%s' type %d id %d\n", when, nm, t, id);
		nf_debug(b);
	}
}

int main(void)
{
	appl_init();
	report("start");
	evnt_timer_(5000);
	report("later");
	nf_debug("AESPROBE done\n");
	appl_exit();
	return 0;
}
