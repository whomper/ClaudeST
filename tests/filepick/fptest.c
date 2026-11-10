/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * FPTEST.PRG: the family's file dialog (ui/filepick.c) in an emulator, for
 * run.py: opened on C:\MUSIC\ with EPlay's types, as EPlay's Add files.
 * run.py presses the keys and clicks; each file taken goes to the console
 * as "FPT took PATH", then "FPT done N" (N taken) and the folder it was
 * left in, "FPT dir PATH". The pictures' times as "FPT picture NAME ..."
 * (the spec's log). Built with -DFP_OLD, the same with EPlay's own
 * addfiles.c, for run.py --old (the move changes no pixel).
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "text.h"
#ifdef FP_OLD
#include "addfiles.h"
#define FP_RECENT AF_RECENT
#define FP_PATH AF_PATH
#else
#include "filepick.h"
#endif

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes, gl_wchar, gl_hchar, ap_id;

static int idles;

static void take(const char *path)
{
	char b[FP_PATH + 16];
	snprintf(b, sizeof(b), "FPT took %s\n", path);
	nf_debug(b);
}

static void idle(void)
{
	idles++;
}

int main(void)
{
	static char recent[FP_RECENT][FP_PATH];
	char dir[FP_PATH], b[FP_PATH + 32];
	short wo[57], d;
	int n;
#ifndef FP_OLD
	static const DL_TYPE types[] = { { "Music files", "MP3 WAV AIF AIFF AIFC M3U" }, { "MP3", "MP3" },
					 { "WAV", "WAV" }, { "AIFF", "AIF AIFF AIFC" }, { "Playlists", "M3U" },
					 { "All files", "" } };
	static const FP_SPEC spec = { "Add files", "Add", "No folder added from yet", "FPT", types, 6 };
#endif
	ap_id = appl_init();
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, wo);
	scr_w = wo[0] + 1;
	scr_h = wo[1] + 1;
	vq_extnd(vdi_h, 1, wo);
	planes = wo[4];
	vst_alignment(vdi_h, 0, 5);
	text_font(vdi_h, 1, cw, ch);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(ARROW, 0);
	strcpy(recent[0], "C:\\MUSIC\\LIVE\\");
	strcpy(dir, "C:\\MUSIC\\");
	{	/* a drive D: to P: with a folder fptlong (run.py's --ext2 of the XaAES rig, long names): there */
		long map = Drvmap();
		short k;
		for (k = 3; k < 16; k++) {
			char p[16];
			long a;
			if (!(map & (1L << k)))
				continue;
			strcpy(p, "x:\\fptlong");
			p[0] = (char)('a' + k);
			a = Fattrib(p, 0, 0);
			if (a >= 0 && (a & 0x10)) {
				strcpy(dir, "x:\\fptlong\\");
				dir[0] = (char)('a' + k);
				strcpy(recent[0], dir);
				break;
			}
		}
	}
	if (Fopen("C:\\FPSYS.TXT", 0) >= 0)		/* the setting "Use the system file selector", for run.py */
		filepick_system = 1;
	nf_debug("FPT open\n");
#ifdef FP_OLD
	n = addfiles_run(dir, recent, take, idle);
#else
	n = filepick_run(&spec, dir, recent, take, idle);
#endif
	snprintf(b, sizeof(b), "FPT done %d, idle %s\n", n, idles > 0 ? "called" : "never");
	nf_debug(b);
	snprintf(b, sizeof(b), "FPT dir %s; recent %s\n", dir, recent[0]);
	nf_debug(b);
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
