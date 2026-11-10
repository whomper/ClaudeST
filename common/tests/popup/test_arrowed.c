/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* ui/arrowed.c on the host (arrowed.h, D37): the pane under the pointer, else the slider's pane */
#include <stdio.h>
#include "stub.h"
#include "arrowed.h"

static int failed, total;
#define mx mouse_x
#define my mouse_y
static void check(int ok, const char *what)
{
	total++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

int main(void)
{
	static const ARROW_PANE p[] = { { { 0, 20, 200, 300 }, 1 }, { { 200, 20, 440, 300 }, 2 }, { { 0, 320, 640, 60 }, 3 } };
	mx = 50;
	my = 100;
	check(arrow_target(p, 3, 2) == 1 && mk_calls == 1, "the pointer over the first pane: that pane (one AES call)");
	mx = 300;
	check(arrow_target(p, 3, 1) == 2, "over the second: that one, whatever the slider's pane");
	mx = 639;
	my = 379;
	check(arrow_target(p, 3, 1) == 3, "the last pixel of the third pane is in it");
	mx = 640;
	check(arrow_target(p, 3, 1) == 1, "one past the right edge: outside every pane, the slider's pane");
	mx = 200;
	my = 20;
	check(arrow_target(p, 3, 9) == 2, "a shared edge belongs to the pane that starts there");
	mx = 625;
	my = 10;
	check(arrow_target(p, 3, 7) == 7, "on the window's own arrow (outside the panes): the slider's pane");
	mk_calls = 0;
	check(arrow_target(p, 0, 5) == 5 && arrow_target(0, 3, 5) == 5 && mk_calls == 0, "no panes: the slider's pane, no AES call");
	check(arrow_target_at(p, 3, 1, 10, 330) == 3 && mk_calls == 0, "arrow_target_at takes a pointer already read");
	printf("%d checks, %d failed\n", total, failed);
	return failed != 0;
}
