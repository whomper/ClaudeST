/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * NOTET.PRG: an app that sends a notification through BGEM.NG (ui/notify.c),
 * for notify.py. It says on the console (NatFeats):
 *   "NOTET sent N"          notify_send's answer
 *   "NOTET features F"      BGEM.NG's answer to the question (F in hex)
 *   "NOTET ack"             the text taken
 *   "NOTET clicked ID B"    a button clicked
 *   "NOTET done"
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "notify.h"

static void say(const char *s)
{
	nf_debug(s);
}

static void clicked(short id, short button)
{
	char b[40];
	snprintf(b, sizeof(b), "NOTET clicked %d %d\n", id, button);
	say(b);
}

int main(void)
{
	char b[60];
	short m[8], t;
	AEVENT e;
	appl_init();
	notify_init("NOTET");
	notify_clicked = clicked;
	evnt_timer_(3000);		/* BGEM.NG up first */
	notify_ask();
	for (t = 0; t < 300; t++) {	/* 30 s of messages */
		if (t == 20) {		/* 2 s on: its answer has come */
			snprintf(b, sizeof(b), "NOTET sent %d\n",
				 notify_send("New mail", "3 new messages|from the test", "Open|Later", 42));
			say(b);
		}
		evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, m, &e);
		if (!(e.which & MU_MESAG))
			continue;
		if ((unsigned short)m[0] == BGEMNG_INQUIRE) {
			snprintf(b, sizeof(b), "NOTET features %x\n", m[7]);
			say(b);
		} else if ((unsigned short)m[0] == BUBBLEGEM_ACK && m[3] == 0) {
			say("NOTET ack\n");
		}
		notify_message(m);
	}
	say("NOTET done\n");
	appl_exit();
	return 0;
}
