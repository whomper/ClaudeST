/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dirlist.c - a folder's entries for the family's file dialog (dirlist.h).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dirlist.h"

/* the names' memory: chunks that stay where they are while the folder grows (a name is pointed to) */
#define POOL_CHUNK 2048
struct dl_pool {
	struct dl_pool *next;
	int used;
	char b[POOL_CHUNK];
};

static void pool_free(DLIST *d)
{
	while (d->pool) {
		struct dl_pool *n = d->pool->next;
		free(d->pool);
		d->pool = n;
	}
}

static char *pool_name(DLIST *d, const char *name)
{
	size_t n = strlen(name) + 1;
	char *p;
	if (!d->pool || POOL_CHUNK - d->pool->used < (int)n) {
		struct dl_pool *c = malloc(sizeof(*c));
		if (!c)
			return 0;
		c->next = d->pool;
		c->used = 0;
		d->pool = c;
	}
	p = d->pool->b + d->pool->used;
	memcpy(p, name, n);
	d->pool->used += (int)n;
	return p;
}

static const DL_TYPE all_files[1] = { { "All files", "" } };

void dl_init(DLIST *d)
{
	memset(d, 0, sizeof(*d));
	d->types = all_files;
	d->ntypes = 1;
	d->type = 0;
	d->sort = DS_NAME;
}

void dl_types(DLIST *d, const DL_TYPE *t, int n)
{
	if (t && n > 0) {
		d->types = t;
		d->ntypes = n > DL_TYPES ? DL_TYPES : n;
	} else {
		d->types = all_files;
		d->ntypes = 1;
	}
	d->type = 0;
}

void dl_free(DLIST *d)
{
	pool_free(d);
	free(d->e);
	free(d->view);
	d->e = 0;
	d->view = 0;
	d->n = d->cap = d->nview = 0;
}

void dl_clear(DLIST *d)
{
	pool_free(d);
	d->n = d->nview = 0;
}

static int up(int c)
{
	return c >= 'a' && c <= 'z' ? c - 32 : c;
}

int dl_add(DLIST *d, const char *name, long size, unsigned short date, unsigned short time, int dir)
{
	DENT *x;
	char *nm;
	if (!strcmp(name, ".") || !strcmp(name, "..") || strlen(name) >= DL_NAME)
		return 1;
	if (d->n >= d->cap) {
		int cap = d->cap ? d->cap * 2 : 64;
		DENT *e = malloc((size_t)cap * sizeof(DENT));
		int *v = malloc((size_t)cap * sizeof(int));
		if (!e || !v) {
			free(e);
			free(v);
			return 0;
		}
		if (d->n)
			memcpy(e, d->e, (size_t)d->n * sizeof(DENT));
		free(d->e);
		free(d->view);
		d->e = e;
		d->view = v;
		d->cap = cap;
	}
	nm = pool_name(d, name);
	if (!nm)
		return 0;
	x = &d->e[d->n++];
	memset(x, 0, sizeof(*x));
	x->name = nm;
	x->size = size;
	x->date = date;
	x->time = time;
	x->dir = dir != 0;
	return 1;
}

static int namecmp(const char *a, const char *b)
{
	for (; *a && up(*a) == up(*b); a++, b++)
		;
	return up(*a) - up(*b);
}

static const char *ext(const char *n)
{
	const char *d = strrchr(n, '.');
	return d ? d + 1 : "";
}

static int sort_by;
static int cmp(const void *pa, const void *pb)
{
	const DENT *a = pa, *b = pb;
	long k = 0;
	if (a->dir != b->dir)
		return a->dir ? -1 : 1;	/* folders first */
	if (!a->dir)
		switch (sort_by) {
		case DS_DATE:		/* the newest first, as Selectric */
			k = ((long)b->date << 16 | b->time) - ((long)a->date << 16 | a->time);
			break;
		case DS_SIZE:		/* the biggest first */
			k = b->size > a->size ? 1 : b->size < a->size ? -1 : 0;
			break;
		case DS_TYPE:
			k = namecmp(ext(a->name), ext(b->name));
			break;
		}
	if (k)
		return k > 0 ? 1 : -1;
	return namecmp(a->name, b->name);
}

void dl_sort(DLIST *d)
{
	sort_by = d->sort;
	if (d->n > 1)
		qsort(d->e, (size_t)d->n, sizeof(DENT), cmp);
	dl_filter(d);
}

int dl_match(const char *m, const char *s)
{
	/* a long name (MiNT's, up to 255) and a mask with several stars: the usual walk with the last star's
	   place kept, not a backtracking recursion (the time is name times mask at worst) */
	const char *sm = 0, *ss = 0;
	if (!*m)
		return !*s;
	for (;;) {
		if (*m == '*') {
			sm = ++m;
			ss = s;
			continue;
		}
		if (*s && (*m == '?' || (*m && up(*m) == up(*s)))) {
			m++;
			s++;
			continue;
		}
		if (!*s && !*m)
			return 1;
		if (sm && *ss) {
			s = ++ss;
			m = sm;
			continue;
		}
		return 0;
	}
}

/* name's extension one of exts' (a space between each) */
static int ext_in(const char *name, const char *exts)
{
	const char *e = ext(name);
	while (*exts) {
		size_t n;
		while (*exts == ' ')
			exts++;
		for (n = 0; exts[n] && exts[n] != ' '; n++)
			;
		if (n) {
			size_t k;
			for (k = 0; k < n && e[k] && up(e[k]) == up(exts[k]); k++)
				;
			if (k == n && !e[k])
				return 1;
		}
		exts += n;
	}
	return 0;
}

int dl_type_ok(const DLIST *d, int type, const char *name)
{
	const char *x;
	if (type < 0 || type >= d->ntypes)
		return 1;
	x = d->types[type].exts;
	return !x || !*x || ext_in(name, x);
}

const char *dl_type_name(const DLIST *d, int type)
{
	return type >= 0 && type < d->ntypes ? d->types[type].name : "?";
}

const char *dl_sort_name(int sort)
{
	static const char *const n[DS_N] = { "By name", "By date", "By size", "By type" };
	return sort >= 0 && sort < DS_N ? n[sort] : "?";
}

void dl_filter(DLIST *d)
{
	int i;
	int any = !d->mask[0] || !strcmp(d->mask, "*") || !strcmp(d->mask, "*.*");
	d->nview = 0;
	for (i = 0; i < d->n; i++) {
		DENT *e = &d->e[i];
		if (e->dir || (dl_type_ok(d, d->type, e->name) && (any || dl_match(d->mask, e->name))))
			d->view[d->nview++] = i;
		else
			e->sel = 0;	/* (out of sight: not added) */
	}
}

DENT *dl_at(DLIST *d, int row)
{
	return row >= 0 && row < d->nview ? &d->e[d->view[row]] : 0;
}

void dl_select_all(DLIST *d, int on)
{
	int r;
	for (r = 0; r < d->nview; r++) {
		DENT *e = dl_at(d, r);
		e->sel = on && !e->dir;
	}
}

void dl_select_only(DLIST *d, int row)
{
	DENT *e = dl_at(d, row);
	dl_select_all(d, 0);
	if (e)
		e->sel = 1;
}

void dl_toggle(DLIST *d, int row)
{
	DENT *e = dl_at(d, row);
	if (e)
		e->sel = !e->sel;
}

void dl_select_range(DLIST *d, int a, int b)
{
	int r;
	if (a > b) {
		r = a;
		a = b;
		b = r;
	}
	dl_select_all(d, 0);
	for (r = a; r <= b; r++) {
		DENT *e = dl_at(d, r);
		if (e)
			e->sel = 1;
	}
}

int dl_selected_dirs(const DLIST *d)
{
	int r, k = 0;
	for (r = 0; r < d->nview; r++)
		k += d->e[d->view[r]].sel && d->e[d->view[r]].dir;
	return k;
}

int dl_selected(const DLIST *d, long *bytes)
{
	int r, k = 0;
	long b = 0;
	for (r = 0; r < d->nview; r++) {
		const DENT *e = &d->e[d->view[r]];
		if (e->sel) {
			k++;
			if (!e->dir)
				b += e->size;
		}
	}
	if (bytes)
		*bytes = b;
	return k;
}

void dl_size_text(long size, char *out)
{
	if (size < 1024)
		sprintf(out, "%ld", size);
	else if (size < 1024L * 1024)
		sprintf(out, "%ldK", (size + 512) / 1024);
	else if (size < 10L * 1024 * 1024) {
		long t = (size * 10 + 512L * 1024) / (1024L * 1024);	/* tenths of a MB */
		sprintf(out, "%ld.%ldM", t / 10, t % 10);
	} else
		sprintf(out, "%ldM", (size + 512L * 1024) / (1024L * 1024));
}

void dl_date_text(unsigned short date, char *out)
{
	sprintf(out, "%02u.%02u.%02u", date & 31, (date >> 5) & 15, (unsigned)((date >> 9) + 80) % 100);
}

int dl_folder_of(const char *path, char *dir, size_t n)
{
	const char *b = strrchr(path, '\\');
	size_t len;
	if (!b)
		return 0;
	len = (size_t)(b - path) + 1;
	if (len >= n)
		return 0;
	memcpy(dir, path, len);
	dir[len] = 0;
	return 1;
}

int dl_join(const char *dir, const char *name, char *out, size_t n)
{
	size_t a = strlen(dir), b = strlen(name);
	if (a + b >= n)
		return 0;
	memcpy(out, dir, a);
	memcpy(out + a, name, b + 1);
	return 1;
}

void dl_recent_front(char *recent, size_t stride, int n, const char *dir)
{
	int k;
	for (k = 0; k < n - 1 && strcmp(recent + (size_t)k * stride, dir); k++)
		;
	for (; k > 0; k--)
		strcpy(recent + (size_t)k * stride, recent + (size_t)(k - 1) * stride);
	strcpy(recent, dir);
}
