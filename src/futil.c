/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * futil.c - whole-file helpers on top of plat.h.
 *
 * pf_save() writes NAME.IN$ first and renames it over NAME.INF (the AERA
 * guidelines, 2.3): a reset or a full disk in the middle leaves the old
 * file as it was. pf_load() finds a NAME.IN$ whose renaming was cut
 * short and takes it.
 */
#include <stdlib.h>
#include <string.h>
#include "plat.h"

/* where pf_save writes first: the last letter of the extension '$'
   (ECAL.IN$; '~' when it is '$' already), NAME.$$$ without one. An 8.3
   name, as TOS's FAT wants: there ECAL.INF.TMP would be ECAL.INF itself */
static int tmp_name(const char *path, char *out, long size)
{
	const char *base = path, *dot = 0, *p;
	long n = (long)strlen(path);
	if (n < 1 || n + 5 > size)
		return 0;
	for (p = path; *p; p++) {
		if (*p == '\\' || *p == '/' || *p == ':') {
			base = p + 1;
			dot = 0;
		} else if (*p == '.' && p > base) {
			dot = p;
		}
	}
	strcpy(out, path);
	if (!dot)
		strcat(out, ".$$$");
	else if (strlen(dot + 1) < 3)
		strcat(out, "$");
	else
		out[n - 1] = out[n - 1] == '$' ? '~' : '$';
	return 1;
}

/* an open file's size, its position left at the start (GEMDOS: Fseek
   to the end and back), or NULL. An app that sets it (declared in its
   plat.h) has pf_load open the file first and look at the directory
   only when that fails: one look per file read instead of two (the
   guidelines, 6.5) */
long (*pf_size_open)(int h);

char *pf_load(const char *path, long *len)
{
	long size = -1, got = 0, r;
	char *buf;
	int h = -1;
	if (len)
		*len = 0;
	if (pf_size_open && (h = pf_open(path, PF_READ)) >= 0 && (size = pf_size_open(h)) < 0) {
		pf_close(h);
		h = -1;
		size = pf_size(path);
	} else if (!pf_size_open) {
		size = pf_size(path);	/* with the hook, a failed open says it */
	}
	if (h < 0 && size < 0) {
		/* a save cut short after the old file went: the new one is whole */
		char tmp[260];
		if (!tmp_name(path, tmp, sizeof(tmp)) || pf_size(tmp) < 0 || pf_rename(tmp, path) != 0)
			return 0;
		size = pf_size(path);
		if (size < 0)
			return 0;
	}
	if (h < 0)
		h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	buf = malloc(size + 1);
	if (!buf) {
		pf_close(h);
		return 0;
	}
	while (got < size && (r = pf_read(h, buf + got, size - got)) > 0)
		got += r;
	pf_close(h);
	buf[got] = 0;
	if (len)
		*len = got;
	return buf;
}

static int write_file(const char *path, const void *data, long len)
{
	int h = pf_open(path, PF_WRITE);
	long r;
	if (h < 0)
		return h;
	r = pf_write(h, data, len);
	pf_close(h);
	return r == len ? 0 : -1;
}

int pf_save(const char *path, const void *data, long len)
{
	char tmp[260];
	if (tmp_name(path, tmp, sizeof(tmp)) && write_file(tmp, data, len) == 0) {
		pf_remove(path);		/* TOS doesn't rename over a file */
		if (pf_rename(tmp, path) == 0)
			return 0;
	}
	/* no room for both, or a drive that doesn't rename: as before */
	pf_remove(tmp);
	return write_file(path, data, len);
}
