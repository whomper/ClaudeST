/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for tests/tlsovl: what src/ovl.c needs, over the build
   machine's files (test_ovl.c) */
#ifndef PLAT_H
#define PLAT_H
#define PF_READ 0
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_seek(int h, long off);
void pf_close(int h);
extern void (*pf_idle)(void);
#endif
