/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * libc.c - the subset of the C library the AERA programs use on the Atari:
 * strings, a heap on top of GEMDOS Malloc/Mxalloc, number parsing,
 * qsort and a small vsnprintf. On the host build the system libc is
 * used instead, so the mail core is the same code on both.
 */
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "tos.h"

/* ---------------- memory and strings ---------------- */

int mem_unaligned_ok;		/* a 68020 or later: longs at any address */

void *memcpy(void *d, const void *s, size_t n)
{
	u8 *dp = d;
	const u8 *sp = s;
	/* long copies when both are even, or always on a 68020 and later
	   (the 68000 faults on an odd address; text is usually odd on one side) */
	if (mem_unaligned_ok || !(((u32)dp | (u32)sp) & 1)) {
		while (n >= 4) {
			*(u32 *)dp = *(const u32 *)sp;
			dp += 4;
			sp += 4;
			n -= 4;
		}
	}
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	u8 *dp = d;
	const u8 *sp = s;
	if (dp <= sp || dp >= sp + n)
		return memcpy(d, s, n);
	dp += n;
	sp += n;
	while (n--)
		*--dp = *--sp;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	u8 *dp = d;
	/* long words from an even address: a 68000 can */
	if ((u32)dp & 1 && n) {
		*dp++ = (u8)c;
		n--;
	}
	if (n >= 8) {
		u32 v = (u8)c;
		v |= v << 8;
		v |= v << 16;
		while (n >= 4) {
			*(u32 *)dp = v;
			dp += 4;
			n -= 4;
		}
	}
	while (n--)
		*dp++ = (u8)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const u8 *ap = a, *bp = b;
	for (; n; n--, ap++, bp++)
		if (*ap != *bp)
			return *ap - *bp;
	return 0;
}

void *memchr(const void *s, int c, size_t n)
{
	const u8 *p = s;
	for (; n; n--, p++)
		if (*p == (u8)c)
			return (void *)p;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++))
		;
	return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
	char *r = d;
	while (n && *s) {
		*d++ = *s++;
		n--;
	}
	while (n--)
		*d++ = 0;
	return r;
}

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strncat(char *d, const char *s, size_t n)
{
	char *p = d + strlen(d);
	while (n-- && *s)
		*p++ = *s++;
	*p = 0;
	return d;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (u8)*a - (u8)*b;
		if (!*a)
			return 0;
	}
	return 0;
}

int strcasecmp(const char *a, const char *b)
{
	int ca, cb;
	do {
		ca = tolower((u8)*a++);
		cb = tolower((u8)*b++);
	} while (ca && ca == cb);
	return ca - cb;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	int ca, cb;
	for (; n; n--) {
		ca = tolower((u8)*a++);
		cb = tolower((u8)*b++);
		if (ca != cb)
			return ca - cb;
		if (!ca)
			return 0;
	}
	return 0;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return 0;
	}
}

char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++) {
		if (*s == (char)c)
			r = s;
		if (!*s)
			return (char *)r;
	}
}

char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	if (!l)
		return (char *)h;
	for (; *h; h++)
		if (*h == *n && !strncmp(h, n, l))
			return (char *)h;
	return 0;
}

size_t strspn(const char *s, const char *a)
{
	const char *p = s;
	while (*p && strchr(a, *p))
		p++;
	return p - s;
}

size_t strcspn(const char *s, const char *r)
{
	const char *p = s;
	while (*p && !strchr(r, *p))
		p++;
	return p - s;
}

char *strpbrk(const char *s, const char *a)
{
	for (; *s; s++)
		if (strchr(a, *s))
			return (char *)s;
	return 0;
}

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

/* ---------------- heap ----------------
 * A first-fit free list (K&R) over blocks taken from GEMDOS. Mxalloc
 * mode 3 prefers TT/Fast RAM on a TT or accelerated Falcon; plain
 * TOS 1.x lacks Mxalloc and gets Malloc. */

typedef union hdr {
	struct {
		union hdr *next;
		size_t units;
	} s;
	long align[2];
} HDR;

static HDR base;
static HDR *freep;

/* an accessory can't take memory from GEMDOS once it runs: under plain
   TOS it would belong to whatever program is in front, and go when that
   one quits. So it takes one block at start-up and lives in it. */
static int heap_fixed;

void heap_add(void *p, size_t bytes)
{
	HDR *h = p;
	if (!freep) {
		base.s.next = freep = &base;
		base.s.units = 0;
	}
	h->s.units = bytes / sizeof(HDR);
	free(h + 1);
	heap_fixed = 1;
}

#define MAX_BLOCKS 40		/* TOS's pool is shared with every program and folder */

long heap_tt, heap_st;		/* bytes the heap took from TT/Fast and ST RAM (About) */

static HDR *more_core(size_t units)
{
	static short blocks;		/* GEMDOS blocks taken, a pool entry each */
	static size_t taken;		/* units taken from GEMDOS so far */
	size_t want = units, bytes;
	HDR *h = 0;
	if (heap_fixed)
		return 0;
	/* Every GEMDOS block takes an entry in TOS's small internal pool, and
	   when that runs out TOS halts the machine ("OUT OF INTERNAL MEMORY").
	   So few blocks: 512 KB at first, then each as big as all before it,
	   so the heap doubles and 16 MB takes six. Less if that much isn't
	   free; at worst exactly what is needed. */
	if (want < 65536L)
		want = 65536L;
	if (want < taken)
		want = taken;
	if (blocks >= MAX_BLOCKS)
		return 0;		/* "out of memory" beats a halted machine */
	while (!h) {
		bytes = want * sizeof(HDR);
		h = Mxalloc(bytes, 3);
		if ((long)h == -32 || (long)h <= 0)
			h = Malloc(bytes);
		if ((long)h <= 0) {
			h = 0;
			if (want == units)
				return 0;
			want = want / 2 > units ? want / 2 : units;
		}
	}
	taken += want;
	blocks++;
	if ((unsigned long)h >= 0x01000000UL)	/* above the ST's 16 MB */
		heap_tt += bytes;
	else
		heap_st += bytes;
	h->s.units = want;
	free(h + 1);
	return freep;
}

void *malloc(size_t n)
{
	HDR *p, *prev;
	size_t units = (n + sizeof(HDR) - 1) / sizeof(HDR) + 1;

	if (!freep) {
		base.s.next = freep = &base;
		base.s.units = 0;
	}
	prev = freep;
	for (p = prev->s.next;; prev = p, p = p->s.next) {
		if (p->s.units >= units) {
			if (p->s.units == units) {
				prev->s.next = p->s.next;
			} else {
				p->s.units -= units;
				p += p->s.units;
				p->s.units = units;
			}
			freep = prev;
			return p + 1;
		}
		if (p == freep && !(p = more_core(units)))
			return 0;
	}
}

void free(void *ptr)
{
	HDR *b, *p;
	if (!ptr)
		return;
	b = (HDR *)ptr - 1;
	for (p = freep; !(b > p && b < p->s.next); p = p->s.next)
		if (p >= p->s.next && (b > p || b < p->s.next))
			break;
	if (b + b->s.units == p->s.next) {
		b->s.units += p->s.next->s.units;
		b->s.next = p->s.next->s.next;
	} else {
		b->s.next = p->s.next;
	}
	if (p + p->s.units == b) {
		p->s.units += b->s.units;
		p->s.next = b->s.next;
	} else {
		p->s.next = b;
	}
	freep = p;
}

void *calloc(size_t n, size_t m)
{
	void *p = malloc(n * m);
	if (p)
		memset(p, 0, n * m);
	return p;
}

void *realloc(void *ptr, size_t n)
{
	HDR *h;
	size_t have;
	void *np;
	if (!ptr)
		return malloc(n);
	h = (HDR *)ptr - 1;
	have = (h->s.units - 1) * sizeof(HDR);
	if (have >= n)
		return ptr;
	np = malloc(n);
	if (!np)
		return 0;
	memcpy(np, ptr, have);
	free(ptr);
	return np;
}

/* ---------------- numbers ---------------- */

unsigned long strtoul(const char *s, char **end, int base_)
{
	unsigned long v = 0;
	while (isspace((u8)*s))
		s++;
	if (*s == '+')
		s++;
	if ((base_ == 0 || base_ == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base_ = 16;
	} else if (base_ == 0) {
		base_ = 10;
	}
	for (;; s++) {
		int d;
		if (isdigit((u8)*s))
			d = *s - '0';
		else if (isalpha((u8)*s))
			d = tolower((u8)*s) - 'a' + 10;
		else
			break;
		if (d >= base_)
			break;
		v = v * base_ + d;
	}
	if (end)
		*end = (char *)s;
	return v;
}

long strtol(const char *s, char **end, int base_)
{
	int neg = 0;
	while (isspace((u8)*s))
		s++;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	return neg ? -(long)strtoul(s, end, base_) : (long)strtoul(s, end, base_);
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }

/* heap sort: no recursion, no extra memory, and n log n even for the
   list of thousands of contacts sorted again after every change (the
   shell sort it replaces went quadratic there). Pointers (the usual
   case) are swapped as longs */
static void swap_el(char *x, char *y, size_t size)
{
	if (size == sizeof(long) && !(((unsigned long)x | (unsigned long)y) & 1)) {
		long t = *(long *)x;
		*(long *)x = *(long *)y;
		*(long *)y = t;
	} else {
		while (size--) {
			char t = *x;
			*x++ = *y;
			*y++ = t;
		}
	}
}

static void sift(char *b, size_t root, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1;
		if (child >= n)
			return;
		if (child + 1 < n && cmp(b + child * size, b + (child + 1) * size) < 0)
			child++;
		if (cmp(b + root * size, b + child * size) >= 0)
			return;
		swap_el(b + root * size, b + child * size, size);
		root = child;
	}
}

/* the same for arrays of pointers, indexed without a multiply (a 68000
   multiplies 32-bit numbers in software) */
static void sift_ptr(void **a, size_t root, size_t n, int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1;
		void *t;
		if (child >= n)
			return;
		if (child + 1 < n && cmp(&a[child], &a[child + 1]) < 0)
			child++;
		if (cmp(&a[root], &a[child]) >= 0)
			return;
		t = a[root];
		a[root] = a[child];
		a[child] = t;
		root = child;
	}
}

void qsort(void *base_, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
	char *b = base_;
	size_t i;
	if (n < 2)
		return;
	if (size == sizeof(void *) && !((unsigned long)b & 1)) {
		void **a = base_, *t;
		for (i = n / 2; i-- > 0;)
			sift_ptr(a, i, n, cmp);
		for (i = n - 1; i > 0; i--) {
			t = a[0];
			a[0] = a[i];
			a[i] = t;
			sift_ptr(a, 0, i, cmp);
		}
		return;
	}
	for (i = n / 2; i-- > 0;)
		sift(b, i, n, size, cmp);
	for (i = n - 1; i > 0; i--) {
		swap_el(b, b + i * size, size);
		sift(b, 0, i, size, cmp);
	}
}

void exit(int code)
{
	trap1_ww(0x4c, (short)code);
	for (;;)
		;
}

/* ---------------- vsnprintf ---------------- */

typedef struct {
	char *p;
	size_t left;
	int count;
} OUT;

static void put(OUT *o, char c)
{
	if (o->left > 1) {
		*o->p++ = c;
		o->left--;
	}
	o->count++;
}

static void put_num(OUT *o, unsigned long v, int base_, int upper, int neg,
		    int width, int zero, int left)
{
	char tmp[12];
	int n = 0, len;
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	do {
		tmp[n++] = dig[v % base_];
		v /= base_;
	} while (v);
	len = n + neg;
	if (neg && zero)
		put(o, '-');
	if (!left)
		while (width-- > len)
			put(o, zero ? '0' : ' ');
	if (neg && !zero)
		put(o, '-');
	while (n)
		put(o, tmp[--n]);
	if (left)
		while (width-- > len)
			put(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	OUT o;
	o.p = buf;
	o.left = size;
	o.count = 0;

	for (; *fmt; fmt++) {
		int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		fmt++;
		for (;; fmt++) {
			if (*fmt == '-')
				left = 1;
			else if (*fmt == '0')
				zero = 1;
			else
				break;
		}
		if (*fmt == '*') {
			width = va_arg(ap, int);
			fmt++;
		} else {
			while (isdigit((u8)*fmt))
				width = width * 10 + *fmt++ - '0';
		}
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			} else {
				while (isdigit((u8)*fmt))
					prec = prec * 10 + *fmt++ - '0';
			}
		}
		while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') {
			lng = *fmt != 'h';
			fmt++;
		}
		(void)lng;	/* int and long are both 32 bits */
		switch (*fmt) {
		case 'd':
		case 'i': {
			long v = va_arg(ap, long);
			put_num(&o, v < 0 ? -(unsigned long)v : (unsigned long)v, 10, 0, v < 0, width, zero, left);
			break;
		}
		case 'u':
			put_num(&o, va_arg(ap, unsigned long), 10, 0, 0, width, zero, left);
			break;
		case 'x':
		case 'X':
			put_num(&o, va_arg(ap, unsigned long), 16, *fmt == 'X', 0, width, zero, left);
			break;
		case 'p':
			put(&o, '0');
			put(&o, 'x');
			put_num(&o, (unsigned long)va_arg(ap, void *), 16, 0, 0, 8, 1, 0);
			break;
		case 'c':
			put(&o, (char)va_arg(ap, int));
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len;
			if (!s)
				s = "(null)";
			for (len = 0; s[len] && (prec < 0 || len < prec); len++)
				;
			if (!left)
				while (width-- > len)
					put(&o, ' ');
			while (len--)
				put(&o, *s++), width--;
			if (left)
				while (width-- > 0)
					put(&o, ' ');
			break;
		}
		case '%':
			put(&o, '%');
			break;
		case 0:
			fmt--;
			break;
		default:
			put(&o, '%');
			put(&o, *fmt);
		}
	}
	if (size)
		*o.p = 0;
	return o.count;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = vsnprintf(buf, 0x7fffffff, fmt, ap);
	va_end(ap);
	return r;
}
