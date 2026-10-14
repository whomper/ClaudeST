/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * help.c - the Help key and VA_START to other programs (see help.h).
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "help.h"

#define VA_START 0x4711

static char local_help[256], local_av[1024];
static char *help_buf = local_help, *av_buf = local_av;

/* Memory another program reads, kept while we run. An accessory under
   plain TOS has no memory of its own after the start: what Malloc or
   Mxalloc give it then belongs to the program running at the moment and
   goes when that one ends. Nothing is protected there, so a static
   buffer does. Under MiNT or MagiC the accessory is a process like the
   others: global memory, which memory protection lets others read
   (Mxalloc mode 0x2B; 0x20 alone without 0x08 isn't heeded). On TOS 1.x
   Mxalloc doesn't exist (an error): the static buffer again. */
static char *shared(char *local, long size)
{
	long m;
	if (!get_cookie(0x4D694E54L) && !get_cookie(0x4D616758L))	/* MiNT, MagX */
		return local;
	m = (long)Mxalloc(size, 0x2B);	/* global, TT RAM preferred */
	return m > 0 ? (char *)m : local;
}

void help_init(void)
{
	static int done;
	if (done)
		return;
	done = 1;
	help_buf = shared(local_help, sizeof(local_help));
	av_buf = shared(local_av, sizeof(local_av));
}

static void send(short id, const char *s)
{
	short msg[8];
	memset(msg, 0, sizeof(msg));
	msg[0] = VA_START;
	msg[1] = appl_id();
	msg[3] = (short)((unsigned long)s >> 16);
	msg[4] = (short)(unsigned long)s;
	appl_write(id, 16, msg);
}

/* s, cut to fit n bytes, after what's in b already */
static void add(char *b, long n, const char *s)
{
	long l = (long)strlen(b);
	while (*s && l < n - 1)
		b[l++] = *s++;
	b[l] = 0;
}

int help_show(const char *dir, const char *hyp, const char *page)
{
	static const char *const viewers[] = { "EHYPVIEW", "ST-GUIDE", "HYPVIEW ", "HYP_VIEW" };
	short id = -1, i;
	long n = (long)sizeof(local_help);
	for (i = 0; i < 4 && id < 0; i++)
		id = appl_find(viewers[i]);
	if (id < 0)
		return 0;
	help_init();
	help_buf[0] = 0;
	if (dir && dir[0]) {
		/* the copy next to the program, when it is there */
		add(help_buf, n, dir);
		if (help_buf[strlen(help_buf) - 1] != '\\')
			add(help_buf, n, "\\");
		add(help_buf, n, hyp);
		if (Fattrib(help_buf, 0, 0) < 0)
			help_buf[0] = 0;
	}
	if (!help_buf[0]) {
		add(help_buf, n, "*:\\");
		add(help_buf, n, hyp);
	}
	if (page && page[0]) {
		add(help_buf, n, " ");
		add(help_buf, n, page);
	}
	send(id, help_buf);
	return 1;
}

int av_send(const char *name, const char *text)
{
	short id = appl_find(name);
	if (id < 0)
		return 0;
	help_init();
	av_buf[0] = 0;
	add(av_buf, sizeof(local_av), text);
	send(id, av_buf);
	return 1;
}
