/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FILLJAR.PRG - run.py's second boot puts it in C:\AUTO before
 * FBSTAND.PRG (AAFILL.PRG: the AUTO folder runs in name order): it fills
 * every free slot of TOS's cookie jar with cookies 'FIL' and a number,
 * so that FBSTAND has to make a bigger jar and stay resident.
 */
#include <stdio.h>
#include "tos.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

#pragma GCC diagnostic ignored "-Warray-bounds"	/* the cookie jar's fixed address */
static long added, slots, at;
static long fill(void)
{
	long *j = *(long **)0x5a0, i;
	at = (long)j;
	if (!j)
		return 0;
	for (i = 0; j[2 * i]; i++)
		;
	slots = j[2 * i + 1];
	while (i + 1 < slots) {
		j[2 * i] = 0x46494C00L | (added++ & 0xFF);
		j[2 * i + 1] = 0;
		i++;
		j[2 * i] = 0;
		j[2 * i + 1] = slots;
	}
	return 0;
}

int main(void)
{
	char b[120];
	Supexec(fill);
	snprintf(b, sizeof(b), "FILLJAR jar at %08lx: %ld added, %ld slots\n", at, added, slots);
	nf_debug(b);
	return 0;
}
