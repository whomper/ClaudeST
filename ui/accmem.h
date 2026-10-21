/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * accmem.h - an accessory's memory (the AERA guidelines, 2.1 and 2.2):
 * taken once, before its first event call, then measured, so that the
 * next start takes what it needs and leaves the rest to the programs
 * and the other accessories.
 *
 *   acc_heap_take(first, tt_first, heapkb, memkb)
 *        before the first event call (after nothing else was malloc'd:
 *        libc's heap_early counts what was). first: what the app takes
 *        when it knows nothing yet (bytes); tt_first: the same in TT RAM
 *        (0: first); heapkb: HEAPKB= (0: none), the user's, taken as it
 *        is; memkb: MEMKB= (0: none), what it measured last time.
 *   acc_mem_kb(need, margin)
 *        what to write as MEMKB=: the larger of need (the app's own
 *        floor: what it holds plus room for a job, in bytes) and
 *        heap_peak, with margin bytes more (at least a tenth), in KB
 *   acc_mem_moved(kb, memkb)   1 when MEMKB= should be written: it moved
 *                              by more than an eighth, or isn't there
 *   acc_mem_full_kb()          memory ran out: four times what was taken,
 *                              KB, for MEMKB= at the next start (which
 *                              then measures it down; at most half free)
 *   acc_mem_line(buf, size, next_kb)   the log's "memory: ..." line
 *
 * The share when nothing is known (the first start, or a new machine):
 * at most half of what is free, and a quarter when 1 MB or more is free,
 * so two accessories leave half of the machine to the program in front
 * (on a 4 MB Falcon with NVDI's fonts about 1.5 MB is free when the
 * accessories load; two halves left EMail 0 KB beside its program and
 * NVDI couldn't open its workstation, 20 Oct 2026). Measured (MEMKB=) or chosen (HEAPKB=): at most half.
 * Never under 64 KB. When half is under 96 KB (a 1 MB ST), all but
 * acc_heap_leave, at least 64 KB, and never more than all but 32 KB:
 * acc_heap_leave is 32 KB by default, what the desktop needs to start
 * (ECal's and EContact's network accessories, as before); an accessory a
 * program must run beside sets more first (EHypView: 96 KB, so that
 * ASKHELP.PRG, which needs 86 KB, starts on a 1 MB Falcon).
 */
#ifndef AERA_ACCMEM_H
#define AERA_ACCMEM_H

extern long acc_heap_bytes;	/* taken (0: none, or the program) */
extern long acc_heap_avail;	/* free when it was taken, where it was taken */
extern short acc_heap_tt;	/* 1: in TT/Fast RAM */
extern long acc_heap_leave;	/* the least left on a 1 MB machine (32 KB) */

long acc_heap_take(long first, long tt_first, long heapkb, long memkb);
long acc_mem_kb(long need, long margin);
int acc_mem_moved(long kb, long memkb);
long acc_mem_full_kb(void);
int acc_mem_line(char *buf, int size, long next_kb);

#endif
