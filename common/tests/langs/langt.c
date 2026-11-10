/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * LANGT.PRG: the Languages dialog (ui/langs.c) in an emulator against a fake translategate
 * (fakegate.py), for run.py. The Gateway's address is C:\LANGT.CFG ("127.0.0.1 18480"); the network
 * is common's own (atari/net_tos.c on FAKESTNG, the serial bridge). Before each dialog it says
 * "LANGT ask NAME" on the console (NatFeats), run.py presses the keys and clicks for it, and after
 * it "LANGT got NAME RESULT job N add MB err TEXT". After an OK that gave a job the program follows
 * it as an app would: langs_job_step from its loop, the words and the progress bar in a status
 * line at the foot of the screen ("LANGT job TEXT / PERMILLE"), until it ends.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "text.h"
#include "plat.h"
#include "conn.h"
#include "dialog.h"
#include "panebar.h"
#include "progbar.h"
#include "langs.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes, gl_wchar, gl_hchar, ap_id;

static char host[40] = "127.0.0.1";
static unsigned short port = 18480;
static int nscen = 7;		/* how many of the scenarios to run (the third number of LANGT.CFG) */
static langs L;

static void say(const char *fmt, const char *name, int a, int b, int c, const char *s)
{
	char buf[240];
	snprintf(buf, sizeof(buf), fmt, name, a, b, c, s);
	nf_debug(buf);
}

static void config(void)
{
	long fh = Fopen("LANGT.CFG", 0);
	if (fh >= 0) {
		char b[80];
		long n = Fread((short)fh, sizeof(b) - 1, b);
		Fclose((short)fh);
		if (n > 0) {
			unsigned long p = 0;
			int i = 0;
			b[n] = 0;
			while (b[i] && b[i] != ' ' && i < (int)sizeof(host) - 1) {
				host[i] = b[i];
				i++;
			}
			host[i] = 0;
			while (b[i] == ' ')
				i++;
			while (b[i] >= '0' && b[i] <= '9')
				p = p * 10 + (unsigned long)(b[i++] - '0');
			if (p)
				port = (unsigned short)p;
			while (b[i] == ' ')
				i++;
			if (b[i] >= '0' && b[i] <= '9') {
				nscen = 0;
				while (b[i] >= '0' && b[i] <= '9')
					nscen = nscen * 10 + (b[i++] - '0');
			}
		}
	}
}

/* the app's status line at the foot of the screen: the words, and the progress bar (5.9) */
static void status(const char *text, short permille)
{
	GRECT r;
	short bx, bw, bh = gl_hchar - 2;
	r.x = desk_x;
	r.y = desk_y + desk_h - gl_hchar - 4;
	r.w = desk_w;
	r.h = gl_hchar + 4;
	graf_mouse(M_OFF, 0);
	fill(&r, 0);
	{
		GRECT l;
		l = r;
		l.h = 1;
		fill(&l, 1);
	}
	text_at(r.x + 4, r.y + 3, text, (long)strlen(text), 52, TX_LTR);
	bx = r.x + 4 + 52 * gl_wchar;
	bw = desk_w - (bx - r.x) - 8;
	if (bw > 40) {
		GRECT f;
		f.x = bx;
		f.y = r.y + 2;
		f.w = bw;
		f.h = bh;
		frame(&f, 1);
		progbar_fill(vdi_h, bx + 1, f.y + 1, bw - 2, bh - 2, 0, PB_FULL);	/* the whole bar's colours ... */
		{
			GRECT g;
			g.x = bx + 1 + (short)((long)(bw - 2) * permille / PB_FULL);
			g.y = f.y + 1;
			g.w = bx + bw - 1 - g.x;
			g.h = bh - 2;
			fill(&g, 0);		/* ... the ground beyond the filled part */
		}
	}
	graf_mouse(M_ON, 0);
}

static void follow(const langs *l)
{
	static langs_job j;
	char last[160];
	short lastp = -1;
	unsigned long t_end = tos_ms() + 120000UL;
	last[0] = 0;
	langs_job_begin(&j, l->host, l->port, l->job, l->add_mb, tos_ms());
	status(j.text, 0);
	while (j.state == LJ_RUNNING && tos_ms() < t_end) {
		int s;
		evnt_timer_(200);
		s = langs_job_step(&j, tos_ms());
		if (s && (strcmp(last, j.text) || langs_job_permille(&j) != lastp)) {
			char b[240];
			strcpy(last, j.text);
			lastp = langs_job_permille(&j);
			snprintf(b, sizeof(b), "LANGT job %s / %d\n", j.text, lastp);
			nf_debug(b);
			status(j.text, lastp);
		}
	}
	say("LANGT job ended %s state %d\n", "", j.state, 0, 0, "");
	evnt_timer_(1500);
}

static void ask(const char *name, int do_follow)
{
	static int n;
	int r;
	if (n++ >= nscen)
		return;
	say("LANGT ask %s\n", name, 0, 0, 0, "");
	r = langs_run(&L);
	say("LANGT got %s %d job %d add %d err %s\n", name, r, L.job, L.add_mb, L.err);
	if (r == 1 && L.job && do_follow)
		follow(&L);
	evnt_timer_(2500);		/* (run.py sets the fake Gateway up for the next one meanwhile) */
}

int main(void)
{
	short wo[57], d;
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
	dlg_title = "LANGT";
	progbar_pens(vdi_h, 0);
	pb_vdi(vdi_h);			/* as ECal and EContact do: the bar's fast way, on TOS's own screen */
	config();
	{
		/* a file FLAT beside the program: the flat look on a 3D AES; WINDOW: the dialog in a window of
		   the AES's, as on MiNT and MagiC, forced here (tests the window code where there is none) */
		long f = Fopen("FLAT", 0);
		if (f >= 0) {
			Fclose((short)f);
			dlg_3d_on = 0;
		}
		f = Fopen("WINDOW", 0);
		if (f >= 0) {
			Fclose((short)f);
			dlg_window_on = 1;
		}
	}
	if (net_init()) {
		char b[80];
		snprintf(b, sizeof(b), "LANGT net %s to %s:%u\n", net_stack(), host, (unsigned)port);
		nf_debug(b);
	} else {
		nf_debug("LANGT no network\n");
	}
	L.host = host;
	L.port = port;
	status("LANGT: the app's status line", 0);	/* (text drawn before: TOS's text path is checked once, as in an app that has its window up) */
	evnt_timer_(1500);
	/* the scenarios run.py drives, in this order */
	ask("cancel", 0);
	ask("over", 0);
	ask("keys", 1);
	ask("mouse", 1);
	ask("refused", 1);
	ask("stop", 0);
	ask("down", 0);
	nf_debug("LANGT done\n");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
