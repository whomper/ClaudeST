/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for LANGT.PRG: what common's conn.c and atari/net_tos.c take from an app's platform
   layer (an app's atari/plat_tos.c); stub/plat.c is the smallest that serves them */
#ifndef PLAT_H
#define PLAT_H
#include <stddef.h>
#define PF_SEP    '\\'
#define PF_READ   0
#define PF_WRITE  1
#define PF_APPEND 2
int  pf_open(const char *path, int mode);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_size(const char *path);
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
long pf_seek(int h, long off);
typedef struct {
	short year, mon, day, hour, min, sec;
} PFTIME;
void pf_now(PFTIME *t);
unsigned long pf_ms(void);
char *pf_load(const char *path, long *len);
void pf_entropy(unsigned char *buf, int n);
int  net_init(void);
const char *net_stack(void);
int  net_open(const char *host, unsigned short port, char *err, int errlen);
long net_write(int h, const void *buf, long n);
long net_read(int h, void *buf, long max);
void net_close(int h);
void net_close_wait(int h, int seconds);
extern void (*pf_idle)(void);
#endif
