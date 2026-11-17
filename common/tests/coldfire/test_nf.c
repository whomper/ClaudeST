/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* test_nf.c - nf.S as ColdFire code under qemu-m68k -cpu cfv4e: no
 * NatFeats on a FireBee, so none is found and a call does nothing; run
 * in user mode, it would fault if it touched the illegal instruction's
 * vector at 0x10 as the 68000's build does */
#include "rt.h"

long nf_probe(void), nf_find(const char *name), nf_call2(long id, long arg);

int main(void)
{
	int fails = (nf_probe() != 0) + (nf_find("NF_EXIT") != 0) + (nf_call2(5, (long)"x") != 0);
	out(fails ? "nf: FAIL\n" : "nf: no NatFeats on the ColdFire, none tried and no vector touched\n");
	return fails != 0;
}
