/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ABOUT.PRG: the family's About box (ui/aboutbox.c) on its own, for run.py:
 * an app's icon in its three depths (make ICON=NAME: common/icons/NAME.txt
 * through tools/appicon.py), two status lines and a line of fine print,
 * over a desktop with a window-sized pattern under it (the box puts back
 * what it covered). On the console (NatFeats) it says "ABOUT ground" once
 * that is drawn and waits for a key (run.py's shot of what the box will
 * cover, then Space), "ABOUT open" just before about_show (its shot of the
 * box, then Escape) and "ABOUT closed" when the box is gone, and waits for
 * a key again before it ends (its shot of what was put back). Built
 * with -DAES_PROFILE=20 (gem.c), the box's stretches between AES calls
 * come out as AESGAP lines.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "text.h"
#include "aboutbox.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes, gl_wchar, gl_hchar, ap_id;

/* something under the box that isn't white, in more than one pen: rows of
   text and a filled block, so that a box put back wrong shows */
static void ground(void)
{
	GRECT r;
	short y, n = 0;
	static const char line[] = "The quick brown fox jumps over the lazy dog 0123456789 ";

	r.x = desk_x;
	r.y = desk_y;
	r.w = desk_w;
	r.h = desk_h;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	for (y = desk_y + 2; y + ch <= desk_y + desk_h; y = (short)(y + ch + 2), n++)
		text_at(desk_x + 8, y, line + (n % 11), (long)(sizeof(line) - 1 - n % 11), (short)((desk_w - 16) / cw), 0);
	r.x = (short)(desk_x + desk_w / 3);
	r.y = (short)(desk_y + desk_h / 3);
	r.w = (short)(desk_w / 3);
	r.h = (short)(desk_h / 3);
	fill(&r, planes >= 4 ? 2 : 1);
	clip_off();
	graf_mouse(M_ON, 0);
}

/* until a key comes (run.py's, after a shot) */
static void key(void)
{
	short msg[8];
	AEVENT e;

	evnt_multi_(MU_KEYBD, 0, 0, 0, 0, msg, &e);
}

int main(void)
{
	static const ABOUT a = { "About test", "1.0", "The About box on its own (tests/about)",
				 { "Status line one: 512 KB free", "Status line two", 0 },
				 { "Atari is a trademark of Atari Interactive, Inc.", 0, 0 },
				 icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
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
	ground();
	nf_debug("ABOUT ground");
	key();
	evnt_timer_(200);
	nf_debug("ABOUT open");
	about_show(&a);
	draw_forget();
	nf_debug("ABOUT closed");
	key();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
