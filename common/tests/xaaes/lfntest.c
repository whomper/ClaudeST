/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * LFNTEST.PRG: long file names under FreeMiNT, for run.py --ext2 (the
 * file dialog's, ui/filepick.c: names cut to 8+3 on ext2 and VFAT; the
 * hub, 9 Nov 2026). On the first drive D: to P: that has lfnprobe.txt in
 * its root it says, as NatFeats lines ("LFN ..."):
 *   - the process's domain (Pdomain(-1): 0 TOS, 1 MiNT),
 *   - the names Fsfirst gives, the names Dopendir/Dreaddir give in mode 0
 *     (MiNT: long) and 1 (TOS), what Dxreaddir gives, each with its size,
 *   - whether the long file "A long file name.mp3" opens by its name,
 *   - the same after Pdomain(1).
 * "LFN done" ends it.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

static char line[400];

static void say(const char *a, const char *b)
{
	strcpy(line, "LFN ");
	strcat(line, a);
	strcat(line, b);
	strcat(line, "\n");
	nf_debug(line);
}

static void num(char *o, long v)
{
	char t[12];
	int n = 0;
	if (v < 0) {
		*o++ = '-';
		v = -v;
	}
	do
		t[n++] = (char)('0' + v % 10);
	while ((v /= 10) > 0);
	while (n)
		*o++ = t[--n];
	*o = 0;
}

static void fsfirst(const char *label, const char *root)
{
	static char pat[16], one[80];
	DTA dta, *old = Fgetdta();
	long r;
	strcpy(pat, root);
	strcat(pat, "*.*");
	Fsetdta(&dta);
	for (r = Fsfirst(pat, 0x17); r == 0; r = Fsnext()) {
		strcpy(one, label);
		strcat(one, dta.d_fname);
		strcat(one, " ");
		num(one + strlen(one), dta.d_length);
		say(one, "");
	}
	Fsetdta(old);
}

static void readdir(const char *label, const char *root, short mode)
{
	static char buf[300], one[320], nm[16];
	long h = Dopendir(root, mode), r;
	if (h < 0) {
		num(nm, h);
		say(label, "Dopendir fails: ");
		say(nm, "");
		return;
	}
	while ((r = Dreaddir(sizeof(buf), h, buf)) == 0) {
		strcpy(one, label);
		strcat(one, buf + 4);
		say(one, "");
	}
	Dclosedir(h);
}

static void xreaddir(const char *label, const char *root)
{
	static char buf[300], one[320];
	static short xa[40];
	long h = Dopendir(root, 0), r, xr = 0;
	if (h < 0)
		return;
	while ((r = Dxreaddir(sizeof(buf), h, buf, xa, &xr)) == 0 || r == -64) {
		/* XATTR: mode (2), index (4), dev, rdev, nlink, uid, gid (2 each), size (4) */
		long size = ((long)(unsigned short)xa[8] << 16) | (unsigned short)xa[9];
		strcpy(one, label);
		strcat(one, buf + 4);
		strcat(one, " mode ");
		num(one + strlen(one), (unsigned short)xa[0]);
		strcat(one, " size ");
		num(one + strlen(one), size);
		strcat(one, " xr ");
		num(one + strlen(one), xr);
		say(one, "");
		if (r == -64)
			break;
	}
	Dclosedir(h);
}

static void open_long(const char *label, const char *path)
{
	long h = Fopen(path, 0);
	static char o[100];
	strcpy(o, label);
	strcat(o, h >= 0 ? "opens" : "does not open: ");
	if (h >= 0)
		Fclose((short)h);
	else
		num(o + strlen(o), h);
	say(o, "");
}

int main(void)
{
	static char root[8], path[100];
	long map, h, d;
	short k;
	char drv = 0;
	appl_init();
	map = Drvmap();
	for (k = 3; k < 16 && !drv; k++) {
		if (!(map & (1L << k)))
			continue;
		strcpy(path, "x:\\lfnprobe.txt");
		path[0] = (char)('a' + k);
		h = Fopen(path, 0);
		if (h >= 0) {
			Fclose((short)h);
			drv = path[0];
		}
	}
	if (!drv) {
		say("no drive has lfnprobe.txt", "");
		say("done", "");
		appl_exit();
		return 1;
	}
	strcpy(root, "x:\\");
	root[0] = drv;
	d = Pdomain(-1);
	num(path, d);
	say("domain at the start ", path);
	fsfirst("Fsfirst: ", root);
	readdir("Dopendir0: ", root, 0);
	readdir("Dopendir1: ", root, 1);
	xreaddir("Dxreaddir: ", root);
	strcpy(path, root);
	strcat(path, "A long file name.mp3");
	open_long("long name, domain as started: ", path);
	d = Pdomain(1);
	num(path, d);
	say("Pdomain(1) gave ", path);
	d = Pdomain(-1);
	num(path, d);
	say("domain now ", path);
	fsfirst("Fsfirst MiNT domain: ", root);
	strcpy(path, root);
	strcat(path, "A long file name.mp3");
	open_long("long name, MiNT domain: ", path);
	say("done", "");
	appl_exit();
	return 0;
}
