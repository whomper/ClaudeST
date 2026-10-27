/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * BUBBLE.PRG: a stand-in for BGEM.NG for notify.py, run as C:\BUBBLE.PRG so
 * that XaAES names it "BUBBLE  ". It does what bgemng.hyp's API page says
 * BGEM.NG does: answers BGEMNG_INQUIRE with its features (0x0F), takes a
 * BUBBLEGEM_SHOW (the text read from the sender's global memory) with a
 * BUBBLEGEM_ACK, and a second later "clicks" the first button: another ACK,
 * msg[3] 1 and msg[4] the sender's id. It says on the console:
 *   "BUBBLE asked by N", "BUBBLE show ID KIND TEXT"
 * (BGEM.NG 1.02 itself stops with a bus error in the rig's FreeMiNT 1.17.0
 * and XaAES, which doesn't know its xa_bubble: 27 Oct 2026.)
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "notify.h"

int main(void)
{
	char b[700];
	short m[8], r[8], t;
	AEVENT e;
	appl_init();
	nf_debug("BUBBLE up\n");
	for (t = 0; t < 600; t++) {
		evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, m, &e);
		if (!(e.which & MU_MESAG))
			continue;
		memset(r, 0, sizeof(r));
		r[1] = appl_id();
		if ((unsigned short)m[0] == BGEMNG_INQUIRE) {
			snprintf(b, sizeof(b), "BUBBLE asked by %d\n", m[1]);
			nf_debug(b);
			r[0] = (short)BGEMNG_INQUIRE;
			r[7] = 0x0f;
			appl_write(m[1], 16, r);
		} else if ((unsigned short)m[0] == BUBBLEGEM_SHOW) {
			const char *s = (const char *)(((unsigned long)(unsigned short)m[5] << 16) | (unsigned short)m[6]);
			snprintf(b, sizeof(b), "BUBBLE show %d %x %s\n", m[2], m[7], s);
			nf_debug(b);
			r[0] = (short)BUBBLEGEM_ACK;
			r[5] = m[5];
			r[6] = m[6];
			r[7] = m[7];
			appl_write(m[1], 16, r);
			evnt_timer_(1000);
			r[3] = 1;		/* the first button */
			r[4] = m[2];
			appl_write(m[1], 16, r);
		}
	}
	appl_exit();
	return 0;
}
