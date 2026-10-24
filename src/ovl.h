/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ovl.h - load a file in TOS's program format (elf2tos.py's: the
 * header, text, data, symbols, fixups) into memory from malloc and
 * relocate it to where it landed, its BSS zeroed: TLS.OVL's loader
 * (src/tls_ovl.c). Not Pexec: no basepage, no memory of TOS's own, so
 * an accessory can load one into its heap (the guidelines, 2.2).
 */
#ifndef OVL_H
#define OVL_H

/* the start of its text, or NULL with err filled in; *taken: the bytes
   it holds (text, data and BSS). The file is read in one go, 16 KB at a
   time (50 ms for TLS.OVL on a Falcon), and closed before pf_idle runs:
   an accessory never has it open across an event call (the guidelines,
   2.2). The caller flushes the CPU's caches before running any of it */
void *ovl_load(const char *path, long *taken, char *err, int errlen);

/* relocate an image already in memory (text and data, then BSS room)
   from its fixups: the part ovl_load runs as each piece comes in, for
   tests/tlsovl. 0 when a fixup points outside text and data */
typedef struct {
	unsigned char *base;
	long size;			/* text + data */
	long at;			/* the address being fixed, -1 before the first */
	int started;
} OVL_FIX;
void ovl_fix_start(OVL_FIX *f, unsigned char *base, long size);
int ovl_fix_feed(OVL_FIX *f, const unsigned char *p, long n);	/* 0 bad, 1 more, 2 done */

#endif
