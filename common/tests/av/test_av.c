/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The AV protocol (ui/av.c) on the build machine, with the AES and TOS
 * calls it makes standing in: appl_write writes down the message it was
 * given, appl_find knows the programs in `running`.
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "av.h"

void *_BasePage;
static unsigned char basepage[256];
static char environment[200];

static short sent[8], sent_to = -1, writes;
static const char *running[] = { 0, "EMAIL   ", "THING   ", 0 };	/* by id */
static int failed, checks;

long get_cookie(long id) { (void)id; return 0; }		/* plain TOS */
static unsigned long now_ms = 1000;
unsigned long tos_ms(void) { return now_ms; }
void *Mxalloc(long size, short mode) { (void)size; (void)mode; return 0; }
short appl_id(void) { return 1; }

short appl_find(const char *name)
{
	short i;
	for (i = 0; i < 4; i++)
		if (running[i] && !strcmp(running[i], name))
			return i;
	return -1;
}

short appl_write(short id, short len, const void *buf)
{
	memcpy(sent, buf, len);
	sent_to = id;
	writes++;
	return 1;
}

static int hooked;
static char hook_s[64];
static short hook_from;
static void hook(const char *s, short from)
{
	hooked++;
	strncpy(hook_s, s, sizeof(hook_s) - 1);
	hook_from = from;
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static const char *ptr(short hi, short lo)
{
	return (const char *)(((unsigned long)(unsigned short)hi << 16) | (unsigned short)lo);
}

static void set_env(const char *a, const char *b)
{
	memset(environment, 0, sizeof(environment));
	if (a) {
		strcpy(environment, a);
		if (b)
			strcpy(environment + strlen(a) + 1, b);
	}
}

static void args(const char *s, const char *want)
{
	char out[64], got[300] = "", line[100];
	const char *p = s;
	while ((p = av_arg(p, out, sizeof(out))) != 0) {
		strcat(got, "[");
		strcat(got, out);
		strcat(got, "]");
	}
	snprintf(line, sizeof(line), "av_arg <%s> -> %s", s, got);
	check(!strcmp(got, want), line);
	if (strcmp(got, want))
		printf("     want %s\n", want);
}

int main(void)
{
	static char text[] = "mailto:Maya Carter <maya@pixelpress.example>";
	static char longer[3000];
	const char *s = 0;
	int r;

	_BasePage = basepage;
	{
		char *e = environment;
		memcpy(basepage + 0x2C, &e, sizeof(e));	/* p_env */
	}

	/* file names off a VA_START string */
	args("", "");
	args("   ", "");
	args("C:\\A.TXT", "[C:\\A.TXT]");
	args("C:\\A.TXT D:\\B\\C.ICS", "[C:\\A.TXT][D:\\B\\C.ICS]");
	args("  C:\\A.TXT   D:\\B.VCF  ", "[C:\\A.TXT][D:\\B.VCF]");
	args("'C:\\MY FILES\\A.TXT' C:\\B.TXT", "[C:\\MY FILES\\A.TXT][C:\\B.TXT]");
	args("'it''s here.txt'", "[it's here.txt]");
	args("'C:\\OPEN QUOTE", "[C:\\OPEN QUOTE]");
	args("''", "[]");
	{
		char out[8];
		const char *p = av_arg("C:\\LONGFILENAME.TXT next", out, sizeof(out));
		check(!strcmp(out, "C:\\LONG") && p && !strcmp(p, " next"), "av_arg cuts a long name to fit, moves past it");
	}

	/* no AV server: nothing is sent */
	set_env("PATH=C:\\BIN", 0);
	running[2] = 0;
	writes = 0;
	av_init("EMAIL   ");
	check(writes == 0, "no AV server: no AV_PROTOKOLL");
	av_exit();
	check(writes == 0, "no AV server: no AV_EXIT");

	/* AVSERVER names it: upper case, padded, without .APP */
	running[2] = "THING   ";
	set_env("PATH=C:\\BIN", "AVSERVER=thing.app");
	av_init("EMAIL   ");
	check(writes == 1 && sent_to == 2 && sent[0] == 0x4700 && sent[1] == 1,
	      "AVSERVER=thing.app: AV_PROTOKOLL to THING");
	check(sent[3] == 0x16, "it says: VA_START, AV_STARTED, quoted names");
	check(!strcmp(ptr(sent[6], sent[7]), "EMAIL   "), "with our name, as appl_find wants it");
	av_init("EMAIL   ");
	check(writes == 1, "told once only");
	av_exit();
	check(writes == 2 && sent_to == 2 && sent[0] == 0x4736 && sent[3] == 1, "AV_EXIT with our id when quitting");
	av_exit();
	check(writes == 2, "AV_EXIT once only");

	/* AVSERVER set to a server that isn't running: nothing */
	set_env("AVSERVER=GEMINI", 0);
	av_init("EMAIL   ");
	check(writes == 2, "AVSERVER not running: nothing sent");

	/* no AVSERVER: Thing found by its name */
	set_env(0, 0);
	av_init("EMAIL   ");
	check(writes == 3 && sent_to == 2 && sent[0] == 0x4700, "no AVSERVER: Thing found and told");
	av_exit();

	/* the name we run under, of those given (we are id 1) */
	running[1] = "ECONTST ";
	av_init("ECONTACT|ECONTST");
	check(!strcmp(ptr(sent[6], sent[7]), "ECONTST "), "of ECONTACT|ECONTST, the one the AES knows us by");
	av_exit();
	running[1] = "ECAL    ";
	av_init("ECAL|ECALST");
	check(!strcmp(ptr(sent[6], sent[7]), "ECAL    "), "short names padded");
	av_exit();
	running[1] = "RENAMED ";
	av_init("EFTP|EFTPST");
	check(!strcmp(ptr(sent[6], sent[7]), "EFTP    "), "renamed to something else: the first name");
	av_exit();
	running[1] = "EMAIL   ";

	/* started before the server: told at the first VA_START */
	running[2] = 0;
	writes = 0;
	av_init("EMAIL");
	check(writes == 0, "no server yet: nothing sent");
	running[2] = "THING   ";
	{
		short msg[8] = { 0x4711, 2, 0, 0, 0, 0, 0, 0 };
		av_message(msg, &s);
		check(writes == 2 && sent[0] == 0x4738, "a VA_START later: the server told, the VA_START answered");
		av_exit();
		check(writes == 3 && sent[0] == 0x4736, "and AV_EXIT goes to it");
	}

	/* VA_START in: the string copied, AV_STARTED back to the sender */
	{
		short msg[8] = { 0x4711, 3, 0, 0, 0, 0, 0, 0 };
		msg[3] = (short)((unsigned long)text >> 16);
		msg[4] = (short)(unsigned long)text;
		writes = 0;
		r = av_message(msg, &s);
		check(r == AV_START && s && !strcmp(s, text), "VA_START: the string");
		check(s != text, "VA_START: our own copy");
		check(writes == 1 && sent_to == 3 && sent[0] == 0x4738 && sent[1] == 1 &&
		      sent[3] == msg[3] && sent[4] == msg[4], "VA_START: AV_STARTED to the sender, with its pointer");
		msg[3] = msg[4] = 0;
		r = av_message(msg, &s);
		check(r == AV_START && s && !*s, "VA_START with no string: \"\"");
		memset(longer, 'x', sizeof(longer) - 1);
		msg[3] = (short)((unsigned long)longer >> 16);
		msg[4] = (short)(unsigned long)longer;
		r = av_message(msg, &s);
		check(r == AV_START && strlen(s) == 2047, "VA_START: a very long string is cut to 2047");
	}
	{
		short msg[8] = { 0x4701, 2, 0, 0, 0, 0, 0, 0 }, other[8] = { 41, 2, 0, 0, 0, 0, 0, 0 };
		check(av_message(msg, &s) == AV_DONE, "VA_PROTOSTATUS: taken, nothing to do");
		check(av_message(other, &s) == AV_NONE, "AC_OPEN: not the AV protocol's");
		memset(msg, 0, sizeof(msg));
		msg[0] = 0x4145;		/* TWIN_ASK, from our accessory */
		msg[1] = 2;
		writes = 0;
		check(av_message(msg, &s) == AV_DONE && writes == 1 && sent_to == 2 && sent[0] == 0x4146 &&
		      sent[1] == 1, "TWIN_ASK: answered TWIN_HERE");
	}

	/* VA_START out */
	writes = 0;
	check(!av_send("ECONTACT", "x") && writes == 0, "av_send to a program not running: 0");
	check(av_send("EMAIL   ", text) && writes == 1 && sent_to == 1 && sent[0] == 0x4711 &&
	      !strcmp(ptr(sent[3], sent[4]), text) && ptr(sent[3], sent[4]) != text,
	      "av_send: VA_START with a copy of the text");
	check(av_send("EMAIL   ", longer) && strlen(ptr(sent[3], sent[4])) == 1023, "av_send: cut to 1023");

	/* PEOPLE out: one at a time, answered with AV_STARTED, lost after 3 s */
	writes = 0;
	check(av_people(-1, "names:EMAIL a@b") == -1 && writes == 0, "PEOPLE to no program: -1");
	check(av_people(1, "names:EMAIL a@b") == 1 && writes == 1 && sent_to == 1 && sent[0] == 0x4E43 &&
	      sent[1] == 1 && !strcmp(ptr(sent[3], sent[4]), "names:EMAIL a@b"), "PEOPLE: the string, as VA_START's");
	{
		short ack[8] = { 0x4738, 1, 0, 0, 0, 0, 0, 0 }, other[8] = { 0x4738, 1, 0, 0, 0, 0, 0, 0 };
		const char *s, *mine = ptr(sent[3], sent[4]);
		ack[3] = sent[3];
		ack[4] = sent[4];
		check(av_people(1, "names:EMAIL c@d") == 0 && writes == 1, "PEOPLE: the next waits for AV_STARTED");
		check(av_message(other, &s) == AV_NONE && !av_people_ready(), "an AV_STARTED for something else isn't ours");
		check(av_message(ack, &s) == AV_DONE && av_people_ready(), "AV_STARTED for our PEOPLE: the next may go");
		check(av_people(1, "names:EMAIL c@d") == 1 && writes == 2, "and it goes");
		now_ms += 2900;
		check(!av_people_ready() && av_people_lost() == 0, "2.9 s without an answer: still waiting");
		now_ms += 200;
		check(av_people_ready() && av_people_lost() == 1, "3 s: counted lost, the next may go");
		(void)mine;
	}
	/* PEOPLE in: handed over as AV_PEOPLE, answered with AV_STARTED */
	{
		short msg[8] = { 0x4E43, 3, 0, 0, 0, 0, 0, 0 };
		const char *s = 0, *text2 = "names:\na@b\tAnn\n";
		msg[3] = (short)((unsigned long)text2 >> 16);
		msg[4] = (short)(unsigned long)text2;
		writes = 0;
		check(av_message(msg, &s) == AV_PEOPLE && s && !strcmp(s, text2), "PEOPLE in: AV_PEOPLE with the string");
		check(writes == 1 && sent_to == 3 && sent[0] == 0x4738 && sent[3] == msg[3] && sent[4] == msg[4],
		      "PEOPLE in: AV_STARTED to the sender");
	}
	/* in busy.c's loop: PEOPLE to the hook, our AV_STARTED taken, the rest the app's */
	{
		short pm[8] = { 0x4E43, 3, 0, 0, 0, 0, 0, 0 }, redraw[8] = { 20, 3, 0, 0, 0, 0, 0, 0 };
		const char *t = "names:\na@b\tAnn\n";
		pm[3] = (short)((unsigned long)t >> 16);
		pm[4] = (short)(unsigned long)t;
		av_people_hook = hook;
		hooked = 0;
		check(av_modal_message(pm) == 1 && hooked == 1 && !strcmp(hook_s, t) && hook_from == 3,
		      "av_modal_message: PEOPLE to the hook, with the sender");
		check(av_modal_message(redraw) == 0 && hooked == 1, "av_modal_message: a WM_REDRAW is the app's");
		av_people(1, "names:EMAIL q@r");
		{
			short ack[8] = { 0x4738, 1, 0, 0, 0, 0, 0, 0 };
			ack[3] = sent[3];
			ack[4] = sent[4];
			check(av_modal_message(ack) == 1 && av_people_ready(), "av_modal_message: our AV_STARTED taken");
		}
	}
	av_init("EMAIL|EMAILST");
	check(!strcmp(av_name(), "EMAIL"), "av_name: our AES name without padding");

	/* the command line */
	basepage[0x80] = 10;
	memcpy(basepage + 0x81, "C:\\A.ICS \r\0", 11);
	check(!strcmp(av_command(), "C:\\A.ICS"), "av_command: the command line, without the CR");
	basepage[0x80] = 0;
	check(!strcmp(av_command(), ""), "av_command: none");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
