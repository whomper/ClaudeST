/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pf_save and pf_load (src/futil.c) on the build machine: the new file
 * goes in under a temporary 8.3 name and is renamed over the old one,
 * also where renaming over a file is refused (TOS); a save cut short
 * after the old file went is finished by the next pf_load.
 *   make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "plat.h"

static int tos_rename = 1, no_rename, fail_writes_to_tmp, failed, checks, looks, opens;
static char dir[] = "/tmp/futilXXXXXX";

int pf_open(const char *path, int mode)
{
	if (mode == PF_WRITE && fail_writes_to_tmp && strchr(path, '$'))
		return -1;
	opens++;
	return open(path, mode == PF_WRITE ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0644);
}
long pf_read(int h, void *buf, long n) { return (long)read(h, buf, n); }
long pf_write(int h, const void *buf, long n) { return (long)write(h, buf, n); }
void pf_close(int h) { close(h); }
long pf_size(const char *path)
{
	struct stat st;
	looks++;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}
int pf_remove(const char *path) { return unlink(path); }
int pf_rename(const char *from, const char *to)
{
	struct stat st;
	if (no_rename || (tos_rename && stat(to, &st) == 0))
		return -36;		/* EACCDN */
	return rename(from, to);
}

static long size_open(int h)
{
	long n = (long)lseek(h, 0, SEEK_END);
	return lseek(h, 0, SEEK_SET) == 0 ? n : -1;
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static char *at(const char *name)
{
	static char p[8][200];
	static int k;
	k = (k + 1) % 8;
	snprintf(p[k], sizeof(p[k]), "%s/%s", dir, name);
	return p[k];
}

static int holds(const char *name, const char *want)
{
	long n;
	char *t = pf_load(at(name), &n);
	int ok = t && n == (long)strlen(want) && !memcmp(t, want, n);
	free(t);
	return ok;
}

static void put(const char *name, const char *text)
{
	FILE *f = fopen(at(name), "w");
	fputs(text, f);
	fclose(f);
}

int main(void)
{
	if (!mkdtemp(dir))
		return 1;
	check(pf_save(at("ECAL.INF"), "one", 3) == 0 && holds("ECAL.INF", "one"), "a new file");
	check(pf_save(at("ECAL.INF"), "two!", 4) == 0 && holds("ECAL.INF", "two!"), "over the old one, renaming as TOS does");
	check(pf_size(at("ECAL.IN$")) < 0, "  no ECAL.IN$ left");
	tos_rename = 0;
	check(pf_save(at("ECAL.INF"), "three", 5) == 0 && holds("ECAL.INF", "three"), "renaming over it (MiNT, the build machine)");
	tos_rename = 1;
	check(pf_save(at("README"), "x", 1) == 0 && holds("README", "x") && pf_size(at("README.$$$")) < 0,
	      "a name without an extension: README.$$$ first");
	check(pf_save(at("A.C"), "c", 1) == 0 && holds("A.C", "c") && pf_size(at("A.C$")) < 0, "a short extension: A.C$");
	check(pf_save(at("X.IN$"), "d", 1) == 0 && holds("X.IN$", "d") && pf_size(at("X.IN~")) < 0, "X.IN$: X.IN~ first");
	/* the reset between taking the old file away and renaming */
	put("EMAIL.IN$", "whole");
	unlink(at("EMAIL.INF"));
	check(holds("EMAIL.INF", "whole") && pf_size(at("EMAIL.IN$")) < 0, "pf_load finishes a save cut short");
	put("ECAL.IN$", "half");
	check(holds("ECAL.INF", "three"), "a half-written ECAL.IN$ beside the file: the file");
	unlink(at("ECAL.IN$"));
	check(pf_load(at("NONE.INF"), 0) == 0, "no file: nothing");
	/* no room for the copy, or a drive that won't rename: in place */
	fail_writes_to_tmp = 1;
	check(pf_save(at("ECAL.INF"), "four", 4) == 0 && holds("ECAL.INF", "four"), "the temporary file can't be made: written in place");
	fail_writes_to_tmp = 0;
	no_rename = 1;
	check(pf_save(at("ECAL.INF"), "five", 4) == 0 && holds("ECAL.INF", "five") && pf_size(at("ECAL.IN$")) < 0,
	      "renaming refused: written in place, the temporary file gone");
	no_rename = 0;
	check(pf_save(at("NODIR/X.INF"), "y", 1) != 0, "a folder that isn't there: an error");
	/* pf_size_open: the file opened first, no look at the directory */
	looks = opens = 0;
	check(holds("ECAL.INF", "five") && looks == 1 && opens == 1, "without pf_size_open: a look, then the open");
	pf_size_open = size_open;
	looks = opens = 0;
	check(holds("ECAL.INF", "five") && looks == 0 && opens == 1, "with pf_size_open: the open alone");
	put("WIN.IN$", "won");
	check(holds("WIN.INF", "won") && pf_size(at("WIN.IN$")) < 0, "  and a save cut short is still finished");
	looks = opens = 0;
	check(pf_load(at("NONE.INF"), 0) == 0 && opens == 1 && looks == 1,
	      "  and no file is still nothing: the open, then only the look for NONE.IN$");
	pf_size_open = 0;
	unlink(at("WIN.INF"));
	unlink(at("ECAL.INF"));
	unlink(at("EMAIL.INF"));
	unlink(at("README"));
	unlink(at("A.C"));
	unlink(at("X.IN$"));
	rmdir(dir);
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
