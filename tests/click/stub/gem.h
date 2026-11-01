/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* the host test's stand-in for gem.h: what click.c uses */
#define MU_KEYBD 0x0001
#define MU_BUTTON 0x0002
#define MU_M1 0x0004
#define MU_MESAG 0x0010
#define MU_TIMER 0x0020
typedef struct {
	short which, mx, my, mbutton, kstate, kreturn, breturn;
} AEVENT;
short evnt_dclick(short rate, short setit);
void evnt_set_m1(short leave, short x, short y, short w, short h);
short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev);
