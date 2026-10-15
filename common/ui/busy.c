/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * busy.c - the busy bee, and the keys while it shows (see busy.h).
 */
#include "gem.h"
#include "conn.h"
#include "busy.h"

#define KEPT 16

static short on, kept, kstate[KEPT], kreturn[KEPT];

void busy_bee(int b)
{
	on = (short)(b != 0);
	graf_mouse(b ? BUSYBEE : ARROW, 0);
}

void busy_idle(void)
{
	short msg[8], which, i;
	AEVENT ev;
	if (!on) {
		evnt_timer_(5);
		return;
	}
	which = evnt_multi_(MU_KEYBD | MU_TIMER, 0, 0, 0, 5, msg, &ev);
	if (!(which & MU_KEYBD))
		return;
	i = (short)((ev.kreturn >> 8) & 0xff);
	if (i == 0x01 || i == 0x61) {		/* Esc, Undo */
		conn_stop = 1;
	} else if (kept < KEPT) {
		kstate[kept] = ev.kstate;
		kreturn[kept] = ev.kreturn;
		kept++;
	}
}

int busy_key(AEVENT *ev)
{
	short i;
	if (!kept)
		return 0;
	ev->kstate = kstate[0];
	ev->kreturn = kreturn[0];
	for (i = 1; i < kept; i++) {
		kstate[i - 1] = kstate[i];
		kreturn[i - 1] = kreturn[i];
	}
	kept--;
	return 1;
}
