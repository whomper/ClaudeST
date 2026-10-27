/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/notify.c on the host: the text BGEM.NG is given, and the protocol
 * (bgemng.hyp's API page) against a stand-in "BUBBLE  ": the question
 * first, a notification sent once the answer says it may, nothing where
 * there is no BGEM.NG or an old BubbleGEM answers nothing, a button's
 * click given to the app. make test
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "av.h"
#include "notify.h"

static int checks, failed;
static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static short bubble = -1;		/* the stand-in's id; -1: none runs */
static short sent[8][8];
static int nsent;
static char shared[2000];
static int shared_asked;

short appl_find(const char *name)
{
	return !strcmp(name, "BUBBLE  ") ? bubble : -1;
}
short appl_write(short id, short len, void *msg)
{
	check(id == bubble && len == 16, "appl_write: to BUBBLE, 16 bytes");
	if (nsent < 8)
		memcpy(sent[nsent++], msg, 16);
	return 1;
}
short appl_id(void)
{
	return 3;
}
char *av_shared(char *local, long size)
{
	(void)local;
	shared_asked++;
	check(size <= (long)sizeof(shared), "av_shared: a size it holds");
	return shared;
}

/* the text msg[5/6] points to: 32 bits on the Atari, so here its place
   in the shared buffer, from the low 32 bits (a 64-bit host) */
static const char *text_of(const short *m)
{
	unsigned long a = ((unsigned long)(unsigned short)m[5] << 16) | (unsigned short)m[6];
	unsigned long off = (a - (unsigned long)(size_t)shared) & 0xffffffffUL;
	return off < sizeof(shared) ? shared + off : "(not in the shared buffer)";
}

static short got_id, got_button;
static void clicked(short id, short button)
{
	got_id = id;
	got_button = button;
}

int main(void)
{
	char out[600], big[700];
	short m[8];

	notify_format(out, sizeof(out), "EMAIL   ", "New mail", "3 new messages|from Ann", "Open|Later");
	check(!strcmp(out, "[ Title=\"New mail\" From=\"EMAIL   \" ][0][3 new messages|from Ann][Open|Later]"),
	      "the header, then [0][text][buttons]");
	notify_format(out, sizeof(out), "ECAL    ", 0, "Dentist [today]", 0);
	check(!strcmp(out, "[ From=\"ECAL    \" ][0][Dentist (today)][]"), "no title, no buttons; [ ] in the text made ( )");
	notify_format(out, sizeof(out), "X       ", "A \"quoted\" title that is much longer than thirty-two", "t", 0);
	check(!strcmp(out, "[ Title=\"A 'quoted' title that is much lo\" From=\"X       \" ][0][t][]"),
	      "the title: 32 characters at most, quotes made '");
	memset(big, 'a', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	notify_format(out, 100, "X       ", 0, big, 0);
	check(strlen(out) == 99, "cut to the buffer");

	/* not set up, or no BGEM.NG: nothing sent */
	check(!notify_send("t", "x", 0, 1), "before notify_init: 0");
	notify_init("EMAIL");
	check(shared_asked == 1, "notify_init takes global memory");
	check(!notify_send("t", "x", 0, 1) && nsent == 0, "no BUBBLE: 0, nothing sent");

	/* BGEM.NG runs: asked first; the notification waits for the answer */
	bubble = 5;
	check(notify_send("New mail", "1 new message", "Open", 7) == 1, "BUBBLE runs: 1");
	check(nsent == 1 && (unsigned short)sent[0][0] == BGEMNG_INQUIRE && sent[0][1] == 3,
	      "... asked with BGEMNG_INQUIRE first");
	check(!notify_ok(), "... no answer yet");
	memset(m, 0, sizeof(m));
	m[0] = (short)BGEMNG_INQUIRE;
	m[1] = 5;
	m[7] = 0x0f;
	check(notify_message(m) == 1, "the answer is notify.c's");
	check(notify_ok(), "... notifications (4) said");
	check(nsent == 2 && (unsigned short)sent[1][0] == BUBBLEGEM_SHOW && sent[1][1] == 3 && sent[1][2] == 7 &&
	      sent[1][7] == 0x10, "... then the waiting one sent: SHOW, our id 7, a notification (0x10)");
	check(!strcmp(text_of(sent[1]), "[ Title=\"New mail\" From=\"EMAIL   \" ][0][1 new message][Open]"),
	      "... its text in the global memory, From the AES name padded to 8");

	/* the next goes at once, in the other half (the first may not be read yet) */
	check(notify_send(0, "second", 0, 8) == 1 && nsent == 3 && (unsigned short)sent[2][0] == BUBBLEGEM_SHOW,
	      "once answered: sent at once, not asked again");
	check(text_of(sent[2]) != text_of(sent[1]), "... in the other buffer");

	/* the ACKs: taken (no button), then a click */
	notify_clicked = clicked;
	memset(m, 0, sizeof(m));
	m[0] = (short)BUBBLEGEM_ACK;
	m[1] = 5;
	check(notify_message(m) == 1 && got_button == 0, "ACK of the text: ours, no click");
	m[3] = 1;
	m[4] = 7;
	check(notify_message(m) == 1 && got_button == 1 && got_id == 7, "ACK with a button: notify_clicked(7, 1)");
	m[0] = 0x4711;
	check(notify_message(m) == 0, "another message: the app's");

	/* an old BubbleGEM (another id): asked, answers nothing that says notifications */
	bubble = 9;
	nsent = 0;
	check(notify_send(0, "x", 0, 1) == 1 && nsent == 1 && (unsigned short)sent[0][0] == BGEMNG_INQUIRE,
	      "a new BUBBLE: asked again");
	memset(m, 0, sizeof(m));
	m[0] = (short)BGEMNG_INQUIRE;
	m[1] = 9;
	m[7] = 0x01;
	notify_message(m);
	check(nsent == 1 && !notify_ok(), "... it says no notifications: the waiting one isn't sent");
	check(!notify_send(0, "y", 0, 1) && nsent == 1, "... and later ones give 0");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
