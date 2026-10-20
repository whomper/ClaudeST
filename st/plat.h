/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * plat.h - the file calls the shared code takes from the app: futil.c's
 * whole-file load and safe save, which the Font dialog (common/ui/
 * fontsel.c) keeps CLAUDEST.FNT with. plat_tos.c has them on GEMDOS.
 */
#ifndef CST_PLAT_H
#define CST_PLAT_H

#define PF_READ  0
#define PF_WRITE 1

int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_size(const char *path);
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
long pf_handle_size(int h);

/* common/src/futil.c */
char *pf_load(const char *path, long *len);
extern long (*pf_size_open)(int h);
int  pf_save(const char *path, const void *data, long len);

#endif
