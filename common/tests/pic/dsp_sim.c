/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_sim.c - dsp/jpeg.a56 and dsp/dither.a56 themselves, loaded through
 * the bootstrap of Hatari's DSP56001 core (../ec/fetch-hatari.sh), behind
 * the Falcon's host port: a PIC_DSP for test_pic_dsp. It counts the DSP's
 * cycles, and the words each way. As on the Falcon, a program left clean
 * at its commands is kept for the next picture that wants it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "pic.h"
#include "../../dsp/jpeg_prog.h"
#include "../../dsp/dither_prog.h"

/* what the core calls in the rest of Hatari */
void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void hint(int s) { (void)s; }

unsigned long long sim_cycles;		/* DSP clock cycles (32 MHz on the Falcon) */
unsigned long long sim_pc[1536];	/* PIC_DSP_PROFILE: each address's cycles, P:$0..$1FF and $7000.. at 512 */
unsigned long sim_in, sim_out;		/* words to the DSP, and from it */
int sim_busy;				/* begin() says the DSP is taken */

/* PIC_DSP_TRACE=ADDR (hex): the registers each time the DSP is there,
   and with PIC_DSP_FOLLOW=N at the N instructions after, for debugging
   jpeg.a56 */
static void trace(uint16_t pc)
{
	static long at = -2;
	static int follow;
	const uint32_t *r = dsp_core.registers;
	if (at == -2) {
		const char *e = getenv("PIC_DSP_TRACE");
		at = e != NULL ? strtol(e, NULL, 16) : -1;
	}
	if (at < 0)
		return;
	if (pc == at && follow == 0 && getenv("PIC_DSP_FOLLOW") != NULL)
		follow = atoi(getenv("PIC_DSP_FOLLOW"));
	else if (pc != at) {
		if (follow == 0)
			return;
		follow--;
	}
	fprintf(stderr, "@%04x a=%02x:%06x:%06x b=%02x:%06x:%06x x=%06x,%06x y=%06x,%06x "
		"r0-7=%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x sr=%04x\n", pc,
		r[DSP_REG_A2], r[DSP_REG_A1], r[DSP_REG_A0], r[DSP_REG_B2], r[DSP_REG_B1], r[DSP_REG_B0],
		r[DSP_REG_X0], r[DSP_REG_X1], r[DSP_REG_Y0], r[DSP_REG_Y1], r[DSP_REG_R0], r[DSP_REG_R0 + 1],
		r[DSP_REG_R0 + 2], r[DSP_REG_R0 + 3], r[DSP_REG_R0 + 4], r[DSP_REG_R0 + 5], r[DSP_REG_R0 + 6],
		r[DSP_REG_R0 + 7], r[DSP_REG_SR]);
}

/* PIC_DSP_DUMPAT=PC,N,[x]ADDR,LEN (hex but N): the DSP's external Y (or
   X) memory the Nth time it is at PC (counting from 1) */
static void dump_at(uint16_t pc)
{
	static long at = -2, n, a, len, x;
	unsigned i;
	if (at == -2) {
		const char *e = getenv("PIC_DSP_DUMPAT");
		char sp[2] = "";
		at = -1;
		if (e != NULL && sscanf(e, "%lx,%ld,%1[x]%lx,%lx", &at, &n, sp, &a, &len) < 5) {
			x = 0;
			if (sscanf(e, "%lx,%ld,%lx,%lx", &at, &n, &a, &len) < 4)
				at = -1;
		} else if (e != NULL) {
			x = 0x4000;
		}
	}
	if (pc != at || --n != 0)
		return;
	for (i = 0; i < (unsigned) len; i++)
		fprintf(stderr, "%06lx%c", (unsigned long) dsp_core.ramext[((a + i) & 0x3fff) | x],
			i % 8 == 7 ? '\n' : ' ');
	fprintf(stderr, "\n");
}

static int step(void)
{
	uint16_t pc;
	if (!dsp_core.running)
		return 0;
	pc = dsp_core.pc;
	trace(pc);
	dump_at(pc);
	dsp56k_execute_instruction();
	sim_cycles += dsp_core.instr_cycle;
	if (pc < 512)
		sim_pc[pc] += dsp_core.instr_cycle;
	else if (pc >= 0x7000 && pc < 0x7400)
		sim_pc[512 + pc - 0x7000] += dsp_core.instr_cycle;
	return 1;
}

static void put1(unsigned long w)
{
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		if (!step() || ++n > 10000000) {
			pic_dsp_failed = 1;
			return;
		}
	dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, w & 255);
	sim_in++;
}

static unsigned long get1(void)
{
	unsigned long w;
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 1))
		if (!step() || ++n > 10000000) {
			pic_dsp_failed = 1;
			return 0;
		}
	w = (unsigned long)dsp_core_read_host(CPU_HOST_RXH) << 16;
	w |= (unsigned long)dsp_core_read_host(CPU_HOST_RXM) << 8;
	w |= dsp_core_read_host(CPU_HOST_RXL);
	sim_out++;
	return w;
}

static void put(const unsigned long *w, long n)
{
	while (n-- > 0 && !pic_dsp_failed)
		put1(*w++);
}

static void put16(const unsigned short *w, long n)
{
	while (n-- > 0 && !pic_dsp_failed)
		put1(*w++);
}

static void putpx(const unsigned short *px, const unsigned char *alpha, long n)
{
	long i;
	for (i = 0; i < n && !pic_dsp_failed; i++)
		put1(px[i] | (alpha != NULL && alpha[i] < 128 ? 0x10000UL : 0));
}

static unsigned long get16(unsigned short *d, int cols, int rows, long pitch)
{
	int r, c;
	unsigned long h = get1();
	if (h != 0 || pic_dsp_failed)
		return h;
	for (r = 0; r < rows; r++, d += pitch)
		for (c = 0; c < cols && !pic_dsp_failed; c++)
			d[c] = (unsigned short)get1();
	return 0;
}

static unsigned long get24(unsigned char *d, int cols, int rows, long pitch)
{
	int r, c;
	unsigned long h = get1();
	if (h != 0 || pic_dsp_failed)
		return h;
	for (r = 0; r < rows; r++, d += pitch)
		for (c = 0; c < cols && !pic_dsp_failed; c++) {
			unsigned long v = get1();
			d[3 * c] = (unsigned char)(v >> 16);
			d[3 * c + 1] = (unsigned char)(v >> 8);
			d[3 * c + 2] = (unsigned char)v;
		}
	return 0;
}

static int alive;			/* the program left clean at its commands, + 1 */

static int begin(int prog)
{
	static const unsigned long ext[2][3] = {
		{ 6, DSP_JPEG_PROG_EXT_AT, DSP_JPEG_PROG_EXT_N },
		{ 6, DSP_DITHER_PROG_EXT_AT, DSP_DITHER_PROG_EXT_N }
	};
	static int inited;
	if (sim_busy)
		return 0;
	if (!inited) {
		dsp_core_init(hint);
		dsp56k_init_cpu();
		inited = 1;
	}
	pic_dsp_failed = 0;
	if (alive == prog + 1 && !getenv("PIC_DSP_RELOAD")) {
		/* its PING */
		static const unsigned long ping = 5;
		put(&ping, 1);
		if (get1() == (prog == PIC_DSP_JPEG ? 0x4A5047UL : 0x445448UL) && !pic_dsp_failed)
			return 2;
	}
	alive = prog + 1;
	dsp_core_reset();
	pic_dsp_failed = 0;
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

/* PIC_DSP_DUMP=[x]ADDR,N: the DSP's external Y (or X) memory from ADDR
   at the end, for debugging jpeg.a56 */
static void end(int clean)
{
	const char *e = getenv("PIC_DSP_DUMP");
	unsigned a, n, i, x = 0;
	if (!clean || pic_dsp_failed)
		alive = 0;
	if (e != NULL && *e == 'x') {
		x = 0x4000;
		e++;
	}
	if (e == NULL || sscanf(e, "%x,%u", &a, &n) != 2)
		return;
	for (i = 0; i < n; i++)
		fprintf(stderr, "%06lx%c", (unsigned long)dsp_core.ramext[((a + i) & 0x3fff) | x],
			i % 8 == 7 ? '\n' : ' ');
	fprintf(stderr, "\n");
}
static const PIC_DSP port = { begin, end, put, put16, putpx, get16, get24 };

const PIC_DSP *sim_dsp(void)
{
	return &port;
}
