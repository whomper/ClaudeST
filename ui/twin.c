/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * twin.c - NAME.PRG and NAME.ACC on one machine (see twin.h).
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "twin.h"

#define VA_START	0x4711
#define AV_STARTED	0x4738

enum { KIND_PRG, KIND_ACC };

static const char *names = "";
static char local_buf[1024];
static char *buf;
static short asked = -2, answer = -1;	/* ap_id 0 asked since AC_CLOSE, and what it said */
static int searching = -1;		/* the AES lists its programs: not known yet */

void twin_init(const char *n)
{
	names = n;
	searching = -1;
}

/* the AES lists its programs: appl_search (AES 4.0, MagiC, XaAES). Only
   an AES with appl_getinfo is asked: EmuTOS's hangs */
static int can_search(void)
{
	if (searching < 0) {
		short o1 = 0, o2 = 0, o3 = 0, o4 = 0;
		searching = (aes_version() >= 0x0400 || get_cookie(0x3F414749L) ||	/* ?AGI */
			     get_cookie(0x4D616758L)) &&				/* MagX */
			    appl_getinfo(4, &o1, &o2, &o3, &o4) && o3 == 1;
	}
	return searching;
}

/* the next name off "ECAL|ECALST", blank-padded to 8 as the AES keeps it */
static const char *next_name(const char *p, char *n)
{
	short i = 0;
	while (*p && *p != '|') {
		if (i < 8)
			n[i++] = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
		p++;
	}
	while (i < 8)
		n[i++] = ' ';
	n[8] = 0;
	return *p ? p + 1 : p;
}

/* a name appl_search gave (blank-padded, or not) is one of ours */
static int ours(const char *got)
{
	const char *p = names;
	char g[9], m[9];
	short i;
	for (i = 0; i < 8 && got[i]; i++)
		g[i] = got[i] >= 'a' && got[i] <= 'z' ? got[i] - 32 : got[i];
	while (i < 8)
		g[i++] = ' ';
	g[8] = 0;
	while (*p) {
		p = next_name(p, m);
		if (!memcmp(g, m, 8))
			return 1;
	}
	return 0;
}

/* the other edition running, the program or the accessory: its ap_id */
static short find(int kind)
{
	short me = appl_id(), id, type, mode = APP_FIRST;
	char n[10], m[9];
	const char *p;
	if (can_search()) {
		memset(n, 0, sizeof(n));
		while (appl_search(mode, n, &type, &id)) {
			mode = APP_NEXT;
			if (id != me && (type & (kind == KIND_ACC ? APP_ACCESSORY : APP_APPLICATION)) && ours(n))
				return id;
			memset(n, 0, sizeof(n));
		}
		return -1;
	}
	if (kind == KIND_ACC || me == 0)
		return -1;
	/* plain TOS: the program in front is ap_id 0 */
	for (p = names; *p;) {
		p = next_name(p, m);
		if (appl_find(m) == 0)
			return 0;
	}
	return -1;
}

/* s where the other program may read it */
static const char *shared(const char *s)
{
	long l = (long)strlen(s);
	if (!buf)
		buf = av_shared(local_buf, sizeof(local_buf));
	if (l > (long)sizeof(local_buf) - 1)
		l = sizeof(local_buf) - 1;
	memcpy(buf, s, l);
	buf[l] = 0;
	return buf;
}

static void post(short id, short what, const char *s)
{
	short msg[8];
	memset(msg, 0, sizeof(msg));
	msg[0] = what;
	msg[1] = appl_id();
	if (s) {
		msg[3] = (short)((unsigned long)s >> 16);
		msg[4] = (short)(unsigned long)s;
	}
	appl_write(id, 16, msg);
}

/* up to tenths of a second for the message what from id; the others
   that come meanwhile go back in our queue, in order */
static int await(short id, short what, short tenths)
{
	short msg[8], kept[8][8], nkept = 0, i, got = 0;
	AEVENT ev;
	while (tenths-- > 0 && !got) {
		if (!(evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, msg, &ev) & MU_MESAG))
			continue;
		if (msg[0] == what && msg[1] == id)
			got = 1;
		else if (nkept < 8)
			memcpy(kept[nkept++], msg, sizeof(msg));
	}
	for (i = 0; i < nkept; i++)
		appl_write(appl_id(), 16, kept[i]);
	return got;
}

int twin_handover(const char *cmd)
{
	short id;
	if (!can_search() || (id = find(KIND_ACC)) < 0)
		return 0;
	post(id, VA_START, shared(cmd ? cmd : ""));
	/* AV_STARTED: it has its copy, and we may go. An ACC that doesn't
	   answer in 3 s isn't taking it: we run after all */
	return await(id, AV_STARTED, 30);
}

short twin_prg(void)
{
	short id = find(KIND_PRG);
	if (id < 0)
		asked = -2;		/* gone: the next one is asked again */
	if (id < 0 || can_search())
		return id;
	/* plain TOS: the name may be the last program's, left to the desktop
	   on an older TOS; the answer holds until the program changes */
	if (asked != id) {
		asked = id;
		post(id, TWIN_ASK, 0);
		answer = await(id, TWIN_HERE, 5) ? id : -1;
	}
	return answer;
}

int twin_pass(const char *s)
{
	short id = twin_prg();
	if (id < 0)
		return 0;
	post(id, VA_START, shared(s ? s : ""));
	return 1;
}

void twin_reset(void)
{
	asked = -2;
	answer = -1;
}
