/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * clip.c - the GEM clipboard (see clip.h), as cflib's scrap_* and
 * EContact's copy: the folder from scrp_read(), SCRAP.* cleared before
 * SCRAP.TXT is written, SC_CHANGED to every program.
 */
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "clip.h"

#define SC_CHANGED 80

static int is_dir(const char *p)
{
	long a = Fattrib(p, 0, 0);
	return a >= 0 && (a & 0x10);
}

/* the clipboard's folder, ending '\\': the AES's (EmuTOS names
   C:\\CLIPBRD before there is one: made when it isn't there yet; a file
   name some programs leave at the end taken off), else \\CLIPBRD on the
   boot drive, else on the drive we run from (the boot drive may be a
   write-protected floppy) */
static int folder(char *dir)
{
	long n;
	char *s;
	dir[0] = 0;
	scrp_read(dir);
	n = (long)strlen(dir);
	while (n > 3 && dir[n - 1] == '\\')
		dir[--n] = 0;
	if (n > 4 && dir[n - 4] == '.' && (s = strrchr(dir, '\\')) != 0 && s - dir > 2)
		*s = 0;
	if (!dir[0] || (n > 3 && !is_dir(dir) && Dcreate(dir) < 0)) {
		strcpy(dir, "C:\\CLIPBRD");
		dir[0] = (char)('A' + boot_drive());
		if (!is_dir(dir) && Dcreate(dir) < 0) {
			dir[0] = (char)('A' + Dgetdrv());
			if (!is_dir(dir) && Dcreate(dir) < 0)
				return 0;
		}
		strcat(dir, "\\");
		scrp_write(dir);
		return 1;
	}
	n = (long)strlen(dir);
	if (n < 120 && dir[n - 1] != '\\') {
		dir[n] = '\\';
		dir[n + 1] = 0;
	}
	return 1;
}

/* SCRAP.* away: the clipboard holds one thing at a time */
static void clear(const char *dir)
{
	char pat[160], path[160];
	DTA dta, *old = (DTA *)Fgetdta();
	strcpy(pat, dir);
	strcat(pat, "SCRAP.*");
	Fsetdta(&dta);
	if (Fsfirst(pat, 0) == 0) {
		do {
			strcpy(path, dir);
			strcat(path, dta.d_fname);
			Fdelete(path);
		} while (Fsnext() == 0);
	}
	Fsetdta(old);
}

/* SC_CHANGED to all, where the AES broadcasts (shel_write 7: appl_getinfo
   (10)'s low byte 7 or more; MagiC). appl_getinfo itself is only there
   from AES 4.0, or with the ?AGI cookie: EmuTOS's hangs */
static void tell(void)
{
	short msg[8], o1 = 0, d;
	int magic = get_cookie(0x4D616758L) != 0;		/* MagX */
	if (!magic && !((aes_version() >= 0x0400 || get_cookie(0x3F414749L)) &&	/* ?AGI */
			appl_getinfo(10, &o1, &d, &d, &d) && (o1 & 0xff) >= 7))
		return;
	memset(msg, 0, sizeof(msg));
	msg[0] = SC_CHANGED;
	msg[1] = appl_id();
	msg[3] = 0x0002;			/* text */
	msg[4] = 0x2E54;			/* ".TXT" */
	msg[5] = 0x5854;
	shel_write(7, 0, 0, msg, 0);
}

/* the sliced writer (EPDF, 31 Oct 2026: a whole document copied): the
   text gathers in memory and goes out a piece at a time, one Fwrite a
   piece; no file is open between calls (an accessory's handles are the
   program in front's, 2.2), so the first piece creates SCRAP.TXT and the
   next ones open it again at its end. Nothing on the disk changes until
   the first piece: given up before it, the old clipboard stays */
#define CLIP_PIECE_000 16384L
#define CLIP_PIECE_030 32768L
static struct {
	int on, ok, made;
	char last;
	char path[160];
	char small[512], *buf;
	long size, k;
} cw;

/* SCRAP.TXT made (the old SCRAP.* gone first) or opened at its end */
static long cw_open(void)
{
	long fh;
	if (!cw.made) {
		char dir[160];
		strcpy(dir, cw.path);
		dir[strrchr(dir, '\\') - dir + 1] = 0;
		clear(dir);
		fh = Fcreate(cw.path, 0);
	} else if ((fh = Fopen(cw.path, 1)) >= 0)
		Fseek(0L, (short)fh, 2);
	if (fh < 0)
		cw.ok = 0;
	else
		cw.made = 1;
	return fh;
}

static void cw_flush(void)
{
	long fh;
	if (!cw.ok || !cw.k || (fh = cw_open()) < 0)
		return;
	cw.ok = Fwrite((short)fh, cw.k, cw.buf) == cw.k;
	Fclose((short)fh);
	cw.k = 0;
}

int clip_put_begin(void)
{
	char dir[160];
	if (cw.on)
		clip_put_end(0);
	if (!folder(dir))
		return 0;
	memset(&cw, 0, sizeof(cw));
	strcpy(cw.path, dir);
	strcat(cw.path, "SCRAP.TXT");
	cw.size = get_cookie(0x5F435055L) >= 20 ? CLIP_PIECE_030 : CLIP_PIECE_000;	/* '_CPU' */
	if ((cw.buf = malloc((size_t)cw.size)) == 0) {
		cw.buf = cw.small;
		cw.size = (long)sizeof(cw.small);
	}
	cw.on = cw.ok = 1;
	return 1;
}

int clip_put_step(const char *text, long n)
{
	long i;
	if (!cw.on)
		return 0;
	/* lines end CR LF, whichever the program uses */
	for (i = 0; i < n && cw.ok; i++) {
		if (cw.k >= cw.size - 2)
			cw_flush();
		if (text[i] == '\n' && cw.last != '\r')
			cw.buf[cw.k++] = '\r';
		cw.buf[cw.k++] = cw.last = text[i];
	}
	return cw.ok;
}

int clip_put_end(int ok)
{
	int done;
	long fh;
	if (!cw.on)
		return 0;
	if (ok && cw.ok) {
		if (!cw.made && !cw.k) {		/* an empty text: an empty SCRAP.TXT */
			if ((fh = cw_open()) >= 0)
				Fclose((short)fh);
		} else
			cw_flush();
	}
	done = ok && cw.ok;
	if (!done && cw.made)
		Fdelete(cw.path);		/* given up or failed: no half a text */
	if (cw.buf != cw.small)
		free(cw.buf);
	cw.on = 0;
	if (done)
		tell();
	return done;
}

int clip_put(const char *text, long n)
{
	if (!clip_put_begin())
		return 0;
	clip_put_step(text, n);
	return clip_put_end(1);
}

long clip_get(char *buf, long size)
{
	char dir[160], path[160];
	long fh, n, i, k = 0;
	if (size < 1)
		return 0;
	buf[0] = 0;
	if (!folder(dir))
		return 0;
	strcpy(path, dir);
	strcat(path, "SCRAP.TXT");
	fh = Fopen(path, 0);
	if (fh < 0)
		return 0;
	n = Fread((short)fh, size - 1, buf);
	Fclose((short)fh);
	if (n < 0)
		n = 0;
	/* CR LF (or a lone CR) to '\n'; a NUL ends it */
	for (i = 0; i < n && buf[i]; i++) {
		if (buf[i] == '\r') {
			buf[k++] = '\n';
			if (i + 1 < n && buf[i + 1] == '\n')
				i++;
		} else {
			buf[k++] = buf[i];
		}
	}
	buf[k] = 0;
	return k;
}

char *clip_line(char *buf)
{
	char *p;
	for (p = buf; *p && *p != '\n'; p++)
		if (*p == '\t')
			*p = ' ';
	*p = 0;
	return buf;
}

int clip_key(short key)
{
	switch (key & 0xff) {			/* the control codes, whatever the keyboard */
	case 0x03: return CLIP_COPY;
	case 0x18: return CLIP_CUT;
	case 0x16: return CLIP_PASTE;
	}
	return CLIP_NONE;
}
