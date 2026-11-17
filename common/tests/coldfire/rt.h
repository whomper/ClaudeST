/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
#ifndef CF_RT_H
#define CF_RT_H
#include <stddef.h>
long sys_write(long fd, const void *p, long n);
long sys_read(long fd, void *p, long n);
long sys_open(const char *path, long flags, long mode);
long sys_close(long fd);
long sys_call4(long nr, long a, long b, long c, long d);
void sys_exit(long code) __attribute__((noreturn));
void out(const char *s);
void out_num(unsigned long v);
void out_hex(unsigned long v);
long read_file(const char *path, unsigned char *buf, long cap);
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
void *malloc(size_t n);
void *calloc(size_t a, size_t b);
void free(void *p);
void *realloc(void *p, size_t n);
#endif
