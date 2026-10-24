/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ovl.c - load a file in TOS's program format into memory and relocate
 * it (ovl.h): TLS.OVL's loader. The header gives the sizes; text and
 * data are read in 16 KB pieces, then the fixups, a long that is the
 * first one's offset (0: none) and a byte for each next one: the step
 * to it, 1 for 254 further without a fixup, 0 at the end. Each fixup
 * adds where the image landed to the long at that offset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "ovl.h"

#define PIECE (16 * 1024L)
#define MOST  (4 * 1024 * 1024L)	/* no overlay is bigger: a damaged header */

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

void ovl_fix_start(OVL_FIX *f, unsigned char *base, long size)
{
	f->base = base;
	f->size = size;
	f->at = 0;
	f->started = 0;
}

static int fix(OVL_FIX *f)
{
	unsigned char *q;
	unsigned long v;
	if (f->at < 0 || f->at > f->size - 4 || (f->at & 1))
		return 0;
	q = f->base + f->at;
	v = be32(q) + (unsigned long)f->base;
	q[0] = (unsigned char)(v >> 24);
	q[1] = (unsigned char)(v >> 16);
	q[2] = (unsigned char)(v >> 8);
	q[3] = (unsigned char)v;
	return 1;
}

int ovl_fix_feed(OVL_FIX *f, const unsigned char *p, long n)
{
	while (n-- > 0) {
		unsigned c = *p++;
		if (f->started < 4) {		/* the first fixup's offset */
			f->at = (long)((unsigned long)f->at << 8 | c);
			if (++f->started < 4)
				continue;
			if (!f->at)
				return 2;	/* nothing to fix */
			if (!fix(f))
				return 0;
			continue;
		}
		if (!c)
			return 2;
		if (c == 1) {
			f->at += 254;
			continue;
		}
		if (c & 1)
			return 0;
		f->at += c;
		if (!fix(f))
			return 0;
	}
	return 1;
}

/* the file's name, for the messages */
static const char *name_of(const char *path)
{
	const char *s = path, *p;
	for (p = path; *p; p++)
		if (*p == '\\' || *p == '/' || *p == ':')
			s = p + 1;
	return s;
}

void *ovl_load(const char *path, long *taken, char *err, int errlen)
{
	unsigned char hdr[28], fx[512];
	unsigned char *m = 0;
	long tsize, dsize, bsize, ssize, done, n;
	OVL_FIX f;
	int h, r = 1;
	const char *name = name_of(path);

	*taken = 0;
	if ((h = pf_open(path, PF_READ)) < 0) {
		snprintf(err, errlen, "%s is missing", name);
		return 0;
	}
	if (pf_read(h, hdr, sizeof(hdr)) != (long)sizeof(hdr) || hdr[0] != 0x60 || hdr[1] != 0x1A)
		goto bad;
	tsize = (long)be32(hdr + 2);
	dsize = (long)be32(hdr + 6);
	bsize = (long)be32(hdr + 10);
	ssize = (long)be32(hdr + 14);
	if (tsize < 64 || dsize < 0 || bsize < 0 || ssize < 0 || tsize + dsize + bsize > MOST ||
	    (tsize | dsize) & 1)
		goto bad;
	if (!(m = malloc(tsize + dsize + bsize))) {
		pf_close(h);
		snprintf(err, errlen, "no memory for %s (%ld KB)", name, (tsize + dsize + bsize + 1023) / 1024);
		return 0;
	}
	for (done = 0; done < tsize + dsize; done += n) {
		n = tsize + dsize - done;
		if (n > PIECE)
			n = PIECE;
		if (pf_read(h, m + done, n) != n)
			goto bad;
	}
	if (ssize && pf_seek(h, (long)sizeof(hdr) + tsize + dsize + ssize) != (long)sizeof(hdr) + tsize + dsize + ssize)
		goto bad;
	ovl_fix_start(&f, m, tsize + dsize);
	while (r == 1) {
		if ((n = pf_read(h, fx, sizeof(fx))) <= 0 || !(r = ovl_fix_feed(&f, fx, n)))
			goto bad;
	}
	pf_close(h);
	memset(m + tsize + dsize, 0, bsize);
	*taken = tsize + dsize + bsize;
	if (pf_idle)		/* once the file is closed (the guidelines, 2.2) */
		pf_idle();
	return m;
bad:
	pf_close(h);
	free(m);
	snprintf(err, errlen, "%s is damaged: copy it again from the release", name);
	return 0;
}
