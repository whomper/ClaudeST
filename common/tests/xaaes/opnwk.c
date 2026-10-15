/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* OPNWK.PRG - opens the VDI's physical workstation (screen, device 1) and
 * leaves it open, then ends. Runs from AUTO before FreeMiNT. Without it
 * XaAES 1.17 asks EmuTOS for a virtual workstation on a physical one that
 * no AES ever opened (it starts in place of EmuTOS's AES), and EmuTOS's VDI
 * then draws nothing usable. */
#include "tos.h"

static short contrl[12], intin[16], ptsin[16], intout[64], ptsout[16];
static void *pb[5] = { contrl, intin, ptsin, intout, ptsout };

int main(void)
{
	short i;
	for (i = 0; i < 10; i++)
		intin[i] = 1;
	intin[10] = 2;
	contrl[0] = 1;		/* v_opnwk */
	contrl[1] = 0;
	contrl[3] = 11;
	contrl[5] = 0;
	contrl[6] = 0;
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
		: : "r"((long)pb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	nf_debug(contrl[6] > 0 ? "OPNWK: physical workstation open\n" : "OPNWK: v_opnwk failed\n");
	return 0;
}
