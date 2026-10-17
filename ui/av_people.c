/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * av_people.c - PEOPLE out (see av.h): one at a time, in memory EContact
 * may read, answered with AV_STARTED. Apart from av.c so that an app
 * that never asks (EFTP, EHypView, ClaudeST) doesn't carry its buffer.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "av.h"

#define PEOPLE_WAIT	3000UL	/* ms for AV_STARTED before a PEOPLE counts as lost */

static char local_people[1024];
static char *people_buf;
static int people_out;			/* a PEOPLE sent, AV_STARTED not yet back */
static unsigned long people_at;		/* when it went (tos_ms) */
static long people_lost;

static int started(const char *p)
{
	if (!people_buf || p != people_buf)
		return 0;
	people_out = 0;			/* our PEOPLE arrived: the next may go */
	return 1;
}

int av_people_ready(void)
{
	if (people_out && tos_ms() - people_at >= PEOPLE_WAIT) {
		people_out = 0;		/* no answer in time: lost */
		people_lost++;
	}
	return !people_out;
}

long av_people_lost(void)
{
	return people_lost;
}

int av_people(short id, const char *text)
{
	long l;
	if (id < 0)
		return -1;
	if (!av_people_ready())
		return 0;
	if (!people_buf) {
		people_buf = av_shared(local_people, sizeof(local_people));
		av_people_started = started;
	}
	l = (long)strlen(text);
	if (l > (long)sizeof(local_people) - 1)
		l = sizeof(local_people) - 1;
	memcpy(people_buf, text, l);
	people_buf[l] = 0;
	av_post(id, AV_PEOPLE_MSG, 0, people_buf);
	people_out = 1;
	people_at = tos_ms();
	return 1;
}
