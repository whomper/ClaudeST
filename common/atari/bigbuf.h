/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bigbuf.h - a bulk job's memory (the guidelines, 6.5): a sync, a download
 * or an index gathers in it and goes to disk in one go. TT RAM (a TT's, an
 * accelerated Falcon's Fast RAM) when the machine has enough, else ST RAM;
 * a little of each is left for the heap, GEM and the desk accessories.
 * From EFTP's pf_bigbuf.
 *
 * want: the bytes the job needs, or -1 when it doesn't know (as much as
 * there is). *got: what was given; *tt: 1 when it is TT RAM. 0 when there
 * is less than BIG_LEAST (8 KB) and less than want: the job goes through
 * its own small buffer then.
 *
 * An accessory takes it only before its first event call (2.2): later the
 * memory would belong to the program in front.
 */
#ifndef BIGBUF_H
#define BIGBUF_H

#define BIG_LEAST (8L * 1024)

void *big_alloc(long want, long *got, int *tt);
void big_free(void *p);

#endif
