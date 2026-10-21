/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people_ask.c - asking EContact about people (see people_ask.h): a cache
 * of names, questions sent one at a time in PEOPLE messages, picks, and
 * the answers EContact sends, cut into whole commands of at most 1 KB.
 * Memory is taken once, in people_init, so an accessory takes it at start.
 */
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "av.h"
#include "people_ask.h"

#define CMD_MAX		1023		/* av.c's buffer, less its 0 */
#define WAIT_ANSWER	4000UL		/* asked and no answer: show the address */
#define WAIT_ABSENT	30000UL		/* no EContact: ask again after this */
#define WAIT_LOST	5000UL		/* asked, no answer (taken by a loop that
					   doesn't read messages): again after this */

enum { E_FREE, E_QUEUED, E_ASKED, E_KNOWN, E_ABSENT };

typedef struct {
	char addr[PEOPLE_ADDR];
	char name[PEOPLE_NAME];
	char state;
	char lost;			/* absent because the answer never came */
	char seen;			/* looked for in EContact's file, not there */
	char inscan;			/* waiting when the file's reading began */
	unsigned long at;		/* asked, or found absent (tos_ms) */
	unsigned long used;		/* for letting the oldest go */
} ENTRY;

static ENTRY *cache;
static int ncache;
static unsigned long tick;
static void (*on_names)(void);
static void (*on_picked)(const PERSON *list, int n);

static char *outbox;			/* [short to][text][0] ... */
static long out_size, out_len;
static char cur[CMD_MAX + 1];		/* the answer being built */
static short cur_to = -1;
static const char *cur_kind = "";

static int pick_open, pick_wanted;
static char pick_text[CMD_MAX + 1];

int (*people_scan_hook)(void (*line)(const char *addr, const char *name, void *ud), void *ud, int force);
int (*people_pick_hook)(const char *partial, void (*picked)(const PERSON *list, int n));
int (*people_busy_hook)(void);

static int file_busy(void)
{
	return people_busy_hook && people_busy_hook();
}

static int same(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
		char y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

static void copy(char *d, const char *s, int size)
{
	int i;
	for (i = 0; i < size - 1 && s[i]; i++)
		d[i] = s[i];
	d[i] = 0;
}

static void hook(const char *s, short from)
{
	people_message(s, from);
}

int people_init(int n, long outbox_bytes, void (*names_came)(void),
		void (*picked)(const PERSON *list, int n))
{
	on_names = names_came;
	on_picked = picked;
	av_people_hook = hook;
	if (!cache && n > 0) {
		cache = calloc((size_t)n, sizeof(ENTRY));
		ncache = cache ? n : 0;
	}
	if (!outbox && outbox_bytes > 0) {
		outbox = malloc((size_t)outbox_bytes);
		out_size = outbox ? outbox_bytes : 0;
	}
	out_len = 0;
	return cache != 0 && (outbox_bytes <= 0 || outbox != 0);
}

/* ---- the string format: "kind:" then lines "address<TAB>name\n" ---- */

int people_line(char *out, long size, const char *addr, const char *name)
{
	long a = (long)strlen(addr), n = (long)strlen(name);
	if (n > PEOPLE_NAME - 1)
		n = PEOPLE_NAME - 1;
	if (a + n + 3 > size)
		return 0;
	memcpy(out, addr, a);
	out[a] = '\t';
	memcpy(out + a + 1, name, n);
	out[a + 1 + n] = '\n';
	out[a + 2 + n] = 0;
	return (int)(a + 2 + n);
}

const char *people_read_line(const char *p, char *addr, char *name)
{
	int i = 0;
	while (*p == '\r' || *p == '\n')
		p++;
	if (!*p)
		return 0;
	while (*p && *p != '\t' && *p != '\r' && *p != '\n') {
		if (i < PEOPLE_ADDR - 1)
			addr[i++] = *p;
		p++;
	}
	addr[i] = 0;
	i = 0;
	if (*p == '\t') {
		p++;
		while (*p && *p != '\r' && *p != '\n') {
			if (i < PEOPLE_NAME - 1)
				name[i++] = *p;
			p++;
		}
	}
	name[i] = 0;
	return p;
}

/* ---- the asker's side ---- */

static ENTRY *find(const char *addr)
{
	int i;
	for (i = 0; i < ncache; i++)
		if (cache[i].state != E_FREE && same(cache[i].addr, addr))
			return &cache[i];
	return 0;
}

static ENTRY *take(void)
{
	int i, best = -1;
	for (i = 0; i < ncache; i++) {
		if (cache[i].state == E_FREE)
			return &cache[i];
		if (cache[i].state != E_ASKED && (best < 0 || cache[i].used < cache[best].used))
			best = i;
	}
	return best >= 0 ? &cache[best] : 0;
}

const char *people_name(const char *addr)
{
	ENTRY *e;
	if (!addr || !*addr || strlen(addr) >= PEOPLE_ADDR)
		return "";
	e = find(addr);
	if (!e) {
		e = take();
		if (!e)
			return "";		/* every entry is out being asked */
		memset(e, 0, sizeof(*e));
		copy(e->addr, addr, PEOPLE_ADDR);
		e->state = E_QUEUED;
	}
	e->used = ++tick;
	if (e->state == E_KNOWN)
		return e->name;
	if (e->state == E_ABSENT)
		return "";
	return 0;
}

static short econtact(void)
{
	short id = appl_find("ECONTACT");
	return id >= 0 ? id : appl_find("ECONTST ");
}

int people_pick(const char *partial)
{
	int l;
	if (pick_open)
		return 0;
	if (econtact() < 0)		/* EContact's file, when chosen */
		return people_pick_hook ? people_pick_hook(partial, on_picked) : 0;
	l = (int)strlen("pick:") + (int)strlen(av_name());
	strcpy(pick_text, "pick:");
	strcat(pick_text, av_name());
	if (partial && *partial && l + 1 + (long)strlen(partial) <= CMD_MAX) {
		strcat(pick_text, " ");
		strcat(pick_text, partial);
	}
	pick_open = pick_wanted = 1;
	people_poll();
	return 1;
}

int people_picking(void)
{
	return pick_open;
}

void people_forget(void)
{
	int i;
	for (i = 0; i < ncache; i++)
		if (cache[i].state == E_ASKED)
			cache[i].state = E_QUEUED;
	pick_open = pick_wanted = 0;
}

static void got_names(const char *p)
{
	char addr[PEOPLE_ADDR], name[PEOPLE_NAME];
	int changed = 0;
	while ((p = people_read_line(p, addr, name)) != 0) {
		ENTRY *e = find(addr);
		if (!e)
			continue;
		if (e->state != E_KNOWN || strcmp(e->name, name))
			changed = 1;
		e->state = E_KNOWN;
		copy(e->name, name, PEOPLE_NAME);
	}
	if (changed && on_names)
		on_names();
}

static void got_pick(const char *p)
{
	char addr[PEOPLE_ADDR], name[PEOPLE_NAME];
	const char *q = p;
	PERSON *list;
	int n = 0, k = 0;
	pick_open = pick_wanted = 0;
	while ((q = people_read_line(q, addr, name)) != 0)
		n++;
	list = n ? malloc((size_t)n * sizeof(PERSON)) : 0;
	if (n && !list)
		n = 0;
	while (k < n && (p = people_read_line(p, addr, name)) != 0) {
		ENTRY *e;
		copy(list[k].addr, addr, PEOPLE_ADDR);
		copy(list[k].name, name, PEOPLE_NAME);
		k++;
		e = find(addr);		/* what was picked is known now */
		if (e) {
			e->state = E_KNOWN;
			copy(e->name, name, PEOPLE_NAME);
		}
	}
	if (on_picked)
		on_picked(list, k);
	free(list);
}

int people_message(const char *s, short from)
{
	(void)from;
	/* answers have nothing between the colon and the first line;
	   questions name their asker there (EContact's side) */
	if (!strncmp(s, "names:", 6) && (s[6] == '\n' || s[6] == '\r' || !s[6])) {
		got_names(s + 6);
		return 1;
	}
	if (!strncmp(s, "pick:", 5) && (s[5] == '\n' || s[5] == '\r' || !s[5])) {
		got_pick(s + 5);
		return 1;
	}
	if (!strncmp(s, "changed:", 8)) {
		int i;
		for (i = 0; i < ncache; i++)
			if (cache[i].state == E_KNOWN || cache[i].state == E_ABSENT)
				cache[i].state = E_FREE;
		if (on_names)
			on_names();
		return 1;
	}
	return 0;
}

/* ---- EContact's side ---- */

int people_question(const char *s, char *asker, int asker_size, const char **rest)
{
	int kind, i = 0;
	if (!strncmp(s, "names:", 6)) {
		kind = PQ_NAMES;
		s += 6;
	} else if (!strncmp(s, "pick:", 5)) {
		kind = PQ_PICK;
		s += 5;
	} else
		return PQ_NONE;
	if (!*s || *s == '\n' || *s == '\r' || *s == ' ')
		return PQ_NONE;		/* an answer, or no asker */
	while (*s && *s != ' ' && *s != '\n') {
		if (i < asker_size - 1)
			asker[i++] = *s;
		s++;
	}
	asker[i] = 0;
	while (*s == ' ')
		s++;
	*rest = s;
	return kind;
}

const char *people_next(const char *p, char *addr, int size)
{
	int i = 0;
	while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
		p++;
	if (!*p)
		return 0;
	while (*p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
		if (i < size - 1)
			addr[i++] = *p;
		p++;
	}
	addr[i] = 0;
	return p;
}

static int push(void)
{
	long l = (long)strlen(cur) + 1;
	if (out_len + (long)sizeof(short) + l > out_size)
		return 0;
	memcpy(outbox + out_len, &cur_to, sizeof(short));
	memcpy(outbox + out_len + sizeof(short), cur, l);
	out_len += (long)sizeof(short) + l;
	return 1;
}

int people_answer(short to, const char *kind, const char *addr, const char *name)
{
	char line[PEOPLE_ADDR + PEOPLE_NAME + 4];
	int l;
	if (kind) {			/* start: "names:" and a line break */
		cur_to = to;
		cur_kind = kind;
		copy(cur, kind, sizeof(cur));
		strcat(cur, "\n");
		return 1;
	}
	if (!addr) {			/* end; with no lines, the bare kind ("pick:") */
		int ok;
		long k = (long)strlen(cur_kind);
		if ((long)strlen(cur) == k + 1 && cur[k] == '\n')
			cur[k] = 0;
		ok = push();
		cur[0] = 0;
		people_poll();
		return ok;
	}
	l = people_line(line, sizeof(line), addr, name ? name : "");
	if (!l)
		return 0;
	if ((long)strlen(cur) + l > CMD_MAX) {
		if (!push())
			return 0;
		copy(cur, cur_kind, sizeof(cur));
		strcat(cur, "\n");
	}
	strcat(cur, line);
	return 1;
}

/* ---- sending, one at a time ---- */

static int send_outbox(void)
{
	short to;
	long l;
	if (out_len <= 0)
		return 0;
	memcpy(&to, outbox, sizeof(short));
	l = (long)sizeof(short) + (long)strlen(outbox + sizeof(short)) + 1;
	if (av_people(to, outbox + sizeof(short)) == 0)
		return 1;		/* the last isn't answered yet */
	memmove(outbox, outbox + l, out_len - l);	/* sent, or that program is gone */
	out_len -= l;
	return 1;
}

/* names from EContact's file, for the addresses waiting (EContact isn't
   running), a step of it a round; 0 when there is no file */
static void file_line(const char *addr, const char *name, void *ud)
{
	ENTRY *e = find(addr);
	if (e && e->state == E_QUEUED) {
		e->state = E_KNOWN;
		copy(e->name, name, PEOPLE_NAME);
		*(int *)ud = 1;
	}
}

static int from_file(unsigned long now)
{
	int i, force = 0, got = 0, r, left = 0;
	if (!file_busy())	/* a reading begins: who it is for */
		for (i = 0; i < ncache; i++) {
			cache[i].inscan = cache[i].state == E_QUEUED;
			if (cache[i].inscan && !cache[i].seen)
				force = 1;	/* new: the file is read even if unchanged */
		}
	r = people_scan_hook(file_line, &got, force);
	if (r < 0)
		return 0;
	for (i = 0; i < ncache; i++)
		if (cache[i].state == E_QUEUED) {
			/* the file read to its end, or found unchanged: who it was
			   read for isn't in it; who came since waits for a reading
			   from the start */
			if (r != 2 && cache[i].inscan) {
				cache[i].state = E_ABSENT;
				cache[i].lost = 0;
				cache[i].seen = 1;
				cache[i].at = now;
				got = 1;
			} else
				left = 1;
		}
	if (r == 2 && !left)
		people_scan_hook(0, 0, -1);	/* all found: the rest isn't read */
	if (got && on_names)
		on_names();
	return 1;
}

static void ask(void)
{
	char q[CMD_MAX + 1];
	short id;
	int i, any = 0, absent = 0;
	unsigned long now = tos_ms();
	for (i = 0; i < ncache; i++) {
		ENTRY *e = &cache[i];
		if (e->state == E_ASKED && now - e->at >= WAIT_ANSWER) {
			e->state = E_ABSENT;	/* no answer: show the address */
			e->lost = 1;
			e->at = now;
			absent = 1;
		} else if (e->state == E_ABSENT && now - e->at >= (e->lost ? WAIT_LOST : WAIT_ABSENT))
			e->state = E_QUEUED;
		if (e->state == E_QUEUED)
			any = 1;
	}
	if (absent && on_names)
		on_names();
	if (!any || !av_people_ready())
		return;
	id = econtact();
	if (id < 0) {
		if (people_scan_hook && from_file(now))
			return;
		for (i = 0; i < ncache; i++)
			if (cache[i].state == E_QUEUED) {
				cache[i].state = E_ABSENT;
				cache[i].lost = 0;
				cache[i].at = now;
			}
		if (on_names)
			on_names();
		return;
	}
	strcpy(q, "names:");
	strcat(q, av_name());
	for (i = 0; i < ncache; i++) {
		ENTRY *e = &cache[i];
		long l = (long)strlen(q), a = (long)strlen(e->addr);
		if (e->state != E_QUEUED || l + 1 + a > CMD_MAX)
			continue;
		q[l] = ' ';
		memcpy(q + l + 1, e->addr, a + 1);
		e->state = E_ASKED;
		e->at = now;
	}
	if (av_people(id, q) != 1)
		for (i = 0; i < ncache; i++)
			if (cache[i].state == E_ASKED && cache[i].at == now)
				cache[i].state = E_QUEUED;
}

void people_poll(void)
{
	if (send_outbox())
		return;
	if (pick_wanted && av_people_ready()) {
		short id = econtact();
		if (id < 0) {
			pick_open = pick_wanted = 0;
			if (on_picked)
				on_picked(0, 0);
		} else if (av_people(id, pick_text) == 1)
			pick_wanted = 0;
		return;
	}
	ask();
}
