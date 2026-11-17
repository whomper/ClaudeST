/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FBCHECK.PRG - what a family program sees on the FireBee stand-in
 * (machines.py's "FireBee"), for run.py. On the console (NatFeats):
 * every cookie and where the jar is; "FBCHECK flushed" once ovl_flush()
 * (src/ovl.c) is back, its _CF_ path taken when the cookie is there;
 * then the files TLS.OVL's loader (atari/tls_ovl.c, tls_ovl_load) and
 * PIC.OVL's (atari/pic_ovl.c, pic_ovl_step) ask to open, none of them
 * there: on a FireBee tlsfb.ovl and no pic.ovl. "FBCHECK done" at the
 * end.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "plat.h"
#include "conn.h"
#include "tls.h"
#include "tlsovl.h"
#include "ovl.h"
#include "pic_ovl.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

static void say(const char *s)
{
	nf_debug(s);
}

/* the files asked for: none is there */
int pf_open(const char *path, int mode)
{
	char b[240];
	snprintf(b, sizeof(b), "FBCHECK open %s\n", path);
	say(b);
	(void)mode;
	return -33;
}
long pf_read(int h, void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long pf_write(int h, const void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
void pf_close(int h) { (void)h; }
long pf_seek(int h, long off) { (void)h; (void)off; return -1; }
long pf_size(const char *path) { (void)path; return -1; }
int pf_remove(const char *path) { (void)path; return -1; }
char *pf_load(const char *path, long *len) { (void)path; *len = 0; return 0; }
int pf_save(const char *path, const void *data, long len) { (void)path; (void)data; (void)len; return 0; }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return tos_ms(); }
void pf_entropy(unsigned char *buf, int n) { memset(buf, 0, (size_t)n); }
long net_write(int h, const void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long net_read(int h, void *buf, long max) { (void)h; (void)buf; (void)max; return -1; }
void (*pf_idle)(void);
void conn_log(const char *who, const char *dir, const char *text, long n)
{
	char b[200];
	snprintf(b, sizeof(b), "FBCHECK log %s%s%.*s\n", who, dir, (int)n, text);
	say(b);
}

#pragma GCC diagnostic ignored "-Warray-bounds"	/* the cookie jar's fixed address */
static long jar_at;
static long jar[2 * 160];
static long jar_read(void)
{
	long *j = *(long **)0x5a0, i;
	jar_at = (long)j;
	for (i = 0; j && i < 159 && j[2 * i]; i++) {
		jar[2 * i] = j[2 * i];
		jar[2 * i + 1] = j[2 * i + 1];
	}
	jar[2 * i] = 0;
	jar[2 * i + 1] = j ? j[2 * i + 1] : 0;
	return 0;
}

int main(void)
{
	static PIC_PAL pal;
	char b[200], err[120];
	int i, fill = 0;
	Supexec(jar_read);
	for (i = 0; jar[2 * i]; i++) {
		unsigned long id = (unsigned long)jar[2 * i];
		if ((id & 0xFFFFFF00UL) == 0x46494C00UL) {	/* FILLJAR.PRG's */
			fill++;
			continue;
		}
		snprintf(b, sizeof(b), "FBCHECK cookie %c%c%c%c %08lx\n", (char)(id >> 24), (char)(id >> 16),
			 (char)(id >> 8), (char)id, (unsigned long)jar[2 * i + 1]);
		say(b);
	}
	snprintf(b, sizeof(b), "FBCHECK jar at %08lx: %d cookies, %d of FILLJAR's, %ld slots\n",
		 jar_at, i, fill, jar[2 * i + 1]);
	say(b);
	ovl_flush();
	say("FBCHECK flushed\n");
	str_copy(tls_ovl_path, "C:\\FBT\\TLS.OVL", sizeof(tls_ovl_path));
	err[0] = 0;
	snprintf(b, sizeof(b), "FBCHECK tls_ovl_load %d: %s\n", tls_ovl_load(err, sizeof(err)), err);
	say(b);
	snprintf(b, sizeof(b), "FBCHECK pic_ovl_step %d\n", pic_ovl_step(&pal, "", 0));
	say(b);
	say("FBCHECK done\n");
	return 0;
}
