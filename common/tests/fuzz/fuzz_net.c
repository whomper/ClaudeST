/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fuzz_net.c - the network for an app's fuzz targets (see fuzz_net.h).
 */
#include <stdio.h>
#include <string.h>
#include "fuzz_net.h"

/* the parts of the input (FUZZ_NET_NEXT between them), and how far each
   connection has heard its own; without FUZZ_NET_NEXT, one part that every
   connection carries on with */
#define PARTS 8
#define CONNS 16

static const unsigned char *data;
static long size, piece;
static long part_at[PARTS], part_end[PARTS];
static int parts, shared, opened;
static long pos[CONNS], end[CONNS];
static long shared_pos;

static const unsigned char *find(const unsigned char *d, long n, const char *s, long sn)
{
	long i;
	for (i = 0; i + sn <= n; i++)
		if (!memcmp(d + i, s, sn))
			return d + i;
	return 0;
}

void fuzz_net_set(const unsigned char *d, long n)
{
	static const char next[] = FUZZ_NET_NEXT;
	long at = 0, sn = (long)sizeof(next) - 1;
	data = d;
	size = n;
	piece = 0;
	opened = 0;
	shared_pos = 0;
	for (parts = 0; parts < PARTS; parts++) {
		const unsigned char *e = parts < PARTS - 1 ? find(d + at, n - at, next, sn) : 0;
		part_at[parts] = at;
		part_end[parts] = e ? e - d : n;
		if (!e) {
			parts++;
			break;
		}
		at = e - d + sn;
	}
	shared = parts == 1;
}

long fuzz_net_left(void)
{
	long left = 0;
	int i;
	if (shared)
		return size - shared_pos;
	for (i = 0; i < opened && i < CONNS; i++)
		left += end[i] - pos[i];
	for (i = opened; i < parts; i++)
		left += part_end[i] - part_at[i];
	return left;
}

int net_init(void)
{
	return 1;
}

const char *net_stack(void)
{
	return "fuzz";
}

/* a new connection hears its part, or the rest of the one input; none
   once it is all said (or a program that reconnects would go on for
   ever) */
int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	int h = opened;
	(void)host;
	(void)port;
	if (shared ? shared_pos >= size : h >= parts || h >= CONNS) {
		snprintf(err, errlen, "connection refused");
		return -1;
	}
	if (!shared) {
		pos[h] = part_at[h];
		end[h] = part_end[h];
	}
	opened++;
	return h;
}

long net_write(int h, const void *buf, long n)
{
	(void)h;
	(void)buf;
	return n;
}

/* 1 to 97 bytes at a time, so every boundary in a line or a header is hit;
   closed (-1) at the end of the input */
long net_read(int h, void *buf, long max)
{
	long n, *p, e;
	if (shared) {
		p = &shared_pos;
		e = size;
	} else {
		if (h < 0 || h >= CONNS)
			return -1;
		p = &pos[h];
		e = end[h];
	}
	if (*p >= e)
		return -1;
	n = 1 + (piece++ * 37 + 11) % 97;
	if (n > max)
		n = max;
	if (n > e - *p)
		n = e - *p;
	memcpy(buf, data + *p, n);
	*p += n;
	return n;
}

void net_close(int h)
{
	(void)h;
}

void net_close_wait(int h, int seconds)
{
	(void)h;
	(void)seconds;
}
