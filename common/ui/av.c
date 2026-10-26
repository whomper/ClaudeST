/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * av.c - the AV protocol (see av.h). From EHypView's VA_START and
 * EContact's VA_START to EMail.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "twin.h"

#define AV_PROTOKOLL	0x4700
#define VA_PROTOSTATUS	0x4701
#define VA_START	0x4711
#define AV_ACCWINDOPEN	0x4724
#define AV_ACCWINDCLOSED 0x4726
#define AV_EXIT		0x4736
#define AV_STARTED	0x4738

/* what we tell the server we understand: VA_START (bit 1), answering it
   with AV_STARTED (bit 2), quoted file names (bit 4) */
#define AV_WE_TAKE	0x0016

static char local_name[10], local_out[1024];
static char *name_buf = local_name, *out_buf;
int (*av_people_started)(const char *p);
void (*av_twin_here)(short id);
static char got[2048];			/* the last VA_START's string, ours */
static short server = -1;		/* the AV server's id, once told */
static int registered;

/* Memory another program reads, kept while we run. An accessory under
   plain TOS has no memory of its own after the start: what Malloc or
   Mxalloc give it then belongs to the program running at the moment and
   goes when that one ends. Nothing is protected there, so a static
   buffer does. Under MiNT or MagiC the accessory is a process like the
   others: global memory, which memory protection lets others read
   (Mxalloc mode 0x2B; 0x20 alone without 0x08 isn't heeded). On TOS 1.x
   Mxalloc doesn't exist (an error): the static buffer again. */
char *av_shared(char *local, long size)
{
	long m;
	if (!get_cookie(0x4D694E54L) && !get_cookie(0x4D616758L))	/* MiNT, MagX */
		return local;
	m = (long)Mxalloc(size, 0x2B);	/* global, TT RAM preferred */
	return m > 0 ? (char *)m : local;
}

static void alloc(void)
{
	if (out_buf)
		return;
	out_buf = av_shared(local_out, sizeof(local_out));
	name_buf = av_shared(local_name, sizeof(local_name));
}

void av_post(short id, short what, short w3, const char *p)
{
	short msg[8];
	memset(msg, 0, sizeof(msg));
	msg[0] = what;
	msg[1] = appl_id();
	msg[3] = w3;
	if (what == VA_START || what == AV_PEOPLE_MSG) {
		msg[3] = (short)((unsigned long)p >> 16);
		msg[4] = (short)(unsigned long)p;
	} else if (p) {
		msg[6] = (short)((unsigned long)p >> 16);
		msg[7] = (short)(unsigned long)p;
	}
	appl_write(id, 16, msg);
}

const char *av_command(void)
{
	return cmd_line();
}

/* NAME=value from our environment, or 0 */
static const char *env(const char *name)
{
	const char *e;
	long l = (long)strlen(name), k = 0;
	memcpy(&e, (const char *)_BasePage + 0x2C, sizeof(e));	/* p_env */
	while (e && *e && k < 4096) {
		long n = (long)strlen(e);
		if (n > l && !strncmp(e, name, l) && e[l] == '=')
			return e + l + 1;
		e += n + 1;
		k += n + 1;
	}
	return 0;
}

static short find_server(void)
{
	static const char *const known[] = { "THING   ", "JINNEE  ", "GEMINI  " };
	const char *s = env("AVSERVER");
	short i;
	if (s && *s) {
		char n[9];
		for (i = 0; i < 8 && s[i] && s[i] != '.'; i++)
			n[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i];
		for (; i < 8; i++)
			n[i] = ' ';
		n[8] = 0;
		return appl_find(n);
	}
	for (i = 0; i < 3; i++) {
		short id = appl_find(known[i]);
		if (id >= 0)
			return id;
	}
	return -1;
}

/* the next name off "EWEB|EWEBST", as appl_find wants it */
static const char *next_name(const char *p, char *n)
{
	short i = 0;
	while (*p && *p != '|') {
		if (i < 8)
			n[i++] = *p;
		p++;
	}
	while (i > 0 && n[i - 1] == ' ')
		i--;
	while (i < 8)
		n[i++] = ' ';
	n[8] = 0;
	return *p ? p + 1 : p;
}

/* tell the server, if there is one now */
static void tell(void)
{
	server = find_server();
	if (server < 0 || server == appl_id())
		return;
	av_post(server, AV_PROTOKOLL, AV_WE_TAKE, name_buf);
	registered = 1;
}

void av_init(const char *names)
{
	const char *p = names;
	char n[9];
	alloc();
	if (registered)
		return;
	/* the one the AES knows us by (the file may have been either);
	   else the first */
	next_name(names, name_buf);
	while (*p) {
		p = next_name(p, n);
		if (appl_find(n) == appl_id()) {
			memcpy(name_buf, n, 9);
			break;
		}
	}
	tell();
}

void av_exit(void)
{
	if (registered)
		av_post(server, AV_EXIT, appl_id(), 0);
	registered = 0;
	name_buf[0] = 0;		/* and not again, whatever comes now */
}

void av_window(short h, int open)
{
	if (registered && h > 0)
		av_post(server, open ? AV_ACCWINDOPEN : AV_ACCWINDCLOSED, h, 0);
}

int av_message(const short *msg, const char **s)
{
	if (msg[0] == VA_PROTOSTATUS)
		return AV_DONE;
	if (msg[0] == TWIN_ASK) {
		/* our accessory, under plain TOS: is this its program? */
		short reply[8];
		memset(reply, 0, sizeof(reply));
		reply[0] = TWIN_HERE;
		reply[1] = appl_id();
		appl_write(msg[1], 16, reply);
		return AV_DONE;
	}
	if (msg[0] == TWIN_HERE) {
		/* our program's late answer to TWIN_ASK (twin.c) */
		if (av_twin_here)
			av_twin_here(msg[1]);
		return AV_DONE;
	}
	if (msg[0] == AV_STARTED && av_people_started &&
	    av_people_started((const char *)(((unsigned long)(unsigned short)msg[3] << 16) |
					     (unsigned short)msg[4])))
		return AV_DONE;		/* our PEOPLE arrived (av_people.c) */
	if (msg[0] != VA_START && msg[0] != AV_PEOPLE_MSG)
		return AV_NONE;
	{
		const char *p = (const char *)(((unsigned long)(unsigned short)msg[3] << 16) |
					       (unsigned short)msg[4]);
		long n = 0;
		while (p && p[n] && n < (long)sizeof(got) - 1) {
			got[n] = p[n];
			n++;
		}
		got[n] = 0;
	}
	/* an accessory that started before the desktop: tell it now */
	if (msg[0] == VA_START && !registered && name_buf[0])
		tell();
	/* the sender may let its copy go now */
	{
		short reply[8];
		memset(reply, 0, sizeof(reply));
		reply[0] = AV_STARTED;
		reply[1] = appl_id();
		reply[3] = msg[3];
		reply[4] = msg[4];
		appl_write(msg[1], 16, reply);
	}
	*s = got;
	return msg[0] == AV_PEOPLE_MSG ? AV_PEOPLE : AV_START;
}

void (*av_people_hook)(const char *s, short from);

int av_modal_message(const short *msg)
{
	const char *s;
	int r;
	if (msg[0] != AV_PEOPLE_MSG && msg[0] != AV_STARTED)
		return 0;
	r = av_message(msg, &s);
	if (r == AV_PEOPLE && av_people_hook)
		av_people_hook(s, msg[1]);
	return r == AV_PEOPLE || r == AV_DONE;
}

const char *av_name(void)
{
	static char n[9];
	int i;
	alloc();
	memcpy(n, name_buf, 8);
	n[8] = 0;
	for (i = 7; i >= 0 && (n[i] == ' ' || !n[i]); i--)
		n[i] = 0;
	return n;
}

const char *av_arg(const char *p, char *out, long n)
{
	long k = 0;
	if (!p)
		return 0;
	while (*p == ' ')
		p++;
	if (!*p)
		return 0;
	if (*p == '\'') {
		/* 'C:\MY FILES\A.TXT', with '' for a quote */
		for (p++; *p; p++) {
			if (*p == '\'') {
				if (p[1] != '\'') {
					p++;
					break;
				}
				p++;
			}
			if (k < n - 1)
				out[k++] = *p;
		}
	} else {
		while (*p && *p != ' ') {
			if (k < n - 1)
				out[k++] = *p;
			p++;
		}
	}
	out[k] = 0;
	return p;
}

int av_send(const char *names, const char *text)
{
	char n[9];
	short id = -1;
	long l;
	while (*names && id < 0)	/* the first of "EWEB|EWEBST" running */
		id = appl_find((names = next_name(names, n), n));
	if (id < 0)
		return 0;
	alloc();
	l = (long)strlen(text);
	if (l > (long)sizeof(local_out) - 1)
		l = sizeof(local_out) - 1;
	memcpy(out_buf, text, l);
	out_buf[l] = 0;
	av_post(id, VA_START, 0, out_buf);
	return 1;
}
