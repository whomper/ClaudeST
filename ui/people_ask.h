/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people_ask.h - asking EContact about people (the guidelines, 4.1): the
 * names behind e-mail addresses, and people picked in EContact's window.
 * The questions and answers travel in PEOPLE messages (av.c); this holds
 * the asker's side and the string format both sides use.
 *
 * An asker (EMail, ECal):
 *   people_init(64, 2048, names_came, picked);   once at start (an accessory:
 *                                                 before its heap is fixed)
 *   people_name("a@b")     the name at once: "Grace Baker", "" when EContact
 *                          doesn't know the address, 0 while it is being
 *                          asked (the question goes out with people_poll)
 *   people_pick("gra")     EContact's window, to choose people; never while
 *                          we hold the screen (a dialog's BEG_UPDATE)
 *   in every event loop, modal ones included:
 *     case AV_PEOPLE: people_message(s, msg[1]);
 *     each turn:      people_poll();
 *   names_came()           names arrived or changed: redraw what shows them
 *   picked(list, n)        the people chosen (n = 0: cancelled)
 *
 * EContact (answering) reads a question with people_question(), adds the
 * answer's lines with people_answer() and people_poll() sends them, cut
 * into whole commands of at most 1 KB, one at a time as AV_STARTED comes
 * back.
 */
#ifndef PEOPLE_ASK_H
#define PEOPLE_ASK_H

#define PEOPLE_ADDR	64		/* longest address kept, with its 0 */
#define PEOPLE_NAME	64		/* longest name: 63 bytes of Atari text */

typedef struct {
	char addr[PEOPLE_ADDR];
	char name[PEOPLE_NAME];
} PERSON;

int people_init(int cache, long outbox, void (*names_came)(void),
		void (*picked)(const PERSON *list, int n));
const char *people_name(const char *addr);
int people_pick(const char *partial);	/* 0: one is open already, or no EContact */
int people_picking(void);		/* 1 while a pick waits for its answer */
int people_message(const char *s, short from);	/* 1 when it was people_ask's */
void people_poll(void);
void people_forget(void);		/* AC_CLOSE: questions without answers go */

/* EContact's side */
enum { PQ_NONE, PQ_NAMES, PQ_PICK };
/* "names:EMAIL a@b c@d" -> PQ_NAMES, asker "EMAIL", *rest "a@b c@d" */
int people_question(const char *s, char *asker, int asker_size, const char **rest);
/* the next address off a names: question's rest; 0 when none is left */
const char *people_next(const char *p, char *addr, int size);
/* an answer for the program `to`: start with people_answer(to, "names:", 0, 0),
   add a line per person, end with people_answer(to, 0, 0, 0) */
int people_answer(short to, const char *kind, const char *addr, const char *name);

/* host tests: the string format alone */
int people_line(char *out, long size, const char *addr, const char *name);
const char *people_read_line(const char *p, char *addr, char *name);

#endif
