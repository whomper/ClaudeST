/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The machine a measuring tool ran on, first in its log (NSBENCH.TTP,
 * EWGET.TTP; the FireBee tester's runs, the hub, 10 Nov 2026): each read
 * that may fault on a ColdFire (the cookie jar, the OS header, the CACR)
 * through guard.S, so a refusal prints "no answer", and each line written
 * before the next read, so a crash still leaves what came before. */
#ifndef FB_MACHINE_H
#define FB_MACHINE_H

/* out: the tool's own (its log flushed by each write: GEMDOS's Fwrite) */
void machine_block(void (*out)(const char *fmt, ...));
/* what machine_block found (0 for no answer) */
extern long mb_cpu, mb_fpu, mb_mch;
/* 1 when the CACR may be read (a 68020 or better that answered it) */
extern int mb_cacr_ok;
extern long mb_cacr;

#endif
