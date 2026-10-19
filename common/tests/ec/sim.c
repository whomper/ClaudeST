/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/ec.a56 itself, loaded through the bootstrap of Hatari's
 * DSP56001 core (fetch-hatari.sh), behind the Falcon's host port: an
 * ECDSP_PORT for test_ec -s. It also counts the DSP's cycles.
 */
#include <stdio.h>
#include <stdint.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "ecdsp.h"
#include "../../dsp/ec_prog.h"

/* what the core calls in the rest of Hatari */
void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void hint(int s) { (void)s; }

unsigned long long sim_total;		/* DSP clock cycles (32 MHz on the Falcon) */

static int step(void)
{
	if (!dsp_core.running)
		return 0;
	dsp56k_execute_instruction();
	sim_total += dsp_core.instr_cycle;
	return 1;
}

static void put(unsigned long w)
{
	long n = 0;
	if (ecdsp_failed)
		return;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		if ((dsp_core.running && !step()) || ++n > 10000000) {
			ecdsp_failed = 1;
			return;
		}
	dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, w & 255);
}

static unsigned long get(void)
{
	unsigned long w;
	long n = 0;
	if (ecdsp_failed)
		return 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 1))
		if (!step() || ++n > 10000000) {
			ecdsp_failed = 1;
			return 0;
		}
	w = (unsigned long)dsp_core_read_host(CPU_HOST_RXH) << 16;
	w |= (unsigned long)dsp_core_read_host(CPU_HOST_RXM) << 8;
	w |= dsp_core_read_host(CPU_HOST_RXL);
	return w;
}

static int run(void (*fn)(void *), void *ctx)
{
	static int inited;
	int i;
	if (!inited) {
		dsp_core_init(hint);
		dsp56k_init_cpu();
		inited = 1;
	}
	dsp_core_reset();
	for (i = 0; i < 512; i++)
		put(dsp_ec_prog[i]);
	fn(ctx);
	return 1;
}

static const ECDSP_PORT port = { run, put, get };

const ECDSP_PORT *sim_port(void)
{
	return &port;
}
