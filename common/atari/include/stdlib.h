/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* AERA freestanding libc: memory and conversions */
#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void free(void *p);
void heap_add(void *p, size_t bytes);	/* a fixed heap (accessories) */
/* the heap's bytes in use (whole blocks), and the most since the app last
   set heap_peak (to heap_use, say): what its data really needs */
extern long heap_use, heap_peak;
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int abs(int v);
long labs(long v);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
void exit(int code) __attribute__((noreturn));
#endif
