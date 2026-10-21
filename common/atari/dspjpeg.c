/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspjpeg.c - the Falcon's DSP56001 for pic.c's JPEGs: loads dsp/jpeg.a56
 * through the DSP's bootstrap and trades words with it through the host
 * port, as dspec.c does for the elliptic curves. The DSP is locked with
 * the XBIOS for the whole picture (a TLS handshake meanwhile does its
 * arithmetic on the 68030); a busy or missing DSP means the 68030 does
 * the picture.
 *
 * The host port is reached in supervisor mode: the scan's words go in one
 * Supexec and each MCU's pixels come back in another, so the program's
 * polls between them run in user mode as ever (Super for the whole
 * picture would have to leave it at the very stack depth it entered it).
 * jpeg.a56 is in two parts: the bootstrap loads the first, which loads
 * the rest (LOADP) into external memory.
 *
 * dsp/dither.a56 dithers pictures to the screen's pens (pic_dither) the
 * same way. A picture that ended cleanly leaves its program at its
 * commands: the next one for the same program asks it (PING, "JPG" or
 * "DTH" back) rather than resetting the DSP and loading it again; any
 * other program there (TLS's) takes the PING for nothing and doesn't
 * answer.
 */
#include "tos.h"
#include "dspjpeg.h"
#include "../dsp/jpeg_prog.h"
#include "../dsp/dither_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200   (*(volatile unsigned long *)0x4baL)
#define HISR    (*(volatile unsigned char *)0xffffa202L)
#define HTX32   (*(volatile unsigned long *)0xffffa204L)	/* 0, then the word's 3 bytes */
#define HTXH    (*(volatile unsigned char *)0xffffa205L)	/* the word's bytes */
#define HTXM    (*(volatile unsigned char *)0xffffa206L)
#define HTXL    (*(volatile unsigned char *)0xffffa207L)
#define HTX16   (*(volatile unsigned short *)0xffffa206L)	/* its low 2 */

long get_cookie(long id);

static int lock, alive;			/* alive: the program left at its commands, + 1 */
/* a Supexec's work */
static const unsigned long *io_w;
static unsigned short *io_s;
static unsigned char *io_b;
static long io_n, io_pitch;
static int io_cols, io_rows;
static const unsigned short *io_w16;
static const unsigned char *io_a;
static unsigned long io_head;		/* the MCU's word from the DSP */

int dspjpeg_present(void)
{
	return (get_cookie(0x5F534E44L) & 8) != 0;	/* _SND: a DSP56001 */
}

/* the rest in supervisor mode. A word waits at most a second: an MCU
   takes the DSP well under a millisecond */
static int wait_bit(int bit)
{
	unsigned long t0 = HZ200;
	while (!(HISR & bit))
		if (HZ200 - t0 > 200) {
			pic_dsp_failed = 1;
			return 0;
		}
	return 1;
}

static void sup_put(void)
{
	const unsigned long *w = io_w;
	long n = io_n;
	while (n-- > 0) {
		if (!(HISR & 2) && !wait_bit(2))
			return;
		HTX32 = *w++;
	}
}

/* 16-bit words: the top byte 0 once (it stays), then the low two of
   each */
static void sup_put16(void)
{
	const unsigned short *w = io_w16;
	long n = io_n;
	if (!(HISR & 2) && !wait_bit(2))
		return;
	HTXH = 0;
	while (n-- > 0) {
		if (!(HISR & 2) && !wait_bit(2))
			return;
		HTX16 = *w++;
	}
}

/* pixels: as put16, or with an alpha a 3-byte word each, its top byte
   1 for a pixel under half solid */
static void sup_putpx(void)
{
	const unsigned short *w = io_w16;
	const unsigned char *a = io_a;
	long n = io_n;
	if (a == 0) {
		sup_put16();
		return;
	}
	while (n-- > 0) {
		if (!(HISR & 2) && !wait_bit(2))
			return;
		HTXH = *a++ < 128;
		HTX16 = *w++;
	}
}

/* the MCU's word: 0, its pixels follow */
static int head(void)
{
	if (!(HISR & 1) && !wait_bit(1))
		return 0;
	io_head = (unsigned long) HTXH << 16;
	io_head |= (unsigned long) HTXM << 8;
	io_head |= HTXL;
	return io_head == 0;
}

/* Each word is waited for: jpeg.a56 sends them from its host interrupt
   while it decodes the next MCU, so one may come late (an interrupt
   waits out a REP) */
static void sup_get16(void)
{
	unsigned short *d = io_s, *o;
	int r, c;
	if (!head())
		return;
	for (r = 0; r < io_rows; r++, d += io_pitch)
		for (c = io_cols, o = d; c > 0; c--) {
			if (!(HISR & 1) && !wait_bit(1))
				return;
			*o++ = HTX16;
		}
}

static void sup_get24(void)
{
	unsigned char *d = io_b, *o;
	int r, c;
	if (!head())
		return;
	for (r = 0; r < io_rows; r++, d += io_pitch)
		for (c = io_cols, o = d; c > 0; c--) {
			if (!(HISR & 1) && !wait_bit(1))
				return;
			/* its 3 bytes (a long from $A204 is 4 bus cycles, and
			   Hatari doesn't answer it) */
			*o++ = HTXH;
			*o++ = HTXM;
			*o++ = HTXL;
		}
}

/* a moment, for the reset: reads of the host port, a quarter of a
   microsecond or more each, so 1 ms at least */
static void spin(void)
{
	int i;
	for (i = 0; i < 4000; i++)
		(void) HISR;
}

static void sup_reset(void)
{
	trap14_ww(30, 0x10);			/* Ongibit: hold the DSP in reset */
	spin();
	trap14_ww(29, ~0x10);			/* Offgibit: it starts its bootstrap */
	spin();
}

/* the program there? A PING, and its name back within a millisecond */
static int io_alive;
static unsigned long io_name;
static void sup_ping(void)
{
	int i;
	io_alive = 0;
	for (i = 0; i < 4000 && !(HISR & 2); i++)
		;
	if (!(HISR & 2))
		return;
	HTX32 = 5;
	for (i = 0; i < 4000 && !(HISR & 1); i++)
		;
	if (HISR & 1) {
		unsigned long v = (unsigned long) HTXH << 16;
		v |= (unsigned long) HTXM << 8;
		v |= HTXL;
		io_alive = v == io_name;
	}
}

static void put(const unsigned long *w, long n)
{
	if (pic_dsp_failed)
		return;
	io_w = w;
	io_n = n;
	Supexec(sup_put);
}

static void put16(const unsigned short *w, long n)
{
	if (pic_dsp_failed)
		return;
	io_w16 = w;
	io_n = n;
	Supexec(sup_put16);
}

static void putpx(const unsigned short *px, const unsigned char *alpha, long n)
{
	if (pic_dsp_failed)
		return;
	io_w16 = px;
	io_a = alpha;
	io_n = n;
	Supexec(sup_putpx);
}

static unsigned long get16(unsigned short *d, int cols, int rows, long pitch)
{
	if (pic_dsp_failed)
		return 0;
	io_s = d;
	io_cols = cols;
	io_rows = rows;
	io_pitch = pitch;
	io_head = 0;
	Supexec(sup_get16);
	return io_head;
}

static unsigned long get24(unsigned char *d, int cols, int rows, long pitch)
{
	if (pic_dsp_failed)
		return 0;
	io_b = d;
	io_cols = cols;
	io_rows = rows;
	io_pitch = pitch;
	io_head = 0;
	Supexec(sup_get24);
	return io_head;
}

static int begin(int prog)
{
	static const unsigned long ext[2][3] = {
		{ 6, DSP_JPEG_PROG_EXT_AT, DSP_JPEG_PROG_EXT_N },
		{ 6, DSP_DITHER_PROG_EXT_AT, DSP_DITHER_PROG_EXT_N }
	};
	lock = (int) trap14_w(104);		/* Dsp_Lock */
	if (lock == -1)
		return 0;			/* another program has it */
	pic_dsp_failed = 0;
	if (alive == prog + 1) {
		alive = 0;
		io_name = prog == PIC_DSP_JPEG ? 0x4A5047UL : 0x445448UL;	/* "JPG", "DTH" */
		Supexec(sup_ping);
		if (io_alive) {
			alive = prog + 1;
			return 2;
		}
	}
	alive = prog + 1;
	/* reset (bit 4 of the sound chip's port A, through the XBIOS: see
	   dsprsa.c); then the bootstrap loads 512 words and starts them */
	Supexec(sup_reset);
	if (prog == PIC_DSP_JPEG) {
		put(dsp_jpeg_prog, 512);
		put(ext[0], 3);			/* LOADP: the rest */
		put(dsp_jpeg_prog_ext, DSP_JPEG_PROG_EXT_N);
	} else {
		put(dsp_dither_prog, 512);
		put(ext[1], 3);
		put(dsp_dither_prog_ext, DSP_DITHER_PROG_EXT_N);
	}
	return 1;
}

static void end(int clean)
{
	if (!clean || pic_dsp_failed)
		alive = 0;
	if (lock == 0)
		trap14_w(105);			/* Dsp_Unlock */
}

const PIC_DSP dspjpeg_port = { begin, end, put, put16, putpx, get16, get24 };
