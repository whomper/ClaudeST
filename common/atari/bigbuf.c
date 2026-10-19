/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bigbuf.c - a bulk job's memory: TT RAM when the machine has it, else ST
 * RAM (bigbuf.h). From EFTP's pf_bigbuf. A little is left: for the heap,
 * and in ST RAM for GEM and the desk accessories (a 1 MB ST with STinG has
 * only tens of KB spare, which still beats 4 KB at a time).
 */
#include "tos.h"
#include "bigbuf.h"

#define TT_SPARE (64L * 1024)
#define ST_SPARE (32L * 1024)

void *big_alloc(long want, long *got, int *tt)
{
	long tfree = (long)Mxalloc(-1L, 1), sfree, take;
	int old = tfree == -32;		/* TOS 1.x: no Mxalloc, no TT RAM */
	void *p;
	*got = 0;
	*tt = 0;
	if (old || tfree < 0)
		tfree = 0;
	sfree = old ? (long)Malloc(-1L) : (long)Mxalloc(-1L, 0);
	if (tfree - TT_SPARE >= BIG_LEAST || (want > 0 && tfree - TT_SPARE >= want)) {
		take = tfree - TT_SPARE;
		*tt = 1;
	} else {
		take = sfree - ST_SPARE;
	}
	if (want >= 0 && take > want)
		take = want;
	if (take <= 0 || (take < BIG_LEAST && take != want)) {
		*tt = 0;
		return 0;
	}
	take = (take + 1) & ~1L;
	p = old ? Malloc(take) : Mxalloc(take, *tt ? 1 : 0);
	if ((long)p <= 0) {
		*tt = 0;
		return 0;
	}
	*got = take;
	return p;
}

void big_free(void *p)
{
	if (p)
		Mfree(p);
}

int big_direct(void)
{
	return get_cookie(0x5F465242L) != 0;	/* '_FRB' */
}
