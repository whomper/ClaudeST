/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * notify.c - notifications through BGEM.NG (see notify.h). Its protocol
 * is BubbleGEM's with extensions (bgemng.hyp, "API"):
 *   BGEMNG_INQUIRE to the AES program "BUBBLE  ": BGEM.NG answers with
 *     the same message, its features in msg[7] (4: notifications);
 *   BUBBLEGEM_SHOW, msg[5/6] the text in global memory, msg[7] 0x10 for a
 *     notification, msg[2] our number for it (only once BGEM.NG answered:
 *     an old BubbleGEM wants 0 there);
 *   BUBBLEGEM_ACK back: once when the text was taken, and again when a
 *     button was clicked, msg[3] the button (1 the leftmost) and msg[4]
 *     our number.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "notify.h"

#define BGEMNG_NOTIFICATION 0x0010
#define BGEMNG_HAS_NOTES    0x0004
#define TEXT_MAX            512		/* what BGEM.NG takes */
#define BUF                 (TEXT_MAX + 80)	/* the header too */

void (*notify_clicked)(short id, short button);

static char from[9];
static char local[2 * BUF];
static char *buf = local;		/* global memory, two in turn */
static short turn;
static short server = -1;		/* "BUBBLE  " that we asked */
static short features = -1;		/* its answer; -1: none yet */

void notify_init(const char *aes_name)
{
	short i, end = 0;
	for (i = 0; i < 8; i++) {
		if (!end && (!aes_name || !aes_name[i]))
			end = 1;
		from[i] = end ? ' ' : aes_name[i];
	}
	from[8] = 0;
	buf = av_shared(local, sizeof(local));
}

int notify_ok(void)
{
	return features > 0 && (features & BGEMNG_HAS_NOTES);
}

/* s put after what is in out, n bytes at most; '[' and ']' turned into
   '(' and ')', which would end BGEM.NG's own brackets */
static void add(char *out, long n, const char *s, int text)
{
	long l = (long)strlen(out);
	for (; *s && l < n - 1; s++)
		out[l++] = !text ? *s : *s == '[' ? '(' : *s == ']' ? ')' : *s;
	out[l] = 0;
}

void notify_format(char *out, long n, const char *name, const char *title, const char *text,
		   const char *buttons)
{
	char t[33];
	long k;
	out[0] = 0;
	add(out, n, "[ ", 0);
	if (title && *title) {
		/* the title: 32 characters at most, no quote */
		for (k = 0; title[k] && k < 32; k++)
			t[k] = title[k] == '"' ? '\'' : title[k];
		t[k] = 0;
		add(out, n, "Title=\"", 0);
		add(out, n, t, 0);
		add(out, n, "\" ", 0);
	}
	add(out, n, "From=\"", 0);
	add(out, n, name, 0);
	add(out, n, "\" ]", 0);
	add(out, n, "[0][", 0);
	add(out, n, text, 1);
	add(out, n, "][", 0);
	if (buttons)
		add(out, n, buttons, 1);
	add(out, n, "]", 0);
}

/* "BUBBLE  " asked what it is, once for each one that runs: 1 when one runs */
static int ask(void)
{
	short id = appl_find("BUBBLE  "), msg[8];
	if (id < 0) {
		server = -1;
		features = -1;
		return 0;
	}
	if (id == server)
		return 1;
	server = id;
	features = -1;
	memset(msg, 0, sizeof(msg));
	msg[0] = (short)BGEMNG_INQUIRE;
	msg[1] = appl_id();
	appl_write(id, 16, msg);
	return 1;
}

static void show(char *text, short id)
{
	short msg[8];
	memset(msg, 0, sizeof(msg));
	msg[0] = (short)BUBBLEGEM_SHOW;
	msg[1] = appl_id();
	msg[2] = id;
	msg[5] = (short)((unsigned long)text >> 16);
	msg[6] = (short)(unsigned long)text;
	msg[7] = BGEMNG_NOTIFICATION;
	appl_write(server, 16, msg);
}

void notify_ask(void)
{
	if (from[0])
		ask();
}

int notify_send(const char *title, const char *text, const char *buttons, short id)
{
	char *b;
	/* only once BGEM.NG said it shows notifications: an old BubbleGEM
	   never answers, and a notification waiting for an answer that never
	   comes would be lost (ECal's reminders, 27 Oct 2026) */
	if (!from[0] || !ask() || !notify_ok())
		return 0;
	b = buf + turn * BUF;
	turn ^= 1;
	notify_format(b, BUF, from, title, text, buttons);
	show(b, id);
	return 1;
}

int notify_message(const short *msg)
{
	unsigned short m = (unsigned short)msg[0];
	if (m == BGEMNG_INQUIRE) {
		if (msg[1] != server)
			return 1;
		features = msg[7];
		return 1;
	}
	if (m == BUBBLEGEM_ACK) {
		if (msg[3] > 0 && notify_clicked)
			notify_clicked(msg[4], msg[3]);
		return 1;
	}
	return 0;
}
