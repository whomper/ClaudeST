/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat.c - the smallest platform layer for LANGT.PRG: no log file, no files to load, the
 * clock from TOS. The network is common's atari/net_tos.c (STinG, here FAKESTNG).
 */
#include "tos.h"
#include "plat.h"

void (*pf_idle)(void);

int pf_open(const char *path, int mode)
{
	return -1;
}

long pf_write(int h, const void *buf, long n)
{
	return -1;
}

void pf_close(int h)
{
}

long pf_size(const char *path)
{
	return -1;
}

int pf_remove(const char *path)
{
	return 0;
}

int pf_rename(const char *from, const char *to)
{
	return 0;
}

long pf_seek(int h, long off)
{
	return off;
}

void pf_now(PFTIME *t)
{
	u16 d = (u16)Tgetdate(), m = (u16)Tgettime();
	t->year = (short)(1980 + (d >> 9));
	t->mon = (short)((d >> 5) & 15);
	t->day = (short)(d & 31);
	t->hour = (short)(m >> 11);
	t->min = (short)((m >> 5) & 63);
	t->sec = (short)((m & 31) * 2);
}

unsigned long pf_ms(void)
{
	return tos_ms();
}

char *pf_load(const char *path, long *len)
{
	return 0;
}

void pf_entropy(unsigned char *buf, int n)
{
	static unsigned long x;
	int i;
	x ^= tos_ms() * 2654435761UL + 12345UL;
	for (i = 0; i < n; i++) {
		x = x * 1103515245UL + 12345UL;
		buf[i] = (unsigned char)(x >> 16);
	}
}
