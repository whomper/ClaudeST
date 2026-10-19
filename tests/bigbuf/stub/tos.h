/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* tos.h for tests/bigbuf: GEMDOS's memory calls are the test's own */
#ifndef TOS_H
#define TOS_H
void *Mxalloc(long n, int mode);
void *Malloc(long n);
long Mfree(void *p);
long get_cookie(long id);
#endif
