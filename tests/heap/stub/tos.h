/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tos.h for tests/heap: the little of TOS the heap in atari/libc.c uses,
 * on the build machine. GEMDOS's Mxalloc and Malloc are the test's own
 * (test_heap.c), and the heap's functions get names of their own, so the
 * test still has the system's malloc.
 */
#ifndef TOS_H
#define TOS_H
#include <stddef.h>
#include <stdint.h>

typedef unsigned char u8;
typedef uint32_t u32;

void *Mxalloc(long n, int mode);
void *Malloc(long n);

#define malloc  heap_malloc
#define free    heap_free
#define calloc  heap_calloc
#define realloc heap_realloc

void *heap_malloc(size_t n);
void heap_free(void *p);
void *heap_calloc(size_t n, size_t m);
void *heap_realloc(void *p, size_t n);
void heap_add(void *p, size_t bytes);
long heap_check(long *nfree);
unsigned heap_bin(size_t s);
extern long heap_tt, heap_st;
#endif
