/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ovl.c - load a file in TOS's program format into memory and relocate
 * it (ovl.h): TLS.OVL's loader. The header gives the sizes; text and
 * data are read in 16 KB pieces, then the fixups, a long that is the
 * first one's offset (0: none) and a byte for each next one: the step
 * to it, 1 for 254 further without a fixup, 0 at the end. Each fixup
 * adds where the image landed to the long at that offset. What follows
 * the fixups' end (EWEBJS.OVL's imports) goes to ovl_load_tail's tail.
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
	f->used = 0;
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
	const unsigned char *p0 = p;
	while (n-- > 0) {
		unsigned c = *p++;
		if (f->started < 4) {		/* the first fixup's offset */
			f->at = (long)((unsigned long)f->at << 8 | c);
			if (++f->started < 4)
				continue;
			if (!f->at) {
				f->used = (long)(p - p0);
				return 2;	/* nothing to fix */
			}
			if (!fix(f))
				return 0;
			continue;
		}
		if (!c) {
			f->used = (long)(p - p0);
			return 2;
		}
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

/* the header's sizes: text, data, BSS and symbols, from an open file;
   0 when it isn't one */
static int header(int h, long *tsize, long *dsize, long *bsize, long *ssize)
{
	unsigned char hdr[28];

	if (pf_read(h, hdr, sizeof(hdr)) != (long)sizeof(hdr) || hdr[0] != 0x60 || hdr[1] != 0x1A)
		return 0;
	*tsize = (long)be32(hdr + 2);
	*dsize = (long)be32(hdr + 6);
	*bsize = (long)be32(hdr + 10);
	*ssize = (long)be32(hdr + 14);
	return !(*tsize < 64 || *dsize < 0 || *bsize < 0 || *ssize < 0 || *tsize + *dsize + *bsize > MOST ||
		 (*tsize | *dsize) & 1);
}

long ovl_need(const char *path)
{
	long tsize, dsize, bsize, ssize;
	int h, ok;

	if ((h = pf_open(path, PF_READ)) < 0)
		return -1;
	ok = header(h, &tsize, &dsize, &bsize, &ssize);
	pf_close(h);
	return ok ? tsize + dsize + bsize : -1;
}

/* the one loader: into the block given (mem, size), else into one from
   malloc; align: that block 256 + 4 bytes longer, the image at its first
   256-byte boundary after a long that holds what malloc gave (ovl_free) */
static void *load(const char *path, void *mem, long size, long *taken, char *err, int errlen, OVL_TAIL tail, void *ud,
		  int *later, int align);

void *ovl_load(const char *path, long *taken, char *err, int errlen)
{
	return load(path, 0, 0, taken, err, errlen, 0, 0, 0, 0);
}

void *ovl_load_aligned(const char *path, long *taken, char *err, int errlen)
{
	return load(path, 0, 0, taken, err, errlen, 0, 0, 0, 1);
}

void ovl_free(void *image)
{
	if (image)
		free(((void **)image)[-1]);
}

void *ovl_load_tail(const char *path, long *taken, char *err, int errlen, OVL_TAIL tail, void *ud, int *later)
{
	return load(path, 0, 0, taken, err, errlen, tail, ud, later, 0);
}

void *ovl_load_into(const char *path, void *mem, long size, long *taken, char *err, int errlen)
{
	return load(path, mem, size, taken, err, errlen, 0, 0, 0, 0);
}

static void *load(const char *path, void *mem, long size, long *taken, char *err, int errlen, OVL_TAIL tail, void *ud,
		  int *later, int align)
{
	unsigned char fx[512];
	unsigned char *m = 0, *raw = 0;
	long tsize, dsize, bsize, ssize, done, n;
	OVL_FIX f;
	int h, r = 1;
	const char *name = name_of(path);

	*taken = 0;
	if (later)
		*later = 0;
	if ((h = pf_open(path, PF_READ)) < 0) {
		snprintf(err, errlen, "%s is missing", name);
		return 0;
	}
	if (!header(h, &tsize, &dsize, &bsize, &ssize))
		goto bad;
	if (align) {
		if ((raw = malloc(tsize + dsize + bsize + 256 + sizeof(void *))) != 0) {
			m = (unsigned char *)(((unsigned long)raw + sizeof(void *) + 255) & ~255UL);
			((void **)m)[-1] = raw;
		}
	} else
		m = mem ? (tsize + dsize + bsize <= size ? mem : 0) : malloc(tsize + dsize + bsize);
	if (!m) {
		pf_close(h);
		snprintf(err, errlen, "no memory for %s (%ld KB)", name, (tsize + dsize + bsize + 1023) / 1024);
		if (later)
			*later = 1;
		return 0;
	}
	for (done = 0; done < tsize + dsize; done += n) {
		n = tsize + dsize - done;
		if (n > PIECE)
			n = PIECE;
		if (pf_read(h, m + done, n) != n)
			goto bad;
	}
	if (ssize && pf_seek(h, 28L + tsize + dsize + ssize) != 28L + tsize + dsize + ssize)
		goto bad;
	ovl_fix_start(&f, m, tsize + dsize);
	while (r == 1) {
		if ((n = pf_read(h, fx, sizeof(fx))) <= 0 || !(r = ovl_fix_feed(&f, fx, n)))
			goto bad;
	}
	if (tail) {
		/* what came after the table in its last piece, then the rest */
		if (n > f.used && !tail(ud, m, fx + f.used, n - f.used))
			goto bad;
		while ((n = pf_read(h, fx, sizeof(fx))) > 0)
			if (!tail(ud, m, fx, n))
				goto bad;
		if (n < 0 || !tail(ud, m, fx, 0))
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
	if (align)
		free(raw);
	else if (!mem)
		free(m);
	snprintf(err, errlen, "%s is damaged: copy it again from the release", name);
	return 0;
}

#if defined(__m68k__) || defined(__mc68000__)
#pragma GCC diagnostic ignored "-Warray-bounds"	/* the cookie jar's fixed address */
/* in supervisor mode: the _CPU cookie, then the caches as it says */
static long flush_super(void)
{
	long *jar = *(long **)0x5a0, cpu = 0;
#ifdef __mcoldfire__
	/* a ColdFire (the FireBee's MCF5474: 512 sets of 4 ways, each cache):
	   each line pushed and invalidated in both caches, as EmuTOS does */
	{
		unsigned long set, way;
		(void)jar;
		(void)cpu;
		/* nop first: the ColdFire's store buffer drained, so the
		   relocations just written are in the cache when its lines are
		   pushed (EmuTOS's flush does the same; a FireBee crash with
		   HypView resident, 21 Nov 2026, is suspected of it); and after,
		   the pipeline synchronised before the new code runs */
		__asm__ volatile("nop" ::: "memory");
		for (set = 0; set < 512; set++)
			for (way = 0; way < 4; way++)
				__asm__ volatile("cpushl %%bc,(%0)" : : "a"(set << 4 | way) : "memory");
		__asm__ volatile("nop" ::: "memory");
		return 0;
	}
#endif
	for (; jar && jar[0]; jar += 2)
		if (jar[0] == 0x5F43465FL) {	/* _CF_: a 68000 program on a FireBee */
			/* FireTOS says _CPU 60, but a ColdFire has neither cpusha
			   nor a CACR to read: its own push of each line, as the
			   ColdFire build above (cpushl %bc,(%a0) as words). TLSFB.OVL
			   is loaded so (decided 15 Nov 2026) */
			unsigned long set, way;
			__asm__ volatile(".short 0x4e71" ::: "memory");	/* nop: the store buffer drained */
			for (set = 0; set < 512; set++)
				for (way = 0; way < 4; way++) {
					register unsigned long a __asm__("a0") = set << 4 | way;
					__asm__ volatile(".short 0xf4e8" : : "a"(a) : "memory");
				}
			__asm__ volatile(".short 0x4e71" ::: "memory");	/* nop */
			return 0;
		} else if (jar[0] == 0x5F435055L)	/* _CPU: 0, 10, 20, 30, 40, 60 */
			cpu = jar[1];
	if (cpu >= 40)
		__asm__ volatile(".short 0xf4f8" ::: "memory");		/* cpusha bc */
	if (cpu >= 60)
		__asm__ volatile(".short 0x4e7a,0x0002\n\t"		/* movec cacr,d0 */
				 "ori.l #0x00400000,%%d0\n\t"		/* CABC: clear the branch cache */
				 ".short 0x4e7b,0x0002"			/* movec d0,cacr */
				 ::: "d0", "memory");
	else if (cpu >= 20 && cpu < 40)
		__asm__ volatile(".short 0x4e7a,0x0002\n\t"		/* movec cacr,d0 */
				 "ori.w #0x0808,%%d0\n\t"		/* CI, CD: clear both */
				 ".short 0x4e7b,0x0002"			/* movec d0,cacr */
				 ::: "d0", "memory");
	return 0;
}

void ovl_flush(void)
{
	/* Supexec (XBIOS 38) */
#ifdef __mcoldfire__
	__asm__ volatile("move.l %0,-(%%sp)\n\t"
			 "moveq #38,%%d0\n\t"
			 "move.w %%d0,-(%%sp)\n\t"
			 "trap #14\n\t"
			 "addq.l #6,%%sp"
			 :: "r"(flush_super) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return;
#endif
	__asm__ volatile("move.l %0,-(%%sp)\n\t"
			 "move.w #38,-(%%sp)\n\t"
			 "trap #14\n\t"
			 "addq.l #6,%%sp"
			 :: "r"(flush_super) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}
#else
void ovl_flush(void)
{
}
#endif
