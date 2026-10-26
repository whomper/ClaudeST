/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * SPT.PRG - speed.py's own check, built with -DAES_PROFILE=20
 * (atari/gem.c): 300 ms of work before its first event call (AESSTART),
 * then for each key a known stretch: Down 30 ms, Shift+Down 200 ms,
 * Return 80 ms; q quits. speed.py --selftest must read them back.
 */
#include "tos.h"
#include "gem.h"

static void work(u32 ms)
{
	u32 t0 = tos_ms();
	while (tos_ms() - t0 < ms)
		;
}

int main(void)
{
	short msg[8];
	AEVENT ev;
	appl_init();
	work(300);
	nf_debug("SPT ready\n");
	for (;;) {
		short scan;
		evnt_multi_(MU_KEYBD, 0, 0, 0, 0, msg, &ev);
		scan = (ev.kreturn >> 8) & 0xff;
		if ((ev.kreturn & 0xff) == 'q')
			break;
		if (scan == 0x50 && (ev.kstate & 3)) {
			aes_profile_tag("shift-down");
			work(200);
		} else if (scan == 0x50) {
			aes_profile_tag("down");
			work(30);
		} else if (scan == 0x1c) {
			aes_profile_tag("return");
			work(80);
		}
	}
	nf_debug("SPT done\n");
	appl_exit();
	return 0;
}
