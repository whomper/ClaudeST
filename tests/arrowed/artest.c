/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ARTEST.PRG: a window with the AES's own vertical slider, to see what
 * the AES sends for one click on the track above or below the slider
 * (Erez, 28 Oct 2026: the window's slider moves two pages a click). Each
 * WM_ARROWED says "ART arrowed WHAT ms T button B" on the console
 * (NatFeats), T after the program's start, B the button as the message is
 * read; then the program works for as long as BUSY.TXT beside it says
 * (in ms: an app's scroll and redraw), and moves its slider a page.
 * "ART ready" once the window is open.
 */
#include <stdio.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	short win, msg[8], pos = 0, busy = 0, mx, my, mb, ks, x, y, w, h;
	char line[80];
	AEVENT ev;
	u32 t0;
	long fh;
	appl_init();
	fh = Fopen("BUSY.TXT", 0);
	if (fh >= 0) {
		char b[8] = { 0 };
		Fread((short) fh, 7, b);
		Fclose((short) fh);
		busy = (short) atoi(b);
	}
	graf_mouse(0, 0);
	wind_get(0, WF_WORKXYWH, &x, &y, &w, &h);
	win = wind_create(0x01 | 0x02 | 0x08 | 0x20 | 0x40 | 0x80 | 0x100, x, y, w, h);
	wind_set_str(win, WF_NAME, " ARTEST ");
	wind_open(win, x + 40, y + 20, 300, h - 60);
	wind_set(win, WF_VSLSIZE, 100, 0, 0, 0);
	wind_set(win, WF_VSLIDE, pos, 0, 0, 0);
	t0 = tos_ms();
	snprintf(line, sizeof(line), "ART ready busy %d\n", busy);
	nf_debug(line);
	for (;;) {
		short e = evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (!(e & MU_MESAG))
			continue;
		if (msg[0] == WM_CLOSED)
			break;
		if (msg[0] == WM_ARROWED) {
			u32 t = tos_ms() - t0, s;
			graf_mkstate(&mx, &my, &mb, &ks);
			snprintf(line, sizeof(line), "ART arrowed %d ms %lu button %d\n", msg[4], (unsigned long) t, mb);
			nf_debug(line);
			s = tos_ms();
			while (tos_ms() - s < (u32) busy)
				;
			pos = (short) (pos + (msg[4] == 1 ? 100 : msg[4] == 0 ? -100 : msg[4] == 3 ? 10 : -10));
			pos = pos < 0 ? 0 : pos > 1000 ? 1000 : pos;
			wind_set(win, WF_VSLIDE, pos, 0, 0, 0);
			snprintf(line, sizeof(line), "ART done pos %d ms %lu\n", pos, (unsigned long) (tos_ms() - t0));
			nf_debug(line);
		}
	}
	wind_close(win);
	wind_delete(win);
	appl_exit();
	return 0;
}
