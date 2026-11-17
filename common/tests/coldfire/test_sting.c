/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_sting.c - sting.S's sting_call as ColdFire code under qemu-m68k
 * -cpu cfv4e: the words passed packed, as Pure C takes them (a short, a
 * long as two words, a short), the result back, no arguments at all, and
 * d2-d7 and a2-a6 kept whatever the function clobbers of its own */
#include "rt.h"

long sting_call(void *fn, const short *args, long nwords);
long purec_fn(void), purec_none(void);
long call_kept(void *fn, const short *args, long nwords);
extern long keep_ret;
static int fails;

static void check(const char *what, long got, long want)
{
	if (got != want) {
		fails++;
		out("sting: FAIL "); out(what); out(": "); out_num((unsigned long)got); out("\n");
	}
}

int main(void)
{
	short a[4];
	long b = 1234567;
	a[0] = 3;
	a[1] = (short)(b >> 16);
	a[2] = (short)b;
	a[3] = -5;
	check("three packed arguments", sting_call((void *)purec_fn, a, 4), 3 * 100000L + 1234567 - 5);
	check("no arguments", sting_call((void *)purec_none, a, 0), 77);
	check("the registers kept", call_kept((void *)purec_fn, a, 4), 0);
	check("its result through them", keep_ret, 3 * 100000L + 1234567 - 5);
	out(fails ? "sting: failed\n" : "sting: sting_call passes Pure C's words and keeps what C keeps\n");
	return fails != 0;
}
