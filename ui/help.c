/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
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

/* 1 where the AES runs several programs at once (MiNT with XaAES, N.AES
   or MyAES; MagiC): there a program can be started beside this one */
static int multitasking(void)
{
	return aes_count() != 1 || get_cookie(0x4D616758L) != 0;	/* MagX */
}

/* EHYPVIEW.PRG started beside us with args, where no viewer runs and the
   AES can (Erez, 25 Oct 2026): its folder is FOLDER= in the boot drive's
   EHYPVIEW.INF, which the program writes there itself. 1 when the AES
   took it */
static int start_viewer(const char *args)
{
	static char prg[200];
	static char tail[128];
	char inf[16] = "C:\\EHYPVIEW.INF", buf[256], line[200];
	long fh, n, i;
	short k = 0, found = 0;
	inf[0] = (char)('A' + boot_drive());
	fh = Fopen(inf, 0);
	if (fh < 0)
		return 0;
	/* FOLDER= at a line's start, the file read a piece at a time */
	while (!found && (n = Fread((short)fh, sizeof(buf), buf)) > 0)
		for (i = 0; i < n && !found; i++) {
			char c = buf[i];
			if (c == '\r' || c == '\n') {
				line[k] = 0;
				if (!strncmp(line, "FOLDER=", 7) && line[7])
					found = 1;
				k = 0;
			} else if (k < (short)sizeof(line) - 1) {
				line[k++] = c;
			}
		}
	if (!found && k) {
		line[k] = 0;
		found = !strncmp(line, "FOLDER=", 7) && line[7];
	}
	Fclose((short)fh);
	if (!found)
		return 0;
	k = (short)strlen(line);
	while (k > 7 && line[k - 1] == ' ')
		line[--k] = 0;
	prg[0] = 0;
	add(prg, sizeof(prg), line + 7);
	if (prg[strlen(prg) - 1] != '\\')
		add(prg, sizeof(prg), "\\");
	add(prg, sizeof(prg), "EHYPVIEW.PRG");
	if (Fattrib(prg, 0, 0) < 0)
		return 0;
	/* the command line: its length, then the text (as Pexec's) */
	k = (short)strlen(args);
	if (k > (short)sizeof(tail) - 2)
		k = (short)sizeof(tail) - 2;
	tail[0] = (char)k;
	memcpy(tail + 1, args, k);
	tail[k + 1] = 0;
	/* a GEM program, at once and beside us: MagiC's SHW_PARALLEL is 100,
	   MultiTOS's and XaAES's 1 */
	return shel_write(1, 1, get_cookie(0x4D616758L) ? 100 : 1, prg, tail) > 0;
}

int help_show(const char *dir, const char *hyp, const char *page)
{
	static const char *const viewers[] = { "EHYPVIEW", "ST-GUIDE", "HYPVIEW ", "HYP_VIEW" };
	short id = -1, i;
	long n = (long)sizeof(local_help);
	for (i = 0; i < 4 && id < 0; i++)
		id = appl_find(viewers[i]);
	if (id < 0 && !multitasking())
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
	if (id < 0)
		return start_viewer(help_buf);
	send(id, help_buf);
	return 1;
}

int help_key(const char *dir, const char *hyp, const char *page, const char *own)
{
	if (help_show(dir, hyp, page))
		return 0;
	if (multitasking()) {
		/* EHYPVIEW.PRG would have been started: it isn't known yet */
		static const char m[] = "[1][No help viewer found.|Install EHypView and run it|once: then it starts by|itself when needed.]";
		if (!own) {
			alert(1, "%s[ OK ]", m);
			return 0;
		}
		return alert(2, "%s[%s|OK]", m, own) == 1;
	}
	if (!own) {
		alert(1, "[1][No help viewer is running:|install EHypView or ST-Guide|as an accessory.][ OK ]");
		return 0;
	}
	return alert(2, "[1][No help viewer is running:|install EHypView or ST-Guide|as an accessory.][%s|OK]",
		     own) == 1;
}
