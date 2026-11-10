/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * SETDEMO.PRG: the family's Settings dialog (ui/settings.c) in an emulator, for run.py. Two dialogs with the real
 * pages of two apps, their words copied from the sources:
 *
 *   key 1   EMail 2.14's Settings (ui/dialogs.c: part_mail ... part_connection): six pages, 57 x 17
 *   key 2   EHypView 1.39's Settings (src/gem/settings.c): General and Library, 57 x 18
 *   key 3   the radio buttons and the keyboard's reach (D24, D25): an Output page, as EPlay's, with a group of three
 *           choices (the last greyed while the "no DSP" tick is on), a pop-up with a greyed item, and a plain page
 *   key 4   the Font dialog (ui/fontsel.c: the title band, Cancel and OK)
 *   key q   ends
 *
 * run.py opens them on TOS 1.04 (ST:mono, flat) and TOS 4.04 (Falcon:16, 3D), takes a screenshot of each page
 * and compares it with one of the real program's, taken the same way. Each is also a speed test (a build
 * with -DAES_PROFILE=20 says each stretch between event calls: the dialog's opening is the key's). It says
 * "SETDEMO ready", "SETDEMO box NAME X Y W H" when a dialog is up, "SETDEMO got NAME R ..." when it closes.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "text.h"
#include "dialog.h"
#include "settings.h"
#include "mbar.h"
#include "fontsel.h"

extern short text_round;
short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes, gl_wchar, gl_hchar, ap_id;

static const char *now_name;

static void say(const char *fmt, const char *name, short a, short b, short c, short d)
{
	char buf[120];
	snprintf(buf, sizeof(buf), fmt, name, a, b, c, d);
	nf_debug(buf);
}

/* tells run.py where the box is, as it opens */
static void drawn(OBJECT *tree, short band)
{
	short x, y;
	(void)band;
	objc_offset(tree, 0, &x, &y);
	say("SETDEMO box %s %d %d %d %d\n", now_name, x, y, tree[0].ob_width, tree[0].ob_height);
}

/* ============ EMail 2.14: ui/dialogs.c, settings_sidebar and part_* ============ */

static char s_zlabel[42], s_check[4], s_page[5], s_wrap[3];
#define SIG_ROWS 4
#define SIG_W 40
#define SIG_CAP 78
static char s_sig[SIG_ROWS][SIG_CAP + 1];
#define PDIR_W 32
static char s_pdir[100];
static short s_on[7];
enum { T_KEEP, T_LOG, T_FALCON, T_DSP, T_COLOUR, T_PFILE, T_START };

static const char *const lang_name[] = { "English", "Hebrew", "German", "French", "Spanish", "Italian",
					 "Portuguese", "Dutch", "Russian", "Arabic" };
static short s_lang;

/* EMail's zone_label: the zone's name, its offset right-aligned, "..." (a long list is a dialog: no arrow, 5.7) */
static void zone_label(void)
{
	const char *name = "Europe/Moscow", *off = "GMT+3";
	short n = (short)strlen(name), o = (short)strlen(off);
	memset(s_zlabel, ' ', 28);
	s_zlabel[28] = 0;
	memcpy(s_zlabel + 1, name, n);
	memcpy(s_zlabel + 24 - o, off, o);
	memcpy(s_zlabel + 25, "...", 3);
}

static int acts;
static void zone_act(SETTINGS *s, void *user)
{
	(void)s, (void)user;
	acts++;
}

static void part_mail(SETTINGS *s, short x, short y, void *user)
{
	(void)y, (void)user;
	set_text(s, x, set_y(s, 0), "Check for new mail every");
	set_field(s, x + SET_FX, set_y(s, 0), s_check, 3, '9');
	set_text(s, x + SET_FX + 4, set_y(s, 0), "min");
	set_text(s, x, set_y(s, 1), "Messages to load at a time");
	set_field(s, x + SET_FX, set_y(s, 1), s_page, 4, '9');
	set_tick(s, x, set_y(s, 2), "Keep read messages on disk", &s_on[T_KEEP]);
	set_tick(s, x, set_y(s, 3), "Start offline, with the mail on disk", &s_on[T_START]);
}

static void part_writing(SETTINGS *s, short x, short y, void *user)
{
	(void)y, (void)user;
	set_text(s, x, set_y(s, 0), "Wrap my lines at column");
	set_field(s, x + SET_FX, set_y(s, 0), s_wrap, 2, '9');
}

/* four rows in one frame, as one box to write in */
static void part_signature(SETTINGS *s, short x, short y, void *user)
{
	short k;
	(void)user;
	set_frame(s, x, set_y(s, 0), SIG_W, SIG_ROWS);
	for (k = 0; k < SIG_ROWS; k++)
		set_long_bare(s, x, (short)((y + k) | (2 << 8)), s_sig[k], SIG_W, SIG_CAP, 0);
	set_text(s, x, (short)(y + SIG_ROWS + 1), "Added to new messages, replies and");
	set_text(s, x, (short)(y + SIG_ROWS + 2), "forwards from every account.");
}

static void part_look(SETTINGS *s, short x, short y, void *user)
{
	(void)y, (void)user;
	set_tick(s, x, set_y(s, 0), "Use colours", &s_on[T_COLOUR]);
	set_text(s, x + 3, set_y(s, 1), "on screens with 16 colours or more:");
	set_text(s, x + 3, set_y(s, 2), "selection, unread counts, flags,");
	set_text(s, x + 3, set_y(s, 3), "quoted text, links; with four");
	set_text(s, x + 3, set_y(s, 4), "colours, links in blue");
}

static void part_contacts(SETTINGS *s, short x, short y, void *user)
{
	(void)y, (void)user;
	set_tick(s, x, set_y(s, 0), "When EContact isn't loaded, read", &s_on[T_PFILE]);
	set_text(s, x + 3, set_y(s, 1), "its contacts file (CONTACTS.TXT)");
	set_text(s, x, set_y(s, 3), "EContact's folder:");
	set_long(s, x, set_y(s, 4), s_pdir, PDIR_W, (short)(sizeof(s_pdir) - 1), 0);
	set_button(s, (short)(x + PDIR_W + 2), set_y(s, 4), 5, "...", zone_act, 0);	/* to column 39 */
	set_text(s, x, set_y(s, 6), "With EContact loaded, EMail asks it.");
}

static void part_connection(SETTINGS *s, short x, short y, void *user)
{
	(void)y, (void)user;
	set_text(s, x, set_y(s, 0), "Time zone");
	zone_label();
	set_value(s, (short)(x + 11), set_y(s, 0), 29, s_zlabel, zone_act, 0);
	set_tick(s, x, set_y(s, 1), "Direct connection: TLS on this Atari", &s_on[T_FALCON]);
	set_text(s, (short)(x + 3), set_y(s, 2), "(68020 or better); off: the Gateway");
	set_tick(s, (short)(x + 3), set_y(s, 3), "Use the DSP for the signatures", &s_on[T_DSP]);
	set_tick(s, x, set_y(s, 4), "Write a protocol log (EMAIL.LOG)", &s_on[T_LOG]);
	set_text(s, x, set_y(s, 5), "Translate messages into");
	set_popup(s, (short)(x + SET_FX - 4), set_y(s, 5), 14, lang_name, 10, &s_lang);
	set_button(s, x, set_y(s, 6), 14, "Languages...", zone_act, 0);
}

static void email_values(void)
{
	/* the defaults of EMail's store.c, as a first start has them, and tz=180 */
	strcpy(s_check, "0");
	strcpy(s_page, "100");
	strcpy(s_wrap, "72");
	s_pdir[0] = 0;
	s_sig[0][0] = 0;
	strcpy(s_sig[0], "Dana");
	strcpy(s_sig[1], "Falcon owner");
	s_sig[2][0] = s_sig[3][0] = 0;
	s_on[T_KEEP] = 1;
	s_on[T_START] = 1;
	s_on[T_LOG] = 0;
	s_on[T_FALCON] = 0;
	s_on[T_DSP] = 1;
	s_on[T_COLOUR] = 1;
	s_on[T_PFILE] = 0;
	s_lang = 0;
}

static int email_dialog(void)
{
	static const SET_PAGE pages[] = { { "Mail", part_mail }, { "Writing", part_writing }, { "Signature", part_signature },
					  { "Look", part_look }, { "Contacts", part_contacts },
					  { "Connection", part_connection } };
	SET_DEF d;
	email_values();
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = 6;
	d.height = 17;
	return settings_dialog(&d);
}

/* ============ EHypView 1.39: src/gem/settings.c ============ */

static const char *const start_name[] = { "When first opened", "At start", "After a minute", "After 5 minutes" };
static const char *const layout_name[] = { "Outline", "Two panes" };
#define NPATH 3
#define PLEN 40
#define PCAP 128
static short start_sel, layout_sel;
static char paths[NPATH][PCAP + 1];
static char folder_text[PLEN + 1];

static void hv_general(SETTINGS *s, short x, short y, void *user)
{
	short gap = (short)(gl_hchar / 2);
	(void)y, (void)user;
	set_text(s, x, 4, "The accessory looks through the library:");
	set_popup(s, (short)(x + 2), (short)(5 | (gap << 8)), 23, start_name, 4, &start_sel);
	set_text(s, x, (short)(7 | (gap << 8)), "The program looks as it starts.");
	set_text(s, x, (short)(9 | (gap << 8)), "EHypView's folder (FOLDER=):");
	set_text(s, (short)(x + 2), (short)(10 | (gap << 8)), folder_text);
	set_text(s, x, (short)(11 | (gap << 8)), "keeps the library's index, EHYPVIEW.IDX,");
	set_text(s, x, (short)(12 | (gap << 8)), "and the fonts measured, EHYPVIEW.FNT.");
}

static void hv_library(SETTINGS *s, short x, short y, void *user)
{
	short gap = (short)(gl_hchar / 2), k;
	(void)y, (void)user;
	set_text(s, x, 4, "Layout:");
	set_popup(s, (short)(x + 9), 4, 15, layout_name, 2, &layout_sel);
	set_text(s, x, (short)(6 | (gap << 8)), "Also look for hypertexts in (PATHS=):");
	for (k = 0; k < NPATH; k++)
		set_long(s, x, (short)((7 + k) | ((gap + 2 + k * (gl_hchar / 4 + 2)) << 8)), paths[k], PLEN, PCAP, 0);
	set_text(s, x, (short)(11 | (gap << 8)), "Each with its subfolders, as well as");
	set_text(s, x, (short)(12 | (gap << 8)), "PATHS, HYPFOLD and REF in ST-GUIDE.INF,");
	set_text(s, x, (short)(13 | (gap << 8)), "and \\GUIDES and \\GEMSYS\\GUIDES on every");
	set_text(s, x, (short)(14 | (gap << 8)), "drive.");
}

static int ehypview_dialog(void)
{
	static const SET_PAGE pages[] = { { "General", hv_general }, { "Library", hv_library } };
	SET_DEF d;
	int k;
	short j;
	text_round = 0;			/* EHypView's text_at draws where it is told (this stand-in rounds to a byte, as EPlay's) */
	start_sel = 0;
	layout_sel = 0;
	for (j = 0; j < NPATH; j++)
		paths[j][0] = 0;
	strcpy(folder_text, "C:\\EHYPVIEW");
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = 2;
	d.height = 18;
	k = settings_dialog(&d);
	text_round = 1;
	return k;
}


/* ============ the radio buttons and the keyboard's reach: an EPlay-like Output page ============ */

static short rd_out, rd_nodsp, rd_rate, rd_tick2, rd_log, rd_acts;
static char rd_name[12];
static const char *const rd_label[] = { "Auto", "FDI", "DSP" };
static const char *const rd_rates[] = { "49 kHz", "32 kHz", "25 kHz", "12 kHz" };

/* DSP applies unless the tick says this machine has none */
static int rd_when(SETTINGS *s, short obj, short c, void *user)
{
	(void)s, (void)obj, (void)user;
	return c != 2 || !rd_nodsp;
}

static void rd_act(SETTINGS *s, void *user)
{
	(void)s, (void)user;
	rd_acts++;
}

static void rd_output(SETTINGS *s, short x, short y, void *user)
{
	short r;
	(void)user;
	set_text(s, x, y, "Player name:");
	set_field(s, (short)(x + 14), y, rd_name, 11, 'X');
	set_text(s, x, set_y(s, 1), "Output:");
	r = set_radio(s, x, set_y(s, 2), 9, rd_label, 3, &rd_out);
	set_choice_when(s, r, rd_when, 0);
	set_tick(s, x, set_y(s, 3), "This machine has no DSP", &rd_nodsp);
	set_tick(s, x, set_y(s, 4), "Write a log", &rd_log);
	set_text(s, x, set_y(s, 5), "Sample rate:");
	r = set_popup(s, (short)(x + 14), set_y(s, 5), 14, rd_rates, 4, &rd_rate);
	set_popup_enable(s, r, 0, 0);
	set_button(s, x, set_y(s, 7), 14, "Languages...", rd_act, 0);
}

static void rd_plain(SETTINGS *s, short x, short y, void *user)
{
	(void)user;
	set_text(s, x, y, "Nothing to type here.");
	set_tick(s, x, set_y(s, 1), "Another tick", &rd_tick2);
}

static int radios_dialog(void)
{
	static const SET_PAGE pages[] = { { "Output", rd_output }, { "Plain", rd_plain } };
	SET_DEF d;
	int k;
	rd_out = 0, rd_nodsp = 0, rd_rate = 1, rd_tick2 = 0, rd_log = 0, rd_acts = 0;
	strcpy(rd_name, "Dana");
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = 2;
	d.height = 17;
	k = settings_dialog(&d);
	{
		char b[160];
		snprintf(b, sizeof(b), "SETDEMO radios out %d nodsp %d rate %d log %d tick2 %d acts %d name '%s'\n", rd_out, rd_nodsp, rd_rate,
			 rd_log, rd_tick2, rd_acts, rd_name);
		nf_debug(b);
	}
	return k;
}

/* ============ the Font dialog (ui/fontsel.c): its title band and its Cancel and OK, for the screenshots ============ */

static void font_apply(fontsel *f)
{
	char b[100];
	snprintf(b, sizeof(b), "SETDEMO font applied id %d pt %d\n", f->id, f->pt);
	nf_debug(b);
}

static int font_dialog(void)
{
	static fontsel f;
	memset(&f, 0, sizeof(f));
	f.app = "SETDEMO";
	f.id = 1;
	f.pt = gl_hchar >= 16 ? 16 : 8;
	f.gdos = FS_UNLOADED;
	f.apply = font_apply;
	return fontsel_run(&f);
}

/* Where the AES runs several programs (XaAES), keys go to the program whose window is on top: a window of the
   demo's own, white, over the desktop; the dialog is a window of its own over it (dlg_redraw_hook draws this one) */
static short mainwin;

static void paint_main(const short *msg)
{
	short x, y, w, h, c[4], pxy[4];
	if (msg[3] != mainwin)
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vsf_interior(vdi_h, 1);
	vsf_color(vdi_h, 0);
	vsf_perimeter(vdi_h, 0);
	vswr_mode(vdi_h, 1);
	wind_get(mainwin, WF_FIRSTXYWH, &x, &y, &w, &h);
	while (w > 0 && h > 0) {
		c[0] = x > msg[4] ? x : msg[4];
		c[1] = y > msg[5] ? y : msg[5];
		c[2] = (x + w < msg[4] + msg[6] ? x + w : msg[4] + msg[6]) - 1;
		c[3] = (y + h < msg[5] + msg[7] ? y + h : msg[5] + msg[7]) - 1;
		if (c[2] >= c[0] && c[3] >= c[1]) {
			pxy[0] = c[0], pxy[1] = c[1], pxy[2] = c[2], pxy[3] = c[3];
			vr_recfl(vdi_h, pxy);
		}
		wind_get(mainwin, WF_NEXTXYWH, &x, &y, &w, &h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

/* a menu bar where the AES runs several programs (the dialogs are windows there; MD17): a title chosen while one is open is
   dropped by dlg_evnt, so "SETDEMO menu T I" is never said for it; one chosen otherwise says it */
static MBAR mbar;
static const MBAR_ITEM mb_desk[] = { { "About SETDEMO...", 0, 1 }, { 0 } };
static const MBAR_ITEM mb_test[] = { { "Do it", "^D", 2 }, { "Do more", 0, 3 }, { 0 } };
static const MBAR_TITLE mb_bar[] = { { " Desk ", mb_desk }, { " Test ", mb_test }, { 0 } };
static int have_bar;

int main(void)
{
	short wo[57], d, m[8];
	AEVENT e;
	int run = 1;
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
	/* as an app has drawn its window's text before it opens a dialog: text.c checks its fast path on the first
	   draw (scr_cpu_wins), which dialog.c's DLG_CPU path asks */
	text_plain(vdi_h, 8, 100, "SETDEMO", 7);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	graf_mouse(ARROW, 0);
	dlg_title = "SETDEMO";
	settings_drawn = drawn;
	if (dlg_windowed()) {
		short x, y, w, h;
		mainwin = wind_create(NAME | MOVER | CLOSER, desk_x, desk_y, desk_w, desk_h);
		wind_set_str(mainwin, WF_NAME, "SETDEMO");
		wind_open(mainwin, desk_x, desk_y, desk_w, desk_h);
		wind_get(mainwin, WF_WORKXYWH, &x, &y, &w, &h);
		dlg_redraw_hook = paint_main;
		if (mbar_build(&mbar, mb_bar, gl_wchar, gl_hchar, scr_w, scr_h) == 0) {
			menu_bar(mbar.tree, 1);
			have_bar = 1;
		}
	}
	say("SETDEMO screen %s %d %d %d %d\n", "", scr_w, scr_h, gl_wchar, gl_hchar);
	nf_debug("SETDEMO ready\n");
	while (run) {
		evnt_multi_(MU_KEYBD | MU_MESAG, 1, 1, 1, 0, m, &e);
		if (e.which & MU_MESAG) {
			if (m[0] == WM_REDRAW)
				paint_main(m);
			else if (m[0] == MN_SELECTED) {
				char b[60];
				snprintf(b, sizeof(b), "SETDEMO menu %d %d\n", m[3], m[4]);
				nf_debug(b);
				if (have_bar)
					menu_tnormal(mbar.tree, m[3], 1);
			} else if (m[0] == WM_TOPPED || m[0] == WM_NEWTOP)
				wind_set(m[3], WF_TOP, m[3], 0, 0, 0);
			else if (m[0] == WM_CLOSED || m[0] == AP_TERM)
				run = 0;
		}
		if (e.which & MU_KEYBD) {
			int r = -1;
			switch (e.kreturn & 0xff) {
			case '1':
				now_name = "email";
				r = email_dialog();
				break;
			case '2':
				now_name = "ehypview";
				r = ehypview_dialog();
				break;
			case '3':
				now_name = "radios";
				r = radios_dialog();
				break;
			case '4':
				now_name = "font";
				r = font_dialog();
				break;
			case 'q':
				run = 0;
				break;
			}
			if (r >= 0) {
				char b[200];
				snprintf(b, sizeof(b), "SETDEMO got %s %d: check '%s' page '%s' wrap '%s' sig '%s' start %d layout %d zone-acts %d\n",
					 now_name, r, s_check, s_page, s_wrap, s_sig[0], start_sel, layout_sel, acts);
				nf_debug(b);
			}
		}
	}
	nf_debug("SETDEMO done\n");
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
