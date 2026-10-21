/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for tests/people: what people_file.c needs, over the build
   machine's files, the opens counted (test_people_file.c) */
#ifndef PLAT_H
#define PLAT_H
#define PF_SEP  '/'
#define PF_READ 0
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_seek(int h, long off);
void pf_close(int h);
unsigned long pf_info(const char *path, long *size);
char *pf_load(const char *path, long *len);
#endif
