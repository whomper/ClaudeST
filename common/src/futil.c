/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * futil.c - whole-file helpers on top of plat.h.
 */
#include <stdlib.h>
#include <string.h>
#include "plat.h"

char *pf_load(const char *path, long *len)
{
	long size = pf_size(path), got = 0, r;
	char *buf;
	int h;
	if (len)
		*len = 0;
	if (size < 0)
		return 0;
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	buf = malloc(size + 1);
	if (!buf) {
		pf_close(h);
		return 0;
	}
	while (got < size && (r = pf_read(h, buf + got, size - got)) > 0)
		got += r;
	pf_close(h);
	buf[got] = 0;
	if (len)
		*len = got;
	return buf;
}

int pf_save(const char *path, const void *data, long len)
{
	int h = pf_open(path, PF_WRITE);
	long r;
	if (h < 0)
		return h;
	r = pf_write(h, data, len);
	pf_close(h);
	return r == len ? 0 : -1;
}
