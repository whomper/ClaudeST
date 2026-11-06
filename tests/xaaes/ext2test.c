/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * EXT2TEST.PRG - the rig's own check of its ext2 drive (run.py --ext2,
 * make ext2check): FreeMiNT's ext2.xfs must give a case-sensitive drive, as
 * a tester's has (the guidelines, 6.6). It looks for probe.txt, which the
 * rig put in the ext2 drive's root in lowercase, on D: to P:; there
 *   - probe.txt opens,
 *   - PROBE.TXT doesn't (the drive keeps case apart),
 *   - Dpathconf's DP_CASE says how MiNT sees the drive,
 *   - made.txt is created, in lowercase, for the rig to find on the image
 *     (Sync: MiNT keeps it in its cache otherwise, and Hatari may stop first).
 * Each step is a NatFeats line ("EXT2 ..."), the last "EXT2 done".
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

#define Dpathconf(p, m)	trap1_wlw(0x124, (long)(p), (m))
#define Sync()		trap1_w(0x150)	/* MiNT: the buffer cache to the disks */
#define DP_CASE		6

static void say(const char *a, const char *b)
{
	static char line[160];
	strcpy(line, "EXT2 ");
	strcat(line, a);
	strcat(line, b);
	strcat(line, "\n");
	nf_debug(line);
}

int main(void)
{
	static char path[32], buf[64];
	static const char made[] = "made by EXT2TEST.PRG\r\n";
	long h, n, map;
	short d;
	char drv = 0;
	appl_init();
	map = Drvmap();
	for (d = 3; d < 16 && !drv; d++) {
		if (!(map & (1L << d)))
			continue;
		path[0] = (char)('a' + d);
		strcpy(path + 1, ":\\probe.txt");
		h = Fopen(path, 0);
		if (h >= 0) {
			drv = path[0];
			n = Fread((short)h, sizeof(buf) - 1, buf);
			Fclose((short)h);
			buf[n > 0 ? n : 0] = 0;
			buf[strcspn(buf, "\r\n")] = 0;
			say("probe.txt opened on ", path);
			say("probe.txt says: ", buf);
		}
	}
	if (!drv) {
		say("no drive has probe.txt", "");
		say("done", "");
		appl_exit();
		return 1;
	}
	path[0] = drv;
	strcpy(path + 1, ":\\PROBE.TXT");
	h = Fopen(path, 0);
	if (h >= 0) {
		Fclose((short)h);
		say("PROBE.TXT opened too: the drive takes any case", "");
	} else {
		say("PROBE.TXT not found: the drive keeps case apart", "");
	}
	path[1] = ':';
	path[2] = '\\';
	path[3] = 0;
	n = Dpathconf(path, DP_CASE);
	buf[0] = (char)('0' + (n >= 0 && n < 10 ? n : 9));
	buf[1] = 0;
	say("DP_CASE ", n < 0 ? "an error" : buf);
	strcpy(path + 1, ":\\made.txt");
	h = Fcreate(path, 0);
	if (h >= 0) {
		Fwrite((short)h, sizeof(made) - 1, made);
		Fclose((short)h);
		Sync();
		say("made.txt created", "");
	} else {
		say("made.txt could not be created", "");
	}
	say("done", "");
	appl_exit();
	return 0;
}
