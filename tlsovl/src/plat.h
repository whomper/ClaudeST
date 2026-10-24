/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for TLS.OVL: the family's file, clock and network calls, which
   head.c hands on to the program that loaded it (TLS_HOST) */
#ifndef PLAT_H
#define PLAT_H
#include <stddef.h>
#define PF_SEP    '\\'
#define PF_READ   0
#define PF_WRITE  1
#define PF_APPEND 2
#define PF_UPDATE 3
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_seek(int h, long off);
long pf_size(const char *path);
int  pf_remove(const char *path);
char *pf_load(const char *path, long *len);
int   pf_save(const char *path, const void *data, long len);
typedef struct {
	short year, mon, day, hour, min, sec;
} PFTIME;
void pf_now(PFTIME *t);
unsigned long pf_ms(void);
void pf_entropy(unsigned char *buf, int n);
long net_write(int h, const void *buf, long n);
long net_read(int h, void *buf, long max);
extern void (*pf_idle)(void);
#endif
