/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * CLOCKT.PRG - tos_ms() (atari/tos.c) on an Atari, in an emulator. With
 * a file C:\FASTCLK it first sets MFP timer C to 312 Hz (2457600 / 64 /
 * 123), TOS's handler still counting each interrupt in _hz_200, as on
 * Erez's Falcon. It says on the emulator's console (NatFeats) what
 * tos_clock() found, then "CLOCKT start", and "CLOCKT end" once tos_ms()
 * has counted 8 seconds; run.py times the two lines with this computer's
 * clock (Hatari runs in real time).
 */
#include <string.h>
#include "tos.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* a fixed hardware address */
static void sup_fastclk(void)
{
	*(volatile unsigned char *)0xfffffa23L = 123;	/* timer C's data register */
}

static void sup_trueclk(void)
{
	*(volatile unsigned char *)0xfffffa23L = 192;	/* TOS's own: 200 Hz */
}

int main(void)
{
	char line[96];
	long h = Fopen("C:\\FASTCLK", 0);
	u32 t0;
	if (h >= 0) {
		Fclose((short)h);
		Supexec(sup_fastclk);
		/* as on a real machine, where the timer was set at boot: the
		   round TOS started at 192 is over long before tos_ms() looks */
		t0 = tos_hz200();
		while (tos_hz200() - t0 < 100)
			;
		nf_debug("CLOCKT timer C at 312 Hz\n");
	}
	strcpy(line, "CLOCKT clock: ");
	strcat(line, tos_clock()[0] ? tos_clock() : "true");
	strcat(line, "\n");
	nf_debug(line);
	t0 = tos_ms();
	while (tos_ms() == t0)
		;
	t0 = tos_ms();
	nf_debug("CLOCKT start\n");
	while (tos_ms() - t0 < 8000)
		;
	nf_debug("CLOCKT end\n");
	if (h >= 0)
		Supexec(sup_trueclk);
	nf_debug("CLOCKT done\n");
	return 0;
}
