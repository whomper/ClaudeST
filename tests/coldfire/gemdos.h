/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
#ifndef CF_GEMDOS_H
#define CF_GEMDOS_H
extern long gd_mshrink_base, gd_mshrink_size, gd_calls;
extern int (*gd_pterm)(int code);
void gd_install(void);
/* a signal's handler(sig, info, uc), in and out in ColdFire code (rt.S) */
void sig_install(int sig, void (*h)(int, void *, void *));
typedef struct {
	unsigned long uc_flags, uc_link, ss_sp, ss_flags, ss_size;
	unsigned long version;
	unsigned long gregs[18];	/* d0-d7, a0-a6, sp, pc, sr */
} UCTX;
#endif
