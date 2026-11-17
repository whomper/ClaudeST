/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* libc.c - the bits of a C library the ColdFire tests need, on rt.S's
 * Linux calls; built with -mcpu=5475 as ColdFire code */
#include <stddef.h>
#include "rt.h"

void *memcpy(void *d, const void *s, size_t n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d; const unsigned char *b = s;
	if (a < b) while (n--) *a++ = *b++;
	else { a += n; b += n; while (n--) *--a = *--b; }
	return d;
}
void *memset(void *d, int c, size_t n) { unsigned char *a = d; while (n--) *a++ = (unsigned char)c; return d; }
int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
	return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (unsigned char)*a - (unsigned char)*b; }

void out(const char *s) { sys_write(1, s, (long)strlen(s)); }

void out_num(unsigned long v)
{
	char b[12];
	int i = 11;
	b[i] = 0;
	do b[--i] = (char)('0' + v % 10); while ((v /= 10) != 0);
	out(b + i);
}

void out_hex(unsigned long v)
{
	char b[9];
	int i;
	for (i = 7; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15];
	b[8] = 0;
	out(b);
}

/* a whole file into a static buffer: its length, or -1 */
long read_file(const char *path, unsigned char *buf, long cap)
{
	long fd = sys_open(path, 0, 0), n = 0, r;
	if (fd < 0)
		return -1;
	while (n < cap && (r = sys_read(fd, buf + n, cap - n)) > 0)
		n += r;
	sys_close(fd);
	return n;
}

/* a small heap for the code under test: blocks with their size before
   them, freed ones kept on a list and taken again first fit (the same
   few sizes come back stream after stream) */
static unsigned char heap[24 << 20];
static long heap_at;
typedef struct blk { size_t size; struct blk *next; } BLK;
static BLK *freed;
void *malloc(size_t n)
{
	BLK **pp, *b;
	n = (n + 7) & ~(size_t)7;
	for (pp = &freed; (b = *pp) != NULL; pp = &b->next)
		if (b->size >= n) {
			*pp = b->next;
			return b + 1;
		}
	if (heap_at + (long)(n + sizeof(BLK)) > (long)sizeof heap)
		return NULL;
	b = (BLK *)(heap + heap_at);
	heap_at += (long)(n + sizeof(BLK));
	b->size = n;
	return b + 1;
}
void *calloc(size_t a, size_t b) { void *p = malloc(a * b); if (p) memset(p, 0, a * b); return p; }
void free(void *p)
{
	BLK *b;
	if (p == NULL)
		return;
	b = (BLK *)p - 1;
	b->next = freed;
	freed = b;
}
void *realloc(void *p, size_t n)
{
	BLK *b;
	void *q;
	if (p == NULL)
		return malloc(n);
	b = (BLK *)p - 1;
	if (b->size >= n)
		return p;
	if ((q = malloc(n)) != NULL) {
		memcpy(q, p, b->size);
		free(p);
	}
	return q;
}

/* util.c's growing buffer (inflate.c's codes use it), without its stdio */
#include "util.h"
static int grow(SBUF *b, long n)
{
	char *p;
	long cap;
	if (n < 0)
		return 0;
	if (b->len + n + 1 <= b->cap)
		return 1;
	cap = b->cap ? b->cap : 64;
	while (cap < b->len + n + 1)
		cap *= 2;
	if ((p = malloc((size_t)cap)) == NULL)
		return 0;
	if (b->s != NULL) {
		memcpy(p, b->s, (size_t)b->len + 1);
		free(b->s);
	}
	b->s = p;
	b->cap = cap;
	return 1;
}
int sb_room(SBUF *b, long n) { return grow(b, n); }
int sb_add(SBUF *b, const char *s, long n)
{
	if (!grow(b, n))
		return 0;
	memcpy(b->s + b->len, s, (size_t)n);
	b->len += n;
	b->s[b->len] = 0;
	return 1;
}
