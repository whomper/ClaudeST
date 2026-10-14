/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * help.c - the Help key (see help.h).
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "help.h"
#include "av.h"
#include "alert.h"

#define VA_START 0x4711

static char local_help[256];
static char *help_buf = local_help;

/* the name stays where the viewer may read it (av.c) */
void help_init(void)
{
	static int done;
	if (done)
		return;
	done = 1;
	help_buf = av_shared(local_help, sizeof(local_help));
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

int help_key(const char *dir, const char *hyp, const char *page, const char *own)
{
	if (help_show(dir, hyp, page))
		return 0;
	if (!own) {
		alert(1, "[1][No help viewer is running:|install EHypView or ST-Guide|as an accessory.][ OK ]");
		return 0;
	}
	return alert(2, "[1][No help viewer is running:|install EHypView or ST-Guide|as an accessory.][%s|OK]",
		     own) == 1;
}
