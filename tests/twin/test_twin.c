/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * NAME.PRG and NAME.ACC (ui/twin.c) on the build machine, with the AES
 * standing in: plain TOS (appl_find, the program in front ap_id 0) and an
 * AES that lists its programs (appl_search).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "twin.h"

#define VA_START 0x4711
#define AV_STARTED 0x4738

typedef struct { const char *name; short type, id; } PROC;
static PROC procs[8];
static short nprocs, me, aes = 0x0140, waits;
static long cookie_magx;
static short sent[16][8], sent_to[16], nsent;
static short inbox[8][8], ninbox, inbox_at;
static int failed, checks;
static int answer_ask = 1, answer_va = 1;	/* what the other program does */

static unsigned long now_ms;
void (*av_twin_here)(short id);
unsigned long tos_ms(void) { return now_ms; }
static unsigned long hz200;
static const char *acc_file;		/* the one file Fattrib finds */
static short timers, appear_after = -1;	/* the accessory appears after that many */
static PROC late;
unsigned long tos_hz200(void) { return hz200; }
short boot_drive(void) { return 2; }
long Fattrib(const char *name, short wflag, short attr)
{
	(void)wflag; (void)attr;
	return acc_file && !strcmp(name, acc_file) ? 0 : -33;
}
short evnt_timer_(unsigned long ms)
{
	(void)ms;
	if (++timers == appear_after)
		procs[nprocs++] = late;
	return 1;
}
long get_cookie(long id) { return id == 0x4D616758L ? cookie_magx : 0; }
char *av_shared(char *local, long size) { (void)size; return local; }
short appl_id(void) { return me; }
short aes_version(void) { return aes; }

short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4)
{
	*o1 = *o2 = *o4 = 0;
	*o3 = type == 4;
	return 1;
}

short appl_find(const char *name)
{
	short i;
	for (i = 0; i < nprocs; i++)
		if (!strcmp(procs[i].name, name))
			return procs[i].id;
	return -1;
}

static short search_at;
short appl_search(short mode, char *name, short *type, short *id)
{
	if (mode == APP_FIRST)
		search_at = 0;
	if (search_at >= nprocs)
		return 0;
	strcpy(name, procs[search_at].name);
	*type = procs[search_at].type;
	*id = procs[search_at].id;
	search_at++;
	return 1;
}

static void deliver(short what, short from)
{
	memset(inbox[ninbox], 0, 16);
	inbox[ninbox][0] = what;
	inbox[ninbox][1] = from;
	ninbox++;
}

short appl_write(short id, short len, const void *buf)
{
	const short *m = buf;
	(void)len;
	if (nsent < 16) {
		memcpy(sent[nsent], buf, 16);
		sent_to[nsent++] = id;
	}
	/* the other program answers at once */
	if (m[0] == TWIN_ASK && answer_ask)
		deliver(TWIN_HERE, id);
	if (m[0] == VA_START && answer_va)
		deliver(AV_STARTED, id);
	return 1;
}

short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev)
{
	(void)flags; (void)clicks; (void)mask; (void)state; (void)timer; (void)ev;
	waits++;
	if (inbox_at < ninbox) {
		memcpy(msg, inbox[inbox_at++], 16);
		return MU_MESAG;
	}
	return MU_TIMER;
}

static void reset(void)
{
	nsent = ninbox = inbox_at = waits = 0;
	answer_ask = answer_va = 1;
	twin_reset();
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

int main(void)
{
	twin_init("ECAL|ECALST");

	/* plain TOS: ECAL.ACC is ap_id 2, ECAL.PRG in front is 0 */
	procs[0] = (PROC){ "ECAL    ", 0, 0 };
	procs[1] = (PROC){ "SCRENMGR", 0, 1 };
	procs[2] = (PROC){ "ECAL    ", 0, 2 };
	nprocs = 3;
	me = 2;
	reset();
	check(twin_prg() == 0, "plain TOS: the accessory finds its program in front");
	check(nsent == 1 && sent_to[0] == 0 && sent[0][0] == TWIN_ASK && sent[0][1] == 2, "  having asked it");
	nsent = 0;
	check(twin_prg() == 0 && nsent == 0, "  and asks once, until the program changes");
	twin_reset();
	check(twin_prg() == 0 && nsent == 1, "  then again");

	reset();
	answer_ask = 0;
	deliver(41, 0);			/* AC_CLOSE and a redraw come meanwhile */
	deliver(20, 0);
	check(twin_prg() == 0, "no answer in half a second: taken for ours, it may still be starting");
	check(waits == 5, "  having waited half a second");
	check(nsent == 3 && sent_to[1] == 2 && sent[1][0] == 41 && sent_to[2] == 2 && sent[2][0] == 20,
	      "  the messages that came meanwhile back in our queue, in order");
	nsent = 0;
	now_ms += 29000;
	check(twin_prg() == 0 && nsent == 0, "  still after 29 s, not asked again");
	now_ms += 1000;
	check(twin_prg() == -1 && nsent == 0, "  after 30 s without an answer: a name left to the desktop, not ours");
	check(twin_prg() == -1 && nsent == 0, "  and not asked again until the program changes");

	reset();
	answer_ask = 0;
	check(twin_prg() == 0 && av_twin_here != 0, "a program still starting: taken for ours (twin_init set av_twin_here)");
	av_twin_here(7);
	now_ms += 30000;
	check(twin_prg() == -1, "  a TWIN_HERE from another ap_id doesn't count");
	reset();
	answer_ask = 0;
	twin_prg();
	av_twin_here(0);		/* its answer, once it reaches its event loop */
	now_ms += 60000;
	check(twin_prg() == 0, "  its late TWIN_HERE: ours for as long as it runs");

	reset();
	answer_ask = 0;
	twin_prg();
	answer_ask = 1;
	nsent = waits = 0;
	check(twin_pass("") == 1 && nsent == 2 && sent[0][0] == TWIN_ASK && sent[1][0] == VA_START,
	      "opened from the Desk menu while waiting: asked again, it answers, AC_OPEN passed on");
	reset();
	answer_ask = 0;
	twin_prg();
	nsent = waits = 0;
	check(twin_pass("") == 0 && nsent == 1 && sent[0][0] == TWIN_ASK && waits == 10,
	      "  no answer in a second: the accessory opens itself");
	now_ms += 1000;
	check(twin_prg() == -1 && nsent == 1, "  and the name in front isn't ours from then on");

	reset();
	procs[0].name = "EMAIL   ";
	check(twin_prg() == -1 && nsent == 0, "another program in front: nothing asked");
	procs[0].name = "ECALST  ";
	check(twin_prg() == 0, "ECALST.PRG in front: ECal's program too");

	me = 0;
	reset();
	check(twin_prg() == -1, "the program itself: no program of its own to find");
	check(twin_handover("") == 0 && nsent == 0, "plain TOS: the program can't see the accessory, and runs");

	/* an AES that lists its programs (XaAES, MagiC) */
	aes = 0x0410;
	procs[0] = (PROC){ "AESSYS  ", 1, 0 };
	procs[1] = (PROC){ "ecal", APP_ACCESSORY, 3 };
	procs[2] = (PROC){ "ECAL    ", APP_APPLICATION, 5 };
	nprocs = 3;
	me = 5;
	twin_init("ECAL|ECALST");	/* the AES is asked again */
	reset();
	check(twin_handover("C:\\NEW.ICS") == 1, "the program hands over to its accessory");
	check(nsent == 1 && sent_to[0] == 3 && sent[0][0] == VA_START && sent[0][1] == 5 &&
	      !strcmp(ptr(sent[0][3], sent[0][4]), "C:\\NEW.ICS"), "  VA_START with the command line");
	reset();
	answer_va = 0;
	check(twin_handover("") == 0 && waits == 30, "an accessory that doesn't answer in 3 s: the program runs");
	reset();
	check(twin_prg() == -1, "the program: no program of its own");

	/* at boot (XaAES's run line) the accessory may register after the
	   program looks: with C:\ECAL.ACC there, it is waited for, 3 s */
	late = procs[1];
	procs[1] = procs[2];
	nprocs = 2;
	acc_file = "C:\\ECAL.ACC";
	hz200 = 10 * 200;
	appear_after = 7;
	timers = 0;
	reset();
	check(twin_handover("C:\\NEW.ICS") == 1 && timers == 7 && sent_to[0] == 3,
	      "at boot: the accessory registered 0.7 s later is waited for and handed over to");
	nprocs = 2;
	appear_after = -1;
	timers = 0;
	reset();
	check(twin_handover("") == 0 && timers == 30, "  one that doesn't come: 3 s, then the program runs");
	acc_file = "C:\\OTHER.ACC";
	timers = 0;
	reset();
	check(twin_handover("") == 0 && timers == 0, "  no ECAL.ACC or ECALST.ACC in the root: no wait");
	acc_file = "C:\\ECALST.ACC";
	hz200 = 61 * 200;
	timers = 0;
	reset();
	check(twin_handover("") == 0 && timers == 0, "  a minute after a reset: no wait");
	acc_file = 0;
	procs[1] = late;
	procs[2] = (PROC){ "ECAL    ", APP_APPLICATION, 5 };
	nprocs = 3;

	me = 3;
	reset();
	check(twin_prg() == 5 && nsent == 0, "the accessory finds its program in the list, nothing asked");
	check(twin_pass("C:\\A.ICS") == 1 && sent_to[0] == 5 && sent[0][0] == VA_START &&
	      !strcmp(ptr(sent[0][3], sent[0][4]), "C:\\A.ICS"), "  and passes it VA_START");
	procs[2].name = "ECALST";
	check(twin_prg() == 5, "  ECALST.PRG too");
	procs[2].name = "ECALENDR";
	check(twin_prg() == -1, "  ECALENDR isn't ours");
	reset();
	check(twin_pass("") == 0 && nsent == 0, "no program: twin_pass sends nothing");
	procs[2] = (PROC){ "ECAL    ", APP_ACCESSORY, 6 };
	check(twin_prg() == -1, "another accessory called ECAL isn't a program");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
