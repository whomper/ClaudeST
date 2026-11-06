/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * atari/libc.c's qsort on the host: for element sizes 1 to 33 (odd ones
 * too, and pointers), lengths 0 to 3000, keys with many equal ones, in
 * order, reversed and at random: the keys come out in the host qsort's
 * order, and whole elements as a stable insertion sort leaves them (equal
 * keys keep their order). The same with malloc refused (the heap sort:
 * the keys' order only, it isn't stable).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int refuse;			/* the next mallocs give NULL */

static void *t_malloc(size_t n)
{
	return refuse ? NULL : malloc(n);
}

#define qsort aera_qsort
#define malloc t_malloc
#include "build/qsort.c"
#undef malloc
#undef qsort

static size_t esize;

/* the key: an element's first byte (its first pointer's target's, for
   pointers); the rest says where it was */
static int cmp_key(const void *a, const void *b)
{
	return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

static int cmp_ptr(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void stable_ref(unsigned char *v, size_t n)
{
	unsigned char *t = malloc(esize);
	size_t i, j;

	for (i = 1; i < n; i++) {
		memcpy(t, v + i * esize, esize);
		for (j = i; j > 0 && v[(j - 1) * esize] > t[0]; j--)
			memcpy(v + j * esize, v + (j - 1) * esize, esize);
		memcpy(v + j * esize, t, esize);
	}
	free(t);
}

static int fails;

static void one(size_t n, size_t sz, int kind, unsigned seed)
{
	unsigned char *v = malloc(n * sz + 1), *ref = malloc(n * sz + 1), *host = malloc(n * sz + 1);
	size_t i, k;
	int heap;

	esize = sz;
	srand(seed);
	for (i = 0; i < n; i++) {
		unsigned char key = kind == 0 ? (unsigned char)(i * 7 / (n + 1)) :
				    kind == 1 ? (unsigned char)(255 - i * 200 / (n + 1)) :
				    (unsigned char)(rand() % (kind == 2 ? 5 : 256));
		v[i * sz] = key;
		for (k = 1; k < sz; k++)
			v[i * sz + k] = (unsigned char)((i >> (8 * ((k - 1) % 4))) ^ (k > 4 ? k : 0));
	}
	for (heap = 0; heap < 2; heap++) {
		unsigned char *w = malloc(n * sz + 1);

		memcpy(w, v, n * sz);
		memcpy(ref, v, n * sz);
		memcpy(host, v, n * sz);
		stable_ref(ref, n);
		qsort(host, n, sz, cmp_key);
		refuse = heap;
		aera_qsort(w, n, sz, cmp_key);
		refuse = 0;
		for (i = 0; i < n; i++)
			if (w[i * sz] != host[i * sz]) {
				printf("qsort: FAIL n %zu size %zu kind %d %s: key %zu\n", n, sz, kind, heap ? "heap" : "merge", i);
				fails++;
				break;
			}
		if (!heap && n && memcmp(w, ref, n * sz)) {
			printf("qsort: FAIL n %zu size %zu kind %d: not stable\n", n, sz, kind);
			fails++;
		}
		free(w);
	}
	free(v);
	free(ref);
	free(host);
}

static void pointers(size_t n, unsigned seed)
{
	char **p = malloc(n * sizeof *p + 1), **h = malloc(n * sizeof *p + 1), *names = malloc(n * 8 + 1);
	size_t i;
	int heap;

	srand(seed);
	for (i = 0; i < n; i++) {
		snprintf(names + i * 8, 8, "%06d", rand() % 1000);
		p[i] = names + i * 8;
	}
	for (heap = 0; heap < 2; heap++) {
		char **w = malloc(n * sizeof *w + 1);

		memcpy(w, p, n * sizeof *p);
		memcpy(h, p, n * sizeof *p);
		qsort(h, n, sizeof *h, cmp_ptr);
		refuse = heap;
		aera_qsort(w, n, sizeof *w, cmp_ptr);
		refuse = 0;
		for (i = 0; i < n; i++)
			if (strcmp(w[i], h[i])) {
				printf("qsort: FAIL pointers n %zu %s at %zu\n", n, heap ? "heap" : "merge", i);
				fails++;
				break;
			}
		/* stable: equal names keep their order (their addresses rise) */
		for (i = 1; !heap && i < n; i++)
			if (!strcmp(w[i - 1], w[i]) && w[i - 1] > w[i]) {
				printf("qsort: FAIL pointers n %zu: not stable\n", n);
				fails++;
				break;
			}
		free(w);
	}
	free(p);
	free(h);
	free(names);
}

int main(void)
{
	static const size_t lens[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 64, 100, 257, 1000, 3000 };
	size_t li, sz;
	int kind, checks = 0;

	for (li = 0; li < sizeof lens / sizeof lens[0]; li++) {
		for (sz = 1; sz <= 33; sz++)
			for (kind = 0; kind < 4; kind++, checks++)
				one(lens[li], sz, kind, (unsigned)(li * 131 + sz * 7 + kind));
		pointers(lens[li], (unsigned)li);
		checks++;
	}
	printf("qsort: %d cases, %s\n", checks, fails ? "FAILED" : "ok");
	return fails != 0;
}
