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
#include "dialog.h"
#include "fsel.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */
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

static void box_draw(short x, short y, short w, short h)
{
	short p[4];
	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	vsf_color(vdi_h, 1);
	vsf_interior(vdi_h, 2);
	vsf_style(vdi_h, 4);
	vr_recfl(vdi_h, p);
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
#ifndef FP_OLD
	if (Fopen("C:\\FPSAVE.TXT", 0) >= 0) {	/* save mode (filepick_save), for save.py */
		static const DL_TYPE pl[] = { { "Playlists", "M3U" }, { "All files", "" } };
		static const FP_SPEC sspec = { "Save the list as a playlist", "Save", "No folder saved in yet", "FPT", pl, 2 };
		static char name[64] = "PLAYLIST.M3U", path[FP_PATH + 64];
		int round, rounds = Fopen("C:\\FPROUNDS.TXT", 0) >= 0 ? 6 : 1;
		if (Fopen("C:\\FPWIN.TXT", 0) >= 0) {
			/* rounds.py --win: a window of the program's under the dialogs, as an app's (EWeb's) */
			short wh = wind_create(0x0B, desk_x, desk_y, desk_w, desk_h);
			if (wh > 0)
				wind_open(wh, desk_x, desk_y, desk_w, desk_h);
			nf_debug(wh > 0 ? "FPT window open\n" : "FPT no window\n");
		}
		for (round = 0; round < rounds; round++) {
		if (Fopen("C:\\FPPRE.TXT", 0) >= 0) {
			/* rounds.py --pre: a press taken as EWeb's main loop takes a link's (any button, 0x101, 3,
			   0) and no wait for its release before the dialog opens (EWeb asks where to save once the
			   file's answer has begun) */
			short m[8];
			AEVENT e;
			nf_debug("FPT pre ready\n");
			while (!(evnt_multi_(MU_BUTTON | MU_TIMER, 0x101, 3, 0, 20000, m, &e) & MU_BUTTON))
				;
			nf_debug("FPT pre pressed\n");
		}
		n = filepick_save(&sspec, dir, recent, name, sizeof(name), path, sizeof(path), idle);
		snprintf(b, sizeof(b), "FPT saved %d %s\n", n, n ? path : "-");
		nf_debug(b);
		if (rounds > 1) {
			/* after.py --rounds: a box over the screen after each save, one press in it, as EWeb's
			   download box after each file saved */
			short m[8], bx = 160, by = 120, bw = 320, bh = 160, j = 0;
			AEVENT e;
			unsigned long t1 = tos_ms();
			fsel_settle();
			dlg_open_area(box_draw, &bx, &by, &bw, &bh);
			snprintf(b, sizeof(b), "FPT round %d box\n", round);
			nf_debug(b);
			while (tos_ms() - t1 < 20000 && !j) {
				short w = dlg_evnt(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 5, m, &e);
				if (w & MU_BUTTON) {
					short mb, ks, mx, my;
					unsigned long t2 = tos_ms();
					j = 1;
					do
						graf_mkstate(&mx, &my, &mb, &ks);
					while ((mb & 1) && tos_ms() - t2 < 10000);
					snprintf(b, sizeof(b), "FPT round %d press %s\n", round, mb & 1 ? "STUCK" : "let go");
					nf_debug(b);
				}
			}
			dlg_close();
			if (!j) {
				snprintf(b, sizeof(b), "FPT round %d no press\n", round);
				nf_debug(b);
			}
			continue;
		}
		if (Fopen("C:\\FPAFTER.TXT", 0) >= 0) {	/* after.py: the presses that come after the dialog */
			short m[8];
			AEVENT e;
			unsigned long t0 = tos_ms();
			int k = 0, ewloop = Fopen("C:\\FPEWLOOP.TXT", 0) >= 0;
			fsel_settle();			/* as EWeb does after it */
			if (Fopen("C:\\FPBOX.TXT", 0) >= 0) {	/* a box over the screen first (EWeb's download box) */
				short bx = 160, by = 120, bw = 320, bh = 160, j = 0;
				unsigned long t1 = tos_ms();
				dlg_open_area(box_draw, &bx, &by, &bw, &bh);
				snprintf(b, sizeof(b), "FPT box %d %d %d %d\n", bx, by, bw, bh);
				nf_debug(b);
				while (tos_ms() - t1 < 20000 && j < 3) {
					short w = dlg_evnt(MU_MESAG | MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 5, m, &e);
					if (w & MU_BUTTON) {
						snprintf(b, sizeof(b), "FPT boxpress %d %d %d\n", ++j, e.mx, e.my);
						nf_debug(b);
						do
							graf_mkstate(&e.mx, &e.my, &e.mbutton, &e.kstate);
						while (e.mbutton & 1);
					}
				}
				dlg_close();
				snprintf(b, sizeof(b), "FPT box done %d\n", j);
				nf_debug(b);
			}
			nf_debug("FPT after ready\n");
			while (tos_ms() - t0 < 30000 && k < 6) {
				short w;
				if (ewloop) {
					/* EWeb's main loop: any button pressed (0x101, 3, 0), the pointer's leaving
					   of a 1x1 box, then the keys alone with no wait */
					short mx, my, mb, ks;
					graf_mkstate(&mx, &my, &mb, &ks);
					evnt_set_m1(1, mx, my, 1, 1);
					w = evnt_multi_(MU_KEYBD | MU_BUTTON | MU_M1 | MU_MESAG | MU_TIMER, 0x101, 3, 0, 200, m, &e);
					if (!(w & MU_BUTTON))
						evnt_multi_(MU_KEYBD | MU_TIMER, 0x101, 3, 0, 0, m, &e);
				} else
					w = evnt_multi_(MU_BUTTON | MU_TIMER, 1, 1, 1, 50, m, &e);
				if (w & MU_BUTTON) {
					snprintf(b, sizeof(b), "FPT press %d %d %d\n", ++k, e.mx, e.my);
					nf_debug(b);
					/* let go first, as a window loop does (5.8: done on the release) */
					evnt_multi_(MU_BUTTON, 1, 1, 0, 0, m, &e);
				}
			}
			snprintf(b, sizeof(b), "FPT after %d\n", k);
			nf_debug(b);
		}
		}
		appl_exit();
		return 0;
	}
#endif
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
