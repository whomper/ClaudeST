/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/* ICONTEST.PRG - does the AES iconify windows? (FreeMiNT + XaAES check)
 *
 * Opens one window with every gadget including the iconifier (SMALLER),
 * reports appl_getinfo(11) and every message it gets on the emulator's
 * console through NatFeats ("ICT ..." lines), and answers WM_ICONIFY /
 * WM_UNICONIFY with wind_set(WF_ICONIFY / WF_UNICONIFY). It draws
 * "NORMAL" or "ICON" in the work area so a screenshot shows the state.
 * Ends on WM_CLOSED or AP_TERM, or after 10 minutes.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"

static short vh, win = -1, iconified;
static char line[160];

static void say(void) { strcat(line, "\n"); nf_debug(line); }

static void redraw(short x, short y, short w, short h)
{
	short wx, wy, ww, wh, r[4], clip[4], p[4];
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	wind_update(BEG_UPDATE);
	v_hide_c(vh);
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	while (r[2] > 0 && r[3] > 0) {
		short x1 = r[0] > x ? r[0] : x, y1 = r[1] > y ? r[1] : y;
		short x2 = r[0] + r[2] < x + w ? r[0] + r[2] : x + w;
		short y2 = r[1] + r[3] < y + h ? r[1] + r[3] : y + h;
		if (x2 > x1 && y2 > y1) {
			clip[0] = x1; clip[1] = y1; clip[2] = x2 - 1; clip[3] = y2 - 1;
			vs_clip(vh, 1, clip);
			vsf_interior(vh, FIS_SOLID);
			vsf_color(vh, iconified ? 2 : 0);	/* red when iconified */
			p[0] = wx; p[1] = wy; p[2] = wx + ww - 1; p[3] = wy + wh - 1;
			vr_recfl(vh, p);
			vswr_mode(vh, MD_TRANS);
			vst_color(vh, 1);
			if (iconified)
				v_gtext_n(vh, wx + 2, wy + wh / 2 + 4, "ICON", 4);
			else
				v_gtext_n(vh, wx + 16, wy + 24, "NORMAL - click the iconifier", 28);
			vs_clip(vh, 0, clip);
		}
		wind_get(win, WF_NEXTXYWH, &r[0], &r[1], &r[2], &r[3]);
	}
	v_show_c(vh, 1);
	wind_update(END_UPDATE);
}

static void where(const char *what)
{
	short x, y, w, h;
	wind_get(win, WF_CURRXYWH, &x, &y, &w, &h);
	sprintf(line, "ICT %s window %d at %d,%d %dx%d iconified=%d", what, win, x, y, w, h, iconified);
	say();
}

int main(void)
{
	short ap, o1, o2, o3, o4, r, work_out[57], msg[8], dummy, i;
	AEVENT ev;
	long ticks = 0;

	ap = appl_init();
	sprintf(line, "ICT start apid=%d aes_version=%04x", ap, (unsigned short)aes_version());
	say();
	{
		char path[128];
		path[0] = 0;
		Dgetpath(path, 0);
		sprintf(line, "ICT cwd %c:%s", 'A' + (short)Dgetdrv(), path);
		say();
	}
	o1 = o2 = o3 = o4 = 0;
	r = appl_getinfo(11, &o1, &o2, &o3, &o4);
	sprintf(line, "ICT appl_getinfo(11) ret=%d w1=%04x w2=%04x w3=%04x w4=%04x WF_ICONIFY=%s",
		r, (unsigned short)o1, (unsigned short)o2, (unsigned short)o3, (unsigned short)o4,
		(r && (o1 & 0x80)) ? "yes" : "no");
	say();
	o1 = o2 = o3 = o4 = 0;
	r = appl_getinfo(12, &o1, &o2, &o3, &o4);
	sprintf(line, "ICT appl_getinfo(12) ret=%d w1=%04x w2=%04x WM_ICONIFY=%s",
		r, (unsigned short)o1, (unsigned short)o2, (r && (o1 & 0x80)) ? "yes" : "no");
	say();

	vh = graf_handle(&dummy, &dummy, &dummy, &dummy);
	vh = v_opnvwk_(vh, work_out);
	win = wind_create(NAME | CLOSER | FULLER | MOVER | SIZER | SMALLER | INFO, 0, 20, 640, 460);
	wind_set_str(win, WF_NAME, " Iconify test ");
	wind_set_str(win, WF_INFO, " ICONTEST ");
	wind_open(win, 120, 100, 360, 200);
	graf_mouse(ARROW, 0);
	where("opened");

	for (;;) {
		evnt_multi_(MU_MESAG | MU_KEYBD | MU_TIMER, 0, 0, 0, 1000, msg, &ev);
		if (ev.which & MU_KEYBD) {
			sprintf(line, "ICT key %04x shift %x", (unsigned short)ev.kreturn, ev.kstate);
			say();
		}
		if (!(ev.which & MU_MESAG)) {
			if (++ticks > 600)
				break;
			continue;
		}
		sprintf(line, "ICT msg %d from %d: %d %d %d %d %d", msg[0], msg[1],
			msg[3], msg[4], msg[5], msg[6], msg[7]);
		say();
		if (msg[3] != win && msg[0] != AP_TERM)
			continue;
		switch (msg[0]) {
		case WM_REDRAW:
			redraw(msg[4], msg[5], msg[6], msg[7]);
			break;
		case WM_TOPPED:
			wind_set(win, WF_TOP, 0, 0, 0, 0);
			break;
		case WM_MOVED:
		case WM_SIZED:
			wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			where("moved");
			break;
		case WM_ICONIFY:
		case WM_ALLICONIFY:
			iconified = 1;
			r = wind_set(win, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
			sprintf(line, "ICT WF_ICONIFY ret=%d", r);
			say();
			where("iconified");
			break;
		case WM_UNICONIFY:
			iconified = 0;
			r = wind_set(win, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
			sprintf(line, "ICT WF_UNICONIFY ret=%d", r);
			say();
			where("uniconified");
			break;
		case WM_CLOSED:
		case AP_TERM:
			goto out;
		}
		for (i = 0; i < 8; i++)
			msg[i] = 0;
	}
out:
	strcpy(line, "ICT done");
	say();
	wind_close(win);
	wind_delete(win);
	v_clsvwk(vh);
	appl_exit();
	return 0;
}
