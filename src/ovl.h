/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ovl.h - load a file in TOS's program format (elf2tos.py's: the
 * header, text, data, symbols, fixups) into memory from malloc and
 * relocate it to where it landed, its BSS zeroed: TLS.OVL's loader
 * (src/tls_ovl.c) and EWeb's EWEBJS.OVL's (its imports after the fixups,
 * through ovl_load_tail). Not Pexec: no basepage, no memory of TOS's own,
 * so an accessory can load one into its heap (the guidelines, 2.2).
 */
#ifndef OVL_H
#define OVL_H

/* the start of its text, or NULL with err filled in; *taken: the bytes
   it holds (text, data and BSS). The file is read in one go, 16 KB at a
   time (50 ms for TLS.OVL on a Falcon), and closed before pf_idle runs:
   an accessory never has it open across an event call (the guidelines,
   2.2). The caller flushes the CPU's caches before running any of it */
void *ovl_load(const char *path, long *taken, char *err, int errlen);

/* ovl_load with the image's start on a 256-byte boundary, so its code
   meets the 68030's instruction cache the same way whatever the heap
   held before (EContact's books read: 70 ms at 0 mod 256, 95 at 128 on
   a stock Falcon). 260 bytes more of the heap while loaded; let it go
   with ovl_free, never free. PIC.OVL and TLS.OVL are loaded so */
void *ovl_load_aligned(const char *path, long *taken, char *err, int errlen);
void ovl_free(void *image);

/* ovl_load into a block the caller holds (mem, size bytes): set aside
   before memory was split, for an overlay too big for what is left later
   (EPDF's RENDER.OVL, 643 KB, on a 4 MB Falcon). Refused, as ovl_load is
   when malloc is, before anything is read, when its text, data and BSS
   are more than size; the block is never freed here, even when the file
   turns out damaged */
void *ovl_load_into(const char *path, void *mem, long size, long *taken, char *err, int errlen);

/* the bytes ovl_load would take for path (text, data and BSS), from its
   header; -1 when it is missing or isn't one */
long ovl_need(const char *path);

/* ovl_load, and the bytes after the fixups' end handed to tail as they
   are read, in pieces (base: the image, relocated), then once with n 0
   at the file's end; tail returns 0 when they are wrong (the file is
   damaged, and nothing is kept). *later (if not NULL) says why it
   failed: 1 only for now (the memory: try again when it is next
   needed), 0 for good (missing or damaged) */
typedef int (*OVL_TAIL)(void *ud, unsigned char *base, const unsigned char *p, long n);
void *ovl_load_tail(const char *path, long *taken, char *err, int errlen, OVL_TAIL tail, void *ud, int *later);

/* the CPU's caches cleared, before the code just loaded runs: a 68030's
   instruction and data caches, a 68040's or 68060's with CPUSHA, and a
   68060's branch cache (CACR's CABC), which CPUSHA leaves alone (in
   supervisor mode, by Supexec; nothing on a 68000, or on the build
   machine) */
void ovl_flush(void);

/* relocate an image already in memory (text and data, then BSS room)
   from its fixups: the part ovl_load runs as each piece comes in, for
   tests/tlsovl. 0 when a fixup points outside text and data */
typedef struct {
	unsigned char *base;
	long size;			/* text + data */
	long at;			/* the address being fixed, -1 before the first */
	int started;
	long used;			/* at the end (2): the bytes of the last piece the table took */
} OVL_FIX;
void ovl_fix_start(OVL_FIX *f, unsigned char *base, long size);
int ovl_fix_feed(OVL_FIX *f, const unsigned char *p, long n);	/* 0 bad, 1 more, 2 done */

#endif
