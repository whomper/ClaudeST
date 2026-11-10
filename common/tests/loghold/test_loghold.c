/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_loghold.c - conn_log_hold (the guidelines, 6.5): a job's
 * milestones held and written in a few opens of the log; with
 * conn_log_hold_bytes 0 the 4 KB buffer. The 4 KB buffer is taken from the
 * heap at the first line and given back when the log is switched off
 * (no BSS for an app that logs nothing); with no heap the lines go out one
 * at a time.
 *
 * The log's cap (conn_log_max, roll-over to NAME.OLD): the files are real
 * ones in build/t; the size comes from the open handle (conn_log_size, as
 * an app sets pf_handle_size) or, without the hook, from one look a run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "plat.h"
#include "conn.h"

static int opens, big_mallocs, fails, live, no_heap, clock_ok;
static long logged;
void *__real_malloc(size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n)
{
	if (n >= 4096) {
		if (no_heap)
			return 0;
		big_mallocs++;
		live++;
	}
	return __real_malloc(n);
}
void __wrap_free(void *p)
{
	/* only the log's blocks are 4 KB or more here: count by size */
	if (p && malloc_usable_size(p) >= 4096)
		live--;
	__real_free(p);
}

int pf_open(const char *path, int mode)
{
	opens++;
	return open(path, mode == PF_APPEND ? O_WRONLY | O_CREAT | O_APPEND : O_RDONLY, 0644);
}
long pf_write(int h, const void *buf, long n) { logged += n; return write(h, buf, n); }
void pf_close(int h) { close(h); }
long pf_seek(int h, long off) { return lseek(h, off, SEEK_SET); }
static int looks;
long pf_size(const char *path)
{
	struct stat st;
	looks++;
	return stat(path, &st) ? -1 : (long)st.st_size;
}
int pf_remove(const char *path) { return unlink(path); }
int pf_rename(const char *from, const char *to) { return rename(from, to); }
/* an app's pf_handle_size: the size, the place left at the start */
static long handle_size(int h)
{
	off_t n = lseek(h, 0, SEEK_END);
	if (n < 0 || lseek(h, 0, SEEK_SET) < 0)
		return -1;
	return (long)n;
}
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); if (clock_ok) { t->year = 2026; t->mon = 11; t->day = 9; t->hour = 14; t->min = 5; t->sec = 31; } }
unsigned long pf_ms(void) { return 0; }
void (*pf_idle)(void);
int net_open(const char *host, unsigned short port, char *err, int errlen) { return -1; }
long net_write(int h, const void *buf, long n) { return -1; }
long net_read(int h, void *buf, long max) { return -1; }
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

static void fresh(const char *dir)
{
	char cmd[120];
	int r;
	snprintf(cmd, sizeof(cmd), "rm -rf build/t && mkdir -p %s", dir);
	r = system(cmd);
	if (r)
		exit(2);
}

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* a job of 120 milestones of about 110 bytes (13 KB): opens of the log */
static int job(void)
{
	char line[91];
	int i;
	memset(line, 'x', 90);
	line[90] = 0;
	opens = big_mallocs = 0;
	logged = 0;
	conn_log_hold(1);
	for (i = 0; i < 120; i++)
		conn_log("HTTP", " -- ", line, 90);
	conn_log_hold(0);
	return opens;
}

static long fsize(const char *p)
{
	struct stat st;
	return stat(p, &st) ? -1 : (long)st.st_size;
}

static char *slurp(const char *p, long *n)
{
	FILE *f = fopen(p, "rb");
	char *b;
	if (!f)
		return NULL;
	b = malloc(100000);
	*n = (long)fread(b, 1, 99999, f);
	b[*n] = 0;
	fclose(f);
	return b;
}

/* lines of 100 bytes ("<n> xxxx...\r\n" after the stamp): n lines, one
   milestone each, written at once when not held */
static void lines(int first, int count)
{
	char text[64];
	int i;
	for (i = 0; i < count; i++) {
		snprintf(text, sizeof(text), "line %04d", first + i);
		conn_log("T", " -- ", text, (long)strlen(text));
	}
}

static void cap_run(const char *label, long (*hook)(int))
{
	char msg[160], *b;
	long n, o;
	int before;
	conn_log_size = hook;
	conn_logfile[0] = 0;
	conn_log("T", " -- ", "off", 3);
	fresh("build/t");
	strcpy(conn_logfile, "build/t/EMAIL.LOG");
	conn_log_hold_bytes = 16384;
	conn_log_max = 2000;		/* a line is 28 bytes */

	looks = 0;
	lines(0, 70);			/* 1820 bytes: under the cap */
	snprintf(msg, sizeof(msg), "%s: under the cap, no .OLD (%ld, %ld)", label, fsize("build/t/EMAIL.LOG"), fsize("build/t/EMAIL.OLD"));
	check(fsize("build/t/EMAIL.LOG") == 70 * 28 && fsize("build/t/EMAIL.OLD") < 0, msg);
	snprintf(msg, sizeof(msg), "%s: looks at the folder %d (%s)", label, looks, hook ? "none: the handle says" : "one a run");
	check(hook ? looks == 0 : looks == 1, msg);

	before = opens;
	lines(70, 10);			/* 80 lines, 2080 bytes: over */
	snprintf(msg, sizeof(msg), "%s: rolled over: .OLD holds the first lines, the new log the rest (%ld, %ld)", label, fsize("build/t/EMAIL.OLD"), fsize("build/t/EMAIL.LOG"));
	b = slurp("build/t/EMAIL.OLD", &o);
	check(b && fsize("build/t/EMAIL.OLD") > 1900 && fsize("build/t/EMAIL.OLD") <= 2000 && strstr(b, "line 0000") &&
	      fsize("build/t/EMAIL.LOG") > 0 && fsize("build/t/EMAIL.LOG") < 2000, msg);
	free(b);
	b = slurp("build/t/EMAIL.LOG", &n);
	check(b && strstr(b, "line 0079") && !strstr(b, "line 0000"), "the new log ends with the last line and has none of the first");
	free(b);
	snprintf(msg, sizeof(msg), "%s: 10 flushes and one roll-over: 11 opens (%d)", label, opens - before);
	check(opens - before == 11, msg);

	/* the next roll-over replaces the .OLD */
	lines(80, 160);
	b = slurp("build/t/EMAIL.OLD", &o);
	snprintf(msg, sizeof(msg), "%s: .OLD replaced by a later one (%ld bytes, first line 0%s)", label, o, b ? "" : "?");
	check(b && !strstr(b, "line 0000") && strstr(b, "line 02") && o <= 2000, msg);
	free(b);
	snprintf(msg, sizeof(msg), "%s: the log never over the cap (%ld)", label, fsize("build/t/EMAIL.LOG"));
	check(fsize("build/t/EMAIL.LOG") <= 2000, msg);

	/* hold buffer unaffected: a held job of 120 milestones is one open, as before, while under the cap */
	conn_log_max = 100000;
	opens = 0;
	{
		char line[91];
		int i;
		memset(line, 'x', 90);
		line[90] = 0;
		conn_log_hold(1);
		for (i = 0; i < 120; i++)
			conn_log("HTTP", " -- ", line, 90);
		conn_log_hold(0);
	}
	snprintf(msg, sizeof(msg), "%s: held job under the cap: 1 open (%d)", label, opens);
	check(opens == 1, msg);

	/* a held job over the cap: written in one piece, and the old log rolls */
	conn_log_max = 20000;
	{
		long was = fsize("build/t/EMAIL.LOG");
		char line[91];
		int i;
		memset(line, 'x', 90);
		line[90] = 0;
		opens = 0;
		conn_log_hold(1);
		for (i = 0; i < 120; i++)
			conn_log("HTTP", " -- ", line, 90);
		conn_log_hold(0);
		snprintf(msg, sizeof(msg), "%s: held job pushing the log over: 2 opens, the 13 KB kept whole (was %ld, now %ld)", label, was, fsize("build/t/EMAIL.LOG"));
		check(was + 13000 > 20000 ? (opens == 2 && fsize("build/t/EMAIL.LOG") > 12000 && fsize("build/t/EMAIL.OLD") == was) : opens == 1, msg);
	}

	/* conn_log_max 0: no cap */
	conn_log_max = 0;
	conn_log_append("crash report\r\n", 14);
	check(fsize("build/t/EMAIL.LOG") > 12000, "conn_log_max 0: no cap, no look");
	conn_log_max = 2000;
}

static void cap_tests(void)
{
	char msg[100], *b;
	long n;

	cap_run("size from the handle", handle_size);
	cap_run("size by one look", NULL);

	/* names: the extension is replaced, a dot in a folder is not one */
	conn_log_size = handle_size;
	conn_log_max = 100;
	conn_logfile[0] = 0;
	conn_log("T", " -- ", "off", 3);
	fresh("build/t/a.b");
	strcpy(conn_logfile, "build/t/a.b/NOEXT");
	lines(0, 10);
	check(fsize("build/t/a.b/NOEXT.OLD") > 0, "a name with no extension: NAME.OLD; a dot in the folder is not an extension");

	/* a report appended goes through the same cap */
	conn_log_append("0123456789012345678901234567890123456789012345678901234567890123456789\r\n", 72);
	check(fsize("build/t/a.b/NOEXT") <= 100, "conn_log_append is capped too");

	/* the start line */
	strcpy(conn_logfile, "build/t/START.LOG");
	conn_log_max = 100000;
	clock_ok = 1;
	conn_log_start("EMail 1.23");
	b = slurp("build/t/START.LOG", &n);
	snprintf(msg, sizeof(msg), "conn_log_start: app, version, date and time: %s", b ? b : "(none)");
	check(b && strstr(b, "EMail 1.23, 2026-11-09 14:05:31\r\n"), msg);
	free(b);
	clock_ok = 0;
	strcpy(conn_logfile, "build/t/START2.LOG");
	conn_log_start("EMail 1.23");
	b = slurp("build/t/START2.LOG", &n);
	check(b && strstr(b, "EMail 1.23\r\n") && !strstr(b, "2026"), "conn_log_start without a clock: no date");
	free(b);
	conn_logfile[0] = 0;
	conn_log("T", " -- ", "off", 3);
	conn_log_size = NULL;
	conn_log_max = 256L * 1024;
}

int main(void)
{
	int n;
	char msg[100];
	fresh("build/t");
	strcpy(conn_logfile, "build/t/LOG.TXT");

	n = job();
	snprintf(msg, sizeof(msg), "held in 16 KB: 1 open for the job (%d), 16 KB from the heap (%d)", n, big_mallocs);
	check(n == 1 && big_mallocs == 1 && logged > 12000, msg);

	conn_log_hold_bytes = 0;
	n = job();
	snprintf(msg, sizeof(msg), "conn_log_hold_bytes 0: the 4 KB buffer, 4 opens (%d), one 4 KB block (%d)", n, big_mallocs);
	check(n == 4 && big_mallocs == 1 && logged > 12000, msg);

	conn_log_hold_bytes = 32768;
	n = job();
	snprintf(msg, sizeof(msg), "conn_log_hold_bytes 32 KB: 1 open (%d)", n);
	check(n == 1 && big_mallocs == 1, msg);

	conn_log_hold_bytes = 16384;
	opens = 0;
	conn_log("HTTP", " -- ", "not held", 8);
	check(opens == 1, "not held: a milestone written at once");

	/* switched off: the buffer goes back, and nothing is taken again */
	conn_logfile[0] = 0;
	conn_log("HTTP", " -- ", "off", 3);
	check(live == 0, "log switched off: the 4 KB buffer is given back");
	big_mallocs = 0;
	conn_log("HTTP", " -- ", "off", 3);
	check(big_mallocs == 0, "log off: nothing taken from the heap");

	/* no heap: lines still reach the file, one open each */
	strcpy(conn_logfile, "build/t/LOG.TXT");
	no_heap = 1;
	opens = 0;
	logged = 0;
	conn_log("HTTP", " -- ", "no heap", 7);
	check(opens > 0 && logged > 7, "no 4 KB in the heap: the line is still written");
	no_heap = 0;
	conn_logfile[0] = 0;
	conn_log("HTTP", " -- ", "off", 3);

	cap_tests();
	printf(fails ? "%d failed\n" : "all passed\n", fails);
	return fails != 0;
}
