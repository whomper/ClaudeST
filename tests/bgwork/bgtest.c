/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * BGT.PRG - ui/bgwork.c on an Atari, in an emulator, built with
 * -DAES_PROFILE (atari/gem.c): first 300 ms of work in one go (the logger
 * must see it), then 3 s of work in 5 ms steps through bgwork, whose
 * longest stretch between event calls must stay near one slice. It says
 * on the emulator's console (NatFeats) how it went; run.py reads it.
 */
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "bgwork.h"

static void step(void)
{
	u32 t = tos_ms();
	while (tos_ms() == t)
		;			/* one step of the 200 Hz clock: about 5 ms */
}

int main(void)
{
	short msg[8];
	AEVENT ev;
	char line[96];
	u32 t0, done;
	int left = 600;			/* steps: about 3 s */
	appl_init();
	nf_debug("BGT start\n");
	evnt_timer_(10);
	aes_profile_tag("in-one-go");
	t0 = tos_ms();
	while (tos_ms() - t0 < 300)
		;
	evnt_timer_(10);
	aes_profile_tag("bgwork");
	bg_init(BG_NOW);
	t0 = tos_ms();
	while (left > 0) {
		evnt_multi_(MU_TIMER, 0, 0, 0, bg_timer(left > 0, 1000), msg, &ev);
		bg_tick();
		if (!bg_may_work())
			continue;
		bg_begin();
		do {
			step();
			left--;
		} while (left > 0 && bg_more());
	}
	done = tos_ms() - t0;
	evnt_timer_(10);
	snprintf(line, sizeof(line), "BGT done in %lu ms, longest stretch %lu ms\n",
		 (unsigned long)done, aes_profile_max());
	nf_debug(line);
	appl_exit();
	return 0;
}
