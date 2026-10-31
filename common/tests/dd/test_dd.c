/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Drops (ui/dd.c) and the clipboard (ui/clip.c) on the build machine.
 * The drag and drop pipe stands in as two buffers: what the sender sends
 * (headers and data, in this machine's byte order) and what the receiver
 * answers; the clipboard's files are kept in memory. Not a position-
 * independent program, so a static buffer has the 32-bit address a
 * message carries (VA_DRAGACCWIND).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "dd.h"
#include "clip.h"

static int failed, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

/* ---- the AES ---- */
static short sent[8], sent_to = -1, broadcast[8], broadcasts, getinfo10 = 7;
static char scrap_dir[64] = "C:\\CLIPBRD\\", scrap_written[64];
short appl_id(void) { return 3; }
static short aes_ver = 0x0410;
short aes_version(void) { return aes_ver; }
short appl_write(short id, short len, const void *buf) { memcpy(sent, buf, len); sent_to = id; return 1; }
short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4)
{
	*o1 = type == 10 ? getinfo10 : 0;
	*o2 = *o3 = *o4 = 0;
	return 1;
}
short scrp_read(char *path) { strcpy(path, scrap_dir); return 1; }
short scrp_write(const char *path) { strcpy(scrap_written, path); strcpy(scrap_dir, path); return 1; }
short shel_write(short mode, short wisgr, short wiscr, const void *cmd, const void *tail)
{
	(void)wisgr; (void)wiscr; (void)tail;
	if (mode == 7) {
		memcpy(broadcast, cmd, 16);
		broadcasts++;
	}
	return 1;
}
long get_cookie(long id) { (void)id; return 0; }
static short boot = 2;			/* C: */
short boot_drive(void) { return boot; }
short Dgetdrv(void) { return 2; }

/* ---- the pipe ---- */
#define PIPE 100
static char opened[40], in[4096], out[256];
static long in_n, in_at, out_n;
static long sig_now = 0, sig_calls;

static void pipe_reset(void)
{
	in_n = in_at = out_n = 0;
	opened[0] = 0;
}

/* a header as the sender writes it: its length, kind, size, then a name */
static void send_header(const char *kind, long size, const char *name)
{
	short hdr = (short)(8 + strlen(name) + 1);
	memcpy(in + in_n, &hdr, 2);
	memcpy(in + in_n + 2, kind, 4);
	memcpy(in + in_n + 6, &size, 4);
	strcpy(in + in_n + 10, name);
	in_n += 10 + (long)strlen(name) + 1;
}

static void send_data(const char *s, long n)
{
	memcpy(in + in_n, s, n);
	in_n += n;
}

long Psignal(short sig, long handler)
{
	long old = sig_now;
	(void)sig;
	sig_now = handler;
	sig_calls++;
	return old;
}

/* ---- the files ---- */
#define NFILE 8
static struct { char name[64]; char data[200000]; long len; int used; } files[NFILE];
static int writing = -1, reading = -1, opens, writes;
static long read_at;
static DTA *dta;
static int search_at;
static char search_pat[64];

static int find(const char *name)
{
	int i;
	for (i = 0; i < NFILE; i++)
		if (files[i].used && !strcmp(files[i].name, name))
			return i;
	return -1;
}

static void put_file(const char *name, const char *data)
{
	int i = find(name);
	if (i < 0)
		for (i = 0; files[i].used; i++)
			;
	files[i].used = 1;
	strcpy(files[i].name, name);
	strcpy(files[i].data, data);
	files[i].len = (long)strlen(data);
}

long Fopen(const char *name, short mode)
{
	if (!strncmp(name, "U:\\PIPE\\", 8)) {
		strcpy(opened, name);
		return PIPE;
	}
	if ((reading = find(name)) < 0)
		return -33;
	read_at = 0;
	if (mode == 1)
		writing = reading;
	opens++;
	return 10 + reading;
}

long Fseek(long off, short fd, short mode)
{
	(void)fd;
	return mode == 2 ? files[writing].len + off : off;	/* writes go at the end */
}

long Fcreate(const char *name, short attr)
{
	(void)attr;
	opens++;
	put_file(name, "");
	writing = find(name);
	return 10 + writing;
}

long Fread(short fd, long n, void *buf)
{
	if (fd == PIPE) {
		if (n > in_n - in_at)
			n = in_n - in_at;
		memcpy(buf, in + in_at, n);
		in_at += n;
		return n;
	}
	if (n > files[reading].len - read_at)
		n = files[reading].len - read_at;
	memcpy(buf, files[reading].data + read_at, n);
	read_at += n;
	return n;
}

long Fwrite(short fd, long n, const void *buf)
{
	if (fd == PIPE) {
		memcpy(out + out_n, buf, n);
		out_n += n;
		return n;
	}
	writes++;
	memcpy(files[writing].data + files[writing].len, buf, n);
	files[writing].len += n;
	files[writing].data[files[writing].len] = 0;
	return n;
}

long Fclose(short fd) { (void)fd; return 0; }
long Fdelete(const char *name) { int i = find(name); if (i >= 0) files[i].used = 0; return i >= 0 ? 0 : -33; }
/* folders: C:\CLIPBRD made on demand; A: is a write-protected floppy */
static char dirs[4][64];
static int ndirs;
long Dcreate(const char *path)
{
	if (path[0] == 'A')
		return -36;
	strcpy(dirs[ndirs++ & 3], path);
	return 0;
}
long Fattrib(const char *name, short wflag, short attr)
{
	int i;
	(void)wflag; (void)attr;
	for (i = 0; i < 4; i++)
		if (!strcmp(dirs[i], name))
			return 0x10;
	return -33;
}
long Fsetdta(DTA *d) { dta = d; return 0; }
void *Fgetdta(void) { return dta; }

static long next_match(void)
{
	char *star = strchr(search_pat, '*');
	size_t pre = star ? (size_t)(star - search_pat) : strlen(search_pat);
	for (; search_at < NFILE; search_at++)
		if (files[search_at].used && !strncmp(files[search_at].name, search_pat, pre)) {
			const char *base = strrchr(files[search_at].name, '\\') + 1;
			strcpy(dta->d_fname, base);
			search_at++;
			return 0;
		}
	return -49;
}

long Fsfirst(const char *pat, short attr)
{
	(void)attr;
	strcpy(search_pat, pat);
	search_at = 0;
	return next_match();
}

long Fsnext(void) { return next_match(); }

/* ---- the checks ---- */
static int drop(int want, DROP *d)
{
	short msg[8] = { 63, 5, 0, 7, 100, 120, 4, ('A' << 8) | 'B' };
	return dd_message(msg, want, d);
}

int main(void)
{
	DROP d;
	int r;
	char buf[100];
	long n;

	/* files offered first, and taken */
	pipe_reset();
	send_header("ARGS", 25, "Files");
	send_data("C:\\A.ICS 'C:\\MY FILE.ICS'", 25);
	r = drop(DD_WANT_FILES | DD_WANT_TEXT, &d);
	check(r == DD_FILES && !strcmp(d.data, "C:\\A.ICS 'C:\\MY FILE.ICS'") && d.len == 25,
	      "files dropped: the names as they came");
	check(!strcmp(opened, "U:\\PIPE\\DRAGDROP.AB"), "the pipe named by the message's last word");
	check(out_n == 34 && out[0] == 0 && !memcmp(out + 1, "ARGS.TXT", 8) && out[9] == 0,
	      "we say DD_OK, then what we take: ARGS, .TXT");
	check(out[33] == 0, "DD_OK for the files");
	check(d.win == 7 && d.x == 100 && d.y == 120 && d.kstate == 4, "where, on which window, the shift keys");
	check(sig_calls == 2 && sig_now == 0, "SIGPIPE ignored meanwhile, then as it was");
	dd_done(&d);
	check(d.data == 0, "dd_done lets the data go");

	/* RTF first: another kind, please; then the text */
	pipe_reset();
	send_header(".RTF", 10, "Text");
	send_header(".TXT", 9, "Text");
	send_data("one\r\ntwo\r", 9);
	r = drop(DD_WANT_TEXT, &d);
	check(r == DD_TEXT && !strcmp(d.data, "one\ntwo\n") && d.len == 8, "text: CR LF to '\\n'");
	check(out_n == 35 && !memcmp(out + 1, ".TXT\0\0\0\0", 8) && out[33] == 2 && out[34] == 0,
	      "only .TXT asked for; DD_EXT for RTF, then DD_OK");
	dd_done(&d);

	/* files where only text is wanted: DD_EXT, the sender gives up */
	pipe_reset();
	send_header("ARGS", 8, "");
	r = drop(DD_WANT_TEXT, &d);
	check(r == DD_REFUSED && d.data == 0 && out[33] == 2, "files refused where text is wanted");

	/* too long */
	pipe_reset();
	send_header(".TXT", DD_MAX + 1, "Text");
	r = drop(DD_WANT_TEXT, &d);
	check(r == DD_REFUSED && out[33] == 3, "longer than DD_MAX: DD_LEN");

	/* nothing wanted */
	pipe_reset();
	r = drop(0, &d);
	check(r == DD_REFUSED && out_n == 33 && out[0] == 1, "nothing wanted: DD_NAK at once");

	/* a long name in the header is skipped */
	pipe_reset();
	send_header("ARGS", 5, "A name longer than the sixty-four bytes read at a time, to be skipped");
	send_data("C:\\X", 5);
	r = drop(DD_WANT_FILES, &d);
	check(r == DD_FILES && !strcmp(d.data, "C:\\X"), "a header's long name skipped");
	dd_done(&d);

	/* the desktop's VA_DRAGACCWIND */
	{
		static char names[] = "C:\\PEOPLE.VCF";
		short msg[8] = { 0x4725, 9, 0, 4, 30, 40, 0, 0 };
		msg[6] = (short)((unsigned long)names >> 16);
		msg[7] = (short)(unsigned long)names;
		sent_to = -1;
		r = dd_message(msg, DD_WANT_FILES, &d);
		check(r == DD_FILES && !strcmp(d.data, names), "VA_DRAGACCWIND: the names, copied");
		check(d.win == 4 && d.x == 30 && d.y == 40, "VA_DRAGACCWIND: window and place");
		check(sent_to == 9 && sent[0] == 0x4738 && sent[3] == msg[6] && sent[4] == msg[7],
		      "VA_DRAGACCWIND answered with AV_STARTED, the pointer in words 3 and 4");
		dd_done(&d);
	}
	{
		short msg[8] = { 41, 9, 0, 4, 0, 0, 0, 0 };
		check(dd_message(msg, DD_WANT_FILES, &d) == DD_NONE, "other messages: DD_NONE");
	}

	/* the clipboard */
	put_file("C:\\CLIPBRD\\SCRAP.IMG", "old picture");
	broadcasts = 0;
	check(clip_put("line 1\nline 2", 13) == 1, "clip_put");
	check(find("C:\\CLIPBRD\\SCRAP.IMG") < 0, "the old SCRAP.* gone");
	n = find("C:\\CLIPBRD\\SCRAP.TXT");
	check(n >= 0 && !strcmp(files[n].data, "line 1\r\nline 2"), "SCRAP.TXT with CR LF");
	check(broadcasts == 1 && broadcast[0] == 80 && broadcast[1] == 3 && broadcast[3] == 2,
	      "SC_CHANGED to all, text");
	check(clip_put("a\r\nb", 4) && !strcmp(files[find("C:\\CLIPBRD\\SCRAP.TXT")].data, "a\r\nb"),
	      "CR LF given stays CR LF");
	getinfo10 = 0;
	broadcasts = 0;
	clip_put("x", 1);
	check(broadcasts == 0, "no broadcast where the AES has none");
	getinfo10 = 7;
	aes_ver = 0x0140;
	clip_put("x", 1);
	check(broadcasts == 0, "no appl_getinfo before AES 4.0 (EmuTOS's hangs)");
	put_file("C:\\CLIPBRD\\SCRAP.TXT", "first\r\nsecond\r\n");
	n = clip_get(buf, sizeof(buf));
	check(n == 13 && !strcmp(buf, "first\nsecond\n"), "clip_get: '\\n' at the line ends");
	n = clip_get(buf, 6);
	check(n == 5 && !strcmp(buf, "first"), "clip_get keeps to the buffer");
	put_file("C:\\CLIPBRD\\SCRAP.TXT", "tab\there\r\nnext");
	clip_get(buf, sizeof(buf));
	check(!strcmp(clip_line(buf), "tab here"), "clip_line: the first line, tabs as spaces");
	Fdelete("C:\\CLIPBRD\\SCRAP.TXT");
	check(clip_get(buf, sizeof(buf)) == 0 && buf[0] == 0, "no SCRAP.TXT: nothing");
	strcpy(scrap_dir, "C:\\CLIPBRD\\SCRAP.TXT");
	ndirs = 0;
	memset(dirs, 0, sizeof(dirs));
	check(clip_put("w", 1) && !strcmp(dirs[0], "C:\\CLIPBRD") && find("C:\\CLIPBRD\\SCRAP.TXT") >= 0,
	      "the AES's folder (a file name at its end left out) made when it isn't there");
	scrap_dir[0] = 0;
	clip_put("y", 1);
	check(!strcmp(scrap_written, "C:\\CLIPBRD\\") && find("C:\\CLIPBRD\\SCRAP.TXT") >= 0,
	      "no clipboard yet: C:\\CLIPBRD\\ on the boot drive");
	scrap_dir[0] = 0;
	ndirs = 0;
	memset(dirs, 0, sizeof(dirs));
	boot = 0;
	check(clip_put("z", 1) && !strcmp(scrap_written, "C:\\CLIPBRD\\"),
	      "the boot drive can't be written: \\CLIPBRD\\ on the drive we run from");

	/* the sliced writer: a long text in slices, written a piece at a time */
	{
		static char big[150000], want[160000];
		long i, k = 0, at;
		int f, ok;
		boot = 2;
		aes_ver = 0x0410;
		strcpy(scrap_dir, "C:\\CLIPBRD\\");
		for (i = 0; i < (long)sizeof(big) - 1; i++)
			big[i] = (i % 61 == 60) ? '\n' : (char)('a' + i % 26);
		for (i = 0; i < (long)sizeof(big) - 1; i++) {
			if (big[i] == '\n')
				want[k++] = '\r';
			want[k++] = big[i];
		}
		want[k] = 0;
		put_file("C:\\CLIPBRD\\SCRAP.TXT", "old");
		put_file("C:\\CLIPBRD\\SCRAP.RTF", "old rtf");
		opens = writes = 0;
		broadcasts = 0;
		ok = clip_put_begin();
		for (at = 0; at < (long)sizeof(big) - 1 && ok; at += 4096)
			ok = clip_put_step(big + at, at + 4096 < (long)sizeof(big) - 1 ? 4096 : (long)sizeof(big) - 1 - at);
		ok = ok && clip_put_end(1);
		f = find("C:\\CLIPBRD\\SCRAP.TXT");
		check(ok && f >= 0 && files[f].len == k && !memcmp(files[f].data, want, (size_t)k),
		      "clip_put_step: 150 KB in 4 KB slices, CR LF added, the same text");
		check(writes == (int)((k + 16383 - 2) / (16384 - 2)) && opens == writes,
		      "clip_put_step: one open and one Fwrite a 16 KB piece");
		check(find("C:\\CLIPBRD\\SCRAP.RTF") < 0 && broadcasts == 1,
		      "clip_put_end: the old SCRAP.* gone, SC_CHANGED told");
		/* a CR LF already there, split between two slices, isn't doubled */
		clip_put_begin();
		clip_put_step("a\r", 2);
		clip_put_step("\nb\n", 3);
		clip_put_end(1);
		check(!strcmp(files[find("C:\\CLIPBRD\\SCRAP.TXT")].data, "a\r\nb\r\n"),
		      "clip_put_step: CR LF across two slices kept as it is");
		/* given up before the first piece: the old clipboard stays */
		put_file("C:\\CLIPBRD\\SCRAP.TXT", "keep me");
		opens = 0;
		clip_put_begin();
		clip_put_step("never", 5);
		check(clip_put_end(0) == 0 && !strcmp(files[find("C:\\CLIPBRD\\SCRAP.TXT")].data, "keep me") && opens == 0,
		      "clip_put_end(0) before a piece: the old clipboard untouched");
		/* given up after a piece: no half a text left */
		clip_put_begin();
		clip_put_step(big, 40000);
		check(clip_put_end(0) == 0 && find("C:\\CLIPBRD\\SCRAP.TXT") < 0,
		      "clip_put_end(0) after a piece: SCRAP.TXT removed");
		/* an empty text */
		check(clip_put("", 0) == 1 && (f = find("C:\\CLIPBRD\\SCRAP.TXT")) >= 0 && files[f].len == 0,
		      "clip_put of nothing: an empty SCRAP.TXT");
	}

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
