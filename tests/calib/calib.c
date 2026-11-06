/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * CALIB.PRG - what a file call costs on THIS machine's disk.
 *
 * Run it from the card or disk the AERA programs live on (a TOS program,
 * no windows). It times, with the 200 Hz clock corrected against the MFP
 * (tos_ms), the calls the programs make: create, open, lookup, directory
 * listing, read and write in 1, 16 and 32 KB pieces (sequential and
 * random), close after a write, rename, delete, make and remove a folder,
 * and a CPU loop for the machine's speed. It leaves CALIB.TXT in the folder
 * it was started in ("key value" lines, microseconds a call) and cleans up
 * its own files (a folder CALIBTMP). Integer arithmetic only.
 *
 * The numbers feed the rig's disk delays (slowdisk, common/tests/slowdisk):
 * Hatari's GEMDOS drive answers every file call in no emulated time.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"

#define FSZ   (256L * 1024L)
#define NFILE 40

static char out[3000];
static int olen;
static char home[200];

static void say(const char *s)
{
	Cconws(s);
}

static void kv(const char *key, long v)
{
	char b[80];
	snprintf(b, sizeof(b), "%s %ld\r\n", key, v);
	if (olen + (int)strlen(b) < (int)sizeof(out)) {
		memcpy(out + olen, b, strlen(b));
		olen += (int)strlen(b);
	}
	say(b);
}

/* microseconds a call, from the total ms of n of them */
static long per_us(u32 ms, long n)
{
	if (n <= 0)
		return 0;
	return (long)((ms * 1000UL + (u32)n / 2) / (u32)n);
}

static u32 seed = 12345;
static u32 rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 8) & 0x7fffUL;
}

static char fname[20];
static const char *name_of(int i, char c)
{
	snprintf(fname, sizeof(fname), "%c%04d.DAT", c, i);
	return fname;
}

static long read_pass(const char *f, long piece, long total, char *buf)
{
	long h = Fopen(f, 0), got = 0;
	u32 t0;
	if (h < 0)
		return -1;
	t0 = tos_ms();
	while (got < total) {
		long r = Fread((short)h, piece, buf);
		if (r <= 0)
			break;
		got += r;
	}
	t0 = tos_ms() - t0;
	Fclose((short)h);
	return per_us(t0, total / piece);
}

static long rread_pass(const char *f, long piece, int n, char *buf)
{
	long h = Fopen(f, 0);
	u32 t0;
	int i;
	if (h < 0)
		return -1;
	t0 = tos_ms();
	for (i = 0; i < n; i++) {
		long off = (long)(rnd() % (u32)(FSZ / piece)) * piece;
		Fseek(off, (short)h, 0);
		Fread((short)h, piece, buf);
	}
	t0 = tos_ms() - t0;
	Fclose((short)h);
	return per_us(t0, n);
}

static void write_pass(const char *f, long piece, long total, char *buf, const char *tag)
{
	long h = Fcreate(f, 0);
	long n = 0;
	u32 t0, t1;
	char key[32];
	if (h < 0)
		return;
	t0 = tos_ms();
	while (n * piece < total) {
		Fwrite((short)h, piece, buf);
		n++;
	}
	t0 = tos_ms() - t0;
	t1 = tos_ms();
	Fclose((short)h);
	t1 = tos_ms() - t1;
	snprintf(key, sizeof(key), "write%s_us", tag);
	kv(key, per_us(t0, n));
	snprintf(key, sizeof(key), "close_after_write%s_us", tag);
	kv(key, (long)(t1 * 1000UL));
	Fdelete(f);
}

int main(void)
{
	char *buf = (char *)Malloc(32768L);
	char dta[44];
	long h, n, i, ent;
	u32 t0, t;
	volatile long lp;
	int drv = (int)Dgetdrv();

	if (!buf) {
		say("CALIB: no memory\r\n");
		return 1;
	}
	memset(buf, 0x5a, 32768L);
	home[0] = (char)('A' + drv);
	home[1] = ':';
	if (Dgetpath(home + 2, drv + 1) < 0)
		home[2] = 0;
	say("CALIB: file call times on this disk (about two minutes)\r\n");

	kv("calib", 1);
	kv("tos", trap1_w(0x30));
	kv("drive", 'A' + drv);
	kv("free_kb", (long)Malloc(-1L) / 1024);

	t0 = tos_ms();
	for (lp = 0; lp < 300000L; lp++)
		;
	kv("cpuloop_ms", (long)(tos_ms() - t0));

	Dcreate("CALIBTMP");
	if (Dsetpath("CALIBTMP") < 0) {
		say("CALIB: can't enter CALIBTMP\r\n");
		return 1;
	}
	Fsetdta(dta);

	/* the base file: also the first write timing */
	h = Fcreate("F0.DAT", 0);
	if (h < 0) {
		say("CALIB: can't create a file here (write-protected?)\r\n");
		return 1;
	}
	t0 = tos_ms();
	for (n = 0; n < FSZ / 8192; n++)
		Fwrite((short)h, 8192L, buf);
	t0 = tos_ms() - t0;
	Fclose((short)h);
	kv("write8k_first_us", per_us(t0, FSZ / 8192));

	/* create */
	t0 = tos_ms();
	for (i = 0; i < NFILE; i++) {
		h = Fcreate(name_of((int)i, 'T'), 0);
		if (h >= 0)
			Fclose((short)h);
	}
	t = tos_ms() - t0;
	kv("create_us", per_us(t, NFILE));

	/* open and close */
	t0 = tos_ms();
	for (i = 0; i < 100; i++) {
		h = Fopen("F0.DAT", 0);
		if (h >= 0)
			Fclose((short)h);
	}
	t = tos_ms() - t0;
	kv("open_close_us", per_us(t, 100));

	/* lookups */
	t0 = tos_ms();
	for (i = 0; i < 100; i++)
		Fsfirst("F0.DAT", 0);
	t = tos_ms() - t0;
	kv("lookup_us", per_us(t, 100));
	t0 = tos_ms();
	for (i = 0; i < 100; i++)
		Fsfirst("NOPE.XXX", 0);
	t = tos_ms() - t0;
	kv("lookup_miss_us", per_us(t, 100));

	/* a directory listing: NFILE + 1 entries */
	t0 = tos_ms();
	ent = 0;
	for (i = 0; i < 10; i++) {
		long r = Fsfirst("*.*", 0);
		while (r == 0) {
			ent++;
			r = Fsnext();
		}
	}
	t = tos_ms() - t0;
	kv("dirlist_entry_us", per_us(t, ent));
	kv("dirlist_entries", ent / 10);

	/* reads, sequential (the pass includes no open) */
	kv("read1k_us", read_pass("F0.DAT", 1024L, FSZ, buf));
	kv("read16k_us", read_pass("F0.DAT", 16384L, FSZ, buf));
	kv("read32k_us", read_pass("F0.DAT", 32768L, FSZ, buf));
	kv("rread1k_us", rread_pass("F0.DAT", 1024L, 100, buf));
	kv("rread16k_us", rread_pass("F0.DAT", 16384L, 40, buf));

	/* writes (create, pieces, close; the file removed) */
	write_pass("F1.DAT", 1024L, 64L * 1024L, buf, "1k");
	write_pass("F1.DAT", 16384L, 128L * 1024L, buf, "16k");
	write_pass("F1.DAT", 32768L, 128L * 1024L, buf, "32k");

	/* rename and delete */
	t0 = tos_ms();
	for (i = 0; i < NFILE; i++) {
		char to[20];
		snprintf(to, sizeof(to), "%s", name_of((int)i, 'U'));
		Frename(name_of((int)i, 'T'), to);
	}
	t = tos_ms() - t0;
	kv("rename_us", per_us(t, NFILE));
	t0 = tos_ms();
	for (i = 0; i < NFILE; i++)
		Fdelete(name_of((int)i, 'U'));
	t = tos_ms() - t0;
	kv("delete_us", per_us(t, NFILE));
	t0 = tos_ms();
	for (i = 0; i < 10; i++) {
		snprintf(fname, sizeof(fname), "D%02d", (int)i);
		Dcreate(fname);
	}
	t = tos_ms() - t0;
	kv("mkdir_us", per_us(t, 10));
	t0 = tos_ms();
	for (i = 0; i < 10; i++) {
		snprintf(fname, sizeof(fname), "D%02d", (int)i);
		Ddelete(fname);
	}
	t = tos_ms() - t0;
	kv("rmdir_us", per_us(t, 10));

	/* clean up, leave CALIB.TXT where we started */
	Fdelete("F0.DAT");
	Dsetpath("\\");
	Dsetpath(home + 2);
	Ddelete("CALIBTMP");
	h = Fcreate("CALIB.TXT", 0);
	if (h >= 0) {
		Fwrite((short)h, (long)olen, out);
		Fclose((short)h);
		say("CALIB: written CALIB.TXT, thank you. Press a key.\r\n");
	} else
		say("CALIB: could not write CALIB.TXT. Press a key.\r\n");
	Mfree(buf);
	trap1_w(0x01);
	return 0;
}
