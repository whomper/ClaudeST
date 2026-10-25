/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for tests/connslice: what conn.c needs, a stand-in network and
   clock (test_connslice.c) */
#ifndef PLAT_H
#define PLAT_H
#include <stddef.h>
#define PF_SEP    '/'
#define PF_READ   0
#define PF_WRITE  1
#define PF_APPEND 2
int  pf_open(const char *path, int mode);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
typedef struct {
	short year, mon, day, hour, min, sec;
} PFTIME;
void pf_now(PFTIME *t);
unsigned long pf_ms(void);
int  net_open(const char *host, unsigned short port, char *err, int errlen);
long net_write(int h, const void *buf, long n);
long net_read(int h, void *buf, long max);
void net_close(int h);
void net_close_wait(int h, int seconds);
extern void (*pf_idle)(void);
#endif
