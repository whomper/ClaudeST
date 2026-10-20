/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat_tos.c - plat.h's file calls on GEMDOS, as ECal's atari/plat_tos.c.
 */
#include "tos.h"
#include "plat.h"

int pf_open(const char *path, int mode)
{
	return (int)(mode == PF_WRITE ? Fcreate(path, 0) : Fopen(path, 0));
}

long pf_read(int h, void *buf, long n) { return Fread((short)h, n, buf); }
long pf_write(int h, const void *buf, long n) { return Fwrite((short)h, n, buf); }
void pf_close(int h) { Fclose((short)h); }
int pf_remove(const char *path) { return (int)Fdelete(path); }
int pf_rename(const char *from, const char *to) { return (int)Frename(from, to); }

/* the size of the open file: main() sets futil.c's pf_size_open to it,
   so pf_load opens first and asks then (one look, the guidelines 6.5) */
long pf_handle_size(int h)
{
	long n = Fseek(0, (short)h, 2);
	Fseek(0, (short)h, 0);
	return n;
}

long pf_size(const char *path)
{
	static DTA dta;
	void *old = Fgetdta();
	long r;
	Fsetdta(&dta);
	r = Fsfirst(path, 0x07) == 0 ? (long)dta.d_length : -1;
	Fsetdta(old);
	return r;
}
