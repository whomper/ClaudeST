/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * VDT.PRG - what the VDI's drawing costs on this machine and TOS (the
 * guidelines' Speed): 40 letters of v_gtext at x on a byte (0, 8) and off
 * one (1, 3), replace and transparent, black and white; a bar filled; the
 * screen copied to itself and from memory. Each line goes to the
 * emulator's console as "VDT <what> <us> us"; vdt.py runs it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"

static short vh;
static char line[96];

static void say(const char *what, u32 ms, int n)
{
	snprintf(line, sizeof(line), "VDT %s %lu us\n", what, (unsigned long)(ms * 1000 / n));
	nf_debug(line);
}

int main(void)
{
	short wo[57], ex[57], cw, ch, bw, bh, i, pxy[8];
	static const char text[] = "The quick brown fox jumps over the lazy";
	short xs[] = {0, 1, 3, 8};
	MFDB scr = {0}, mem;
	u32 t;
	char what[48];
	appl_init();
	vh = graf_handle(&cw, &ch, &bw, &bh);
	vh = v_opnvwk_(vh, wo);
	vq_extnd(vh, 1, ex);
	snprintf(line, sizeof(line), "VDT screen %dx%d, %d planes, char %dx%d\n",
		 wo[0] + 1, wo[1] + 1, ex[4], cw, ch);
	nf_debug(line);
	wind_update(BEG_UPDATE);
	graf_mouse(256, 0);
	for (int mode = 1; mode <= 2; mode++)
		for (int col = 1; col >= 0; col--)
			for (i = 0; i < 4; i++) {
				int n = 40;
				vswr_mode(vh, mode);
				vst_color(vh, col);
				t = tos_ms();
				for (int k = 0; k < n; k++)
					v_gtext_n(vh, 16 + xs[i], 60 + (k % 16) * ch, text, 39);
				snprintf(what, sizeof(what), "text-%s-%s-x%d", mode == 1 ? "replace" : "trans",
					 col ? "black" : "white", xs[i]);
				say(what, tos_ms() - t, n);
			}
	vswr_mode(vh, 1);
	vsf_interior(vh, 1);
	vsf_color(vh, 0);
	pxy[0] = 0; pxy[1] = 20; pxy[2] = wo[0]; pxy[3] = wo[1];
	t = tos_ms();
	for (i = 0; i < 10; i++)
		vr_recfl(vh, pxy);
	say("fill-screen", tos_ms() - t, 10);
	pxy[0] = 0; pxy[1] = 20; pxy[2] = wo[0]; pxy[3] = wo[1] - 20;
	pxy[4] = 0; pxy[5] = 40; pxy[6] = wo[0]; pxy[7] = wo[1];
	t = tos_ms();
	for (i = 0; i < 10; i++)
		vro_cpyfm(vh, 3, pxy, &scr, &scr);
	say("copy-screen-to-screen", tos_ms() - t, 10);
	mem.fd_w = wo[0] + 1;
	mem.fd_h = wo[1] + 1;
	mem.fd_wdwidth = (wo[0] + 16) / 16;
	mem.fd_stand = 0;
	mem.fd_nplanes = ex[4];
	mem.fd_r1 = mem.fd_r2 = mem.fd_r3 = 0;
	mem.fd_addr = Malloc((long)mem.fd_wdwidth * 2 * mem.fd_h * ex[4]);
	if (mem.fd_addr) {
		pxy[0] = 0; pxy[1] = 0; pxy[2] = wo[0]; pxy[3] = wo[1];
		pxy[4] = 0; pxy[5] = 0; pxy[6] = wo[0]; pxy[7] = wo[1];
		t = tos_ms();
		for (i = 0; i < 10; i++)
			vro_cpyfm(vh, 3, pxy, &mem, &scr);
		say("copy-memory-to-screen", tos_ms() - t, 10);
		t = tos_ms();
		for (i = 0; i < 10; i++)
			vro_cpyfm(vh, 3, pxy, &scr, &mem);
		say("copy-screen-to-memory", tos_ms() - t, 10);
	}
	{
		short h2, wo2[57], phys = graf_handle(&cw, &ch, &bw, &bh);
		t = tos_ms();
		for (i = 0; i < 10; i++) {
			h2 = v_opnvwk_(phys, wo2);
			v_clsvwk(h2);
		}
		say("open-close-workstation", tos_ms() - t, 10);
	}
	/* a GDOS font (NVDI's FreeMono, Erez's: 26 Oct 2026), a line at a time
	   and a letter at a time, against the system font's line */
	if (vq_gdos()) {
		short n = vst_load_fonts(vh, 0), id = 0, k, fw, fh;
		char nm[34];
		for (k = 0; k < n && !id; k++) {
			short fid = vqt_name(vh, k + 2, nm);
			if (!strncmp(nm, "FreeMono", 8))
				id = fid;
		}
		snprintf(line, sizeof(line), "VDT gdos fonts %d, FreeMono id %d\n", n, id);
		nf_debug(line);
		if (id) {
			vst_font(vh, id);
			vst_point(vh, 10, &fw, &fh);
			vswr_mode(vh, 2);
			vst_color(vh, 1);
			for (i = 0; i < 2; i++) {
				t = tos_ms();
				for (k = 0; k < 40; k++)
					v_gtext_n(vh, 16 + xs[i * 3], 60 + (k % 16) * fh, text, 39);
				snprintf(what, sizeof(what), "freemono10-line-x%d", xs[i * 3]);
				say(what, tos_ms() - t, 40);
			}
			t = tos_ms();
			for (k = 0; k < 10; k++)
				for (i = 0; i < 39; i++)
					v_gtext_n(vh, 16 + i * fw, 60 + k * fh, text + i, 1);
			say("freemono10-letter-by-letter-line", tos_ms() - t, 10);
			vst_font(vh, 1);
		}
	}
	graf_mouse(257, 0);
	wind_update(0);
	form_dial(3, 0, 0, 0, 0);
	form_dial(3, 0, 0, wo[0] + 1, wo[1] + 1);
	nf_debug("VDT done\n");
	v_clsvwk(vh);
	appl_exit();
	return 0;
}
