/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* plat.h for SETDEMO.PRG: the two file calls fontsel.c makes (NAME.FNT, which the demo has none of) */
#ifndef PLAT_H
#define PLAT_H
char *pf_load(const char *path, long *len);
int pf_save(const char *path, const void *buf, long len);
#endif
