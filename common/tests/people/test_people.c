/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/people_ask.c on the build machine: the asker's cache and questions,
 * picks and change notices against a pretend EContact, and EContact's
 * side (reading questions, answers cut into whole commands of 1 KB).
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "people_ask.h"

static u32 now = 1000;
static short econtact_id = 5;		/* -1: not running */
static int ready = 1;			/* the last PEOPLE was answered */
static char sent[16][1100];
static short sent_to[16];
static int nsent, names_calls, pick_calls, pick_n;
static PERSON pick_got[8];
static int fails;
void (*av_people_hook)(const char *s, short from);

u32 tos_ms(void) { return now; }
short appl_find(const char *n) { return !strcmp(n, "ECONTACT") ? econtact_id : -1; }
const char *av_name(void) { return "EMAIL"; }
int av_people_ready(void) { return ready; }
int av_people(short id, const char *text)
{
	if (id < 0)
		return -1;
	if (!ready)
		return 0;
	if (nsent < 16) {
		strncpy(sent[nsent], text, sizeof(sent[0]) - 1);
		sent_to[nsent] = id;
	}
	nsent++;
	ready = 0;			/* until the test plays AV_STARTED */
	return 1;
}

static void names_came(void) { names_calls++; }
static void picked(const PERSON *l, int n)
{
	int i;
	pick_calls++;
	pick_n = n;
	for (i = 0; i < n && i < 8; i++)
		pick_got[i] = l[i];
}

#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); fails++; } } while (0)

/* EContact answers the last question: known addresses get names */
static void econtact_answers(void)
{
	char ans[1100] = "names:\n", line[200], addr[64];
	const char *rest, *p;
	char asker[16];
	CHECK(people_question(sent[nsent - 1], asker, sizeof(asker), &rest) == PQ_NAMES, "question read back");
	CHECK(!strcmp(asker, "EMAIL"), "the asker named");
	p = rest;
	while ((p = people_next(p, addr, sizeof(addr))) != 0) {
		/* EContact answers with the address as it has it */
		int dana = !strcasecmp(addr, "dana@test.local");
		const char *name = !strcmp(addr, "grace@x.org") ? "Grace Baker" : dana ? "Dana Falcon" : "";
		people_line(line, sizeof(line), dana ? "Dana@Test.local" : addr, name);
		strcat(ans, line);
	}
	ready = 1;			/* AV_STARTED for the question */
	people_message(ans, econtact_id);
}

int main(void)
{
	char line[300], a[PEOPLE_ADDR], n[PEOPLE_NAME];
	const char *p, *rest;
	char asker[16];
	int i;

	/* the format */
	CHECK(people_line(line, sizeof(line), "a@b", "Ann Bee") == 12 && !strcmp(line, "a@b\tAnn Bee\n"), "a line");
	p = people_read_line("x@y\tXavier\r\nz@w\t\n", a, n);
	CHECK(p && !strcmp(a, "x@y") && !strcmp(n, "Xavier"), "read a line with CR LF");
	p = people_read_line(p, a, n);
	CHECK(p && !strcmp(a, "z@w") && !n[0], "read a line with no name");
	CHECK(people_read_line(p, a, n) == 0, "no more lines");
	memset(line, 'N', 100);
	line[100] = 0;
	people_line(line + 150, 150, "l@m", line);
	CHECK(strlen(line + 150) == 3 + 1 + 63 + 1, "names cut to 63 bytes");

	CHECK(people_init(16, 2048, names_came, picked), "init");
	CHECK(av_people_hook != 0, "init sets the hook for busy.c's loop");

	/* names: asked, shown as 0 meanwhile, then known */
	CHECK(people_name("grace@x.org") == 0, "unknown yet: 0");
	CHECK(people_name("someone@else.net") == 0, "second address queued");
	people_poll();
	CHECK(nsent == 1 && !strcmp(sent[0], "names:EMAIL grace@x.org someone@else.net") && sent_to[0] == 5, "one question with both addresses");
	CHECK(people_name("grace@x.org") == 0, "still being asked");
	econtact_answers();
	CHECK(names_calls == 1, "told the app names came");
	CHECK(!strcmp(people_name("GRACE@X.ORG"), "Grace Baker"), "known, case aside");
	CHECK(!strcmp(people_name("someone@else.net"), ""), "unknown to EContact: empty");

	/* one question at a time: nothing goes while the last is unanswered */
	people_name("dana@test.local");
	ready = 0;
	people_poll();
	CHECK(nsent == 1, "waits for AV_STARTED");
	ready = 1;
	people_poll();
	CHECK(nsent == 2, "then asks");
	econtact_answers();
	CHECK(!strcmp(people_name("dana@test.local"), "Dana Falcon"), "answer with other case");
	CHECK(!strcmp(people_name("DANA@test.LOCAL"), "Dana Falcon"), "asked in one case, looked up in another");
	people_name("Mixed@Case.Org");
	CHECK(people_name("mixed@case.org") == 0, "still being asked");
	people_poll();
	{
		const char *q = sent[nsent - 1], *f = strstr(q, "ase.");
		CHECK(f && !strstr(f + 1, "ase."), "one entry whatever the case: asked once");
	}
	econtact_answers();

	/* no answer in time: the address shows, and is asked again later */
	int before = nsent;
	people_name("late@x.org");
	people_poll();
	CHECK(nsent == before + 1, "asked late@");
	ready = 1;			/* av.c gave up on it */
	now += 4100;
	people_poll();
	CHECK(!strcmp(people_name("late@x.org"), ""), "no answer: shown as the address");
	now += 4000;
	people_poll();
	CHECK(nsent == before + 1, "not asked again within 5 s");
	now += 1100;
	people_poll();
	CHECK(nsent == before + 2 && strstr(sent[nsent - 1], "late@x.org"), "a lost answer: asked again after 5 s");
	econtact_answers();

	/* changed: forgets, and the next look asks again */
	names_calls = 0;
	CHECK(people_message("changed:", 5) == 1 && names_calls == 1, "changed: told");
	CHECK(people_name("grace@x.org") == 0, "forgotten after changed:");
	people_poll();
	econtact_answers();
	CHECK(!strcmp(people_name("grace@x.org"), "Grace Baker"), "known again");

	/* a full cache lets the oldest go */
	for (i = 0; i < 16; i++) {
		char ad[16];
		sprintf(ad, "a%d@x", i);
		people_name(ad);
	}
	CHECK(people_name("a16@x") == 0, "a seventeenth address in a cache of sixteen");

	/* no EContact: addresses show as they are */
	people_init(4, 2048, names_came, picked);
	econtact_id = -1;
	ready = 1;
	people_message("changed:", 5);
	people_name("z@z");
	people_poll();
	CHECK(!strcmp(people_name("z@z"), ""), "no EContact: plain address");
	{
		int was = nsent;
		econtact_id = 5;
		ready = 1;
		now += 6000;
		people_poll();
		CHECK(nsent == was, "no EContact: not asked again after 6 s");
		now += 25000;
		people_poll();
		CHECK(nsent == was + 1, "no EContact: asked again after 30 s");
		econtact_id = -1;
		ready = 1;
	}
	CHECK(!people_pick(""), "no pick without EContact");
	econtact_id = 5;

	/* pick: one at a time, the answer, and cancel */
	ready = 1;
	CHECK(people_pick("gra"), "pick sent");
	CHECK(!strcmp(sent[nsent - 1], "pick:EMAIL gra"), "pick names the asker and the text");
	CHECK(people_picking() && !people_pick(""), "no second pick while one is open");
	ready = 1;
	people_message("pick:\ngrace@x.org\tGrace Baker\nbob@y\tBaker, Bob \"B\"\n", 5);
	CHECK(pick_calls == 1 && pick_n == 2 && !strcmp(pick_got[1].name, "Baker, Bob \"B\""), "picked two, comma and quotes intact");
	CHECK(!people_picking(), "pick closed");
	CHECK(people_pick(0), "a new pick");
	ready = 1;
	people_message("pick:", 5);
	CHECK(pick_calls == 2 && pick_n == 0, "cancel: nobody");

	/* EContact's side: questions read, answers cut into whole commands */
	CHECK(people_question("names:ECAL a@b  c@d", asker, sizeof(asker), &rest) == PQ_NAMES && !strcmp(asker, "ECAL"), "names question");
	p = people_next(rest, a, sizeof(a));
	CHECK(p && !strcmp(a, "a@b"), "first address");
	p = people_next(p, a, sizeof(a));
	CHECK(p && !strcmp(a, "c@d") && !people_next(p, a, sizeof(a)), "second, then none");
	CHECK(people_question("pick:ECAL", asker, sizeof(asker), &rest) == PQ_PICK && !*rest, "pick without text");
	CHECK(people_question("names:\na@b\tA\n", asker, sizeof(asker), &rest) == PQ_NONE, "an answer is no question");
	CHECK(people_question("C:\\NAMES.TXT", asker, sizeof(asker), &rest) == PQ_NONE, "a path is no question");

	nsent = 0;
	ready = 1;
	people_answer(7, "names:", 0, 0);
	for (i = 0; i < 40; i++) {
		char ad[40], nm[40];
		sprintf(ad, "person%02d@example.org", i);
		sprintf(nm, "Person Number %02d", i);
		CHECK(people_answer(7, 0, ad, nm), "a line added");
	}
	people_answer(7, 0, 0, 0);
	CHECK(nsent == 1 && strlen(sent[0]) <= 1023, "the first command sent at once, within 1 KB");
	for (i = 0; i < 5 && ready == 0; i++) {
		ready = 1;
		people_poll();
	}
	CHECK(nsent == 2, "the rest after AV_STARTED");
	{
		int lines = 0;
		for (i = 0; i < nsent; i++) {
			CHECK(!strncmp(sent[i], "names:\n", 7), "each command starts names: and a line break");
			CHECK(sent_to[i] == 7, "to the asker");
			for (p = sent[i]; (p = strchr(p, '\n')) != 0; p++)
				lines++;
		}
		CHECK(lines == 40 + nsent, "every line, none cut (and the break after each names:)");
	}
	ready = 1;
	people_answer(7, "pick:", 0, 0);
	people_answer(7, 0, 0, 0);
	CHECK(!strcmp(sent[nsent - 1], "pick:"), "a cancelled pick is a bare pick:");

	/* round trip: what EContact's people_answer() sends, the asker's
	   people_message() takes (EMail found it didn't, 17 Oct 2026) */
	people_init(16, 2048, names_came, picked);
	now += 4100;			/* questions left unanswered above time out */
	ready = 1;
	people_poll();
	people_message("changed:", 5);
	ready = 1;
	nsent = 0;
	CHECK(people_name("rt1@x.org") == 0 && people_name("rt2@x.org") == 0, "round trip: asked");
	people_poll();
	ready = 1;
	{
		char q[1100], answer[1100];
		strcpy(q, sent[nsent - 1]);
		CHECK(people_question(q, asker, sizeof(asker), &rest) == PQ_NAMES, "round trip: the question read");
		people_answer(5, "names:", 0, 0);
		p = rest;
		while ((p = people_next(p, a, sizeof(a))) != 0)
			people_answer(5, 0, a, !strcmp(a, "rt1@x.org") ? "Round Trip" : "");
		people_answer(5, 0, 0, 0);
		strcpy(answer, sent[nsent - 1]);
		ready = 1;
		CHECK(people_message(answer, 5) == 1, "round trip: the answer taken");
		CHECK(people_name("rt1@x.org") && !strcmp(people_name("rt1@x.org"), "Round Trip"), "round trip: the name arrived");
		CHECK(people_name("rt2@x.org") && !strcmp(people_name("rt2@x.org"), ""), "round trip: unknown arrived empty");
		pick_calls = 0;
		people_answer(5, "pick:", 0, 0);
		people_answer(5, 0, "rt1@x.org", "Round Trip");
		people_answer(5, 0, 0, 0);
		strcpy(answer, sent[nsent - 1]);
		ready = 1;
		CHECK(people_message(answer, 5) == 1 && pick_calls == 1 && pick_n == 1 &&
		      !strcmp(pick_got[0].name, "Round Trip"), "round trip: a pick arrived");
		people_answer(5, "changed:", 0, 0);
		people_answer(5, 0, 0, 0);
		CHECK(!strcmp(sent[nsent - 1], "changed:"), "changed: sent bare");
	}

	printf("%s\n", fails ? "people_ask: FAILED" : "people_ask: ok");
	return fails != 0;
}
