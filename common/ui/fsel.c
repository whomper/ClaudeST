/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* fsel.c - the file selector's clicks settled (see fsel.h) */
#include "gemui.h"
#include "fsel.h"

void fsel_settle(void)
{
	short mx, my, mb, ks, msg[8];
	AEVENT ev;
	int n = 0;

	/* the button let go (at most some seconds of polling: a stuck mouse
	   doesn't hold the program) */
	do
		graf_mkstate(&mx, &my, &mb, &ks);
	while ((mb & 1) && ++n < 2000);
	/* the button events queued, taken off (a few at most) */
	for (n = 0; n < 16 && (evnt_multi_(MU_BUTTON | MU_TIMER, 1, 1, 1, 0, msg, &ev) & MU_BUTTON); n++)
		;
}
