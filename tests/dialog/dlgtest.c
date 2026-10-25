/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * DLGT.PRG: the shared alert() and dialog loop in an emulator, for
 * run.py. Before each alert or dialog it says "DLGT ask NAME" on the
 * console (NatFeats), run.py presses the keys or clicks for it, and
 * after it "DLGT got NAME ANSWER TEXT SEL": the button (or -1), the
 * field's text and 1 when an exit button was left selected.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "alert.h"
#include "dialog.h"

enum { ROOT, TITLE, FIELD, OK, CANCEL, NOBJ };
static OBJECT tree[NOBJ];
static TEDINFO ted;
static char text[21], tmplt[] = "Name: ____________________", valid[] = "XXXXXXXXXXXXXXXXXXXX";

static void say(const char *fmt, const char *name, short r)
{
	char b[120];
	snprintf(b, sizeof(b), fmt, name, r, text,
		 ((tree[OK].ob_state | tree[CANCEL].ob_state) & SELECTED) ? 1 : 0);
	nf_debug(b);
}

static void obj(short i, short type, short flags, long spec, short x, short y, short w, short h)
{
	tree[i].ob_next = i + 1 < NOBJ ? i + 1 : 0;
	tree[i].ob_head = tree[i].ob_tail = -1;
	tree[i].ob_type = type;
	tree[i].ob_flags = flags;
	tree[i].ob_state = 0;
	tree[i].ob_spec = spec;
	tree[i].ob_x = x;
	tree[i].ob_y = y;
	tree[i].ob_width = w;
	tree[i].ob_height = h;
}

/* a folder's name: a field, OK (default) and Cancel */
static void make_tree(short cw, short ch)
{
	strcpy(text, "hello");
	ted.te_ptext = text;
	ted.te_ptmplt = tmplt;
	ted.te_pvalid = valid;
	ted.te_font = 3;
	ted.te_just = 0;
	ted.te_color = 0x1180;
	ted.te_thickness = 0;
	ted.te_txtlen = sizeof(text);
	ted.te_tmplen = sizeof(tmplt);
	obj(ROOT, G_BOX, 0, 0x00021100L, 0, 0, 30 * cw, 7 * ch);
	tree[ROOT].ob_next = -1;
	tree[ROOT].ob_head = 1;
	tree[ROOT].ob_tail = NOBJ - 1;
	tree[ROOT].ob_state = OUTLINED;
	obj(TITLE, G_STRING, 0, (long)"New folder", 2 * cw, ch, 10 * cw, ch);
	obj(FIELD, G_FTEXT, EDITABLE, (long)&ted, 2 * cw, 3 * ch, 26 * cw, ch);
	obj(OK, G_BUTTON, SELECTABLE | DEFAULT | EXIT, (long)"OK", 8 * cw, 5 * ch, 8 * cw, ch);
	obj(CANCEL, G_BUTTON, SELECTABLE | EXIT | LASTOB, (long)"Cancel", 19 * cw, 5 * ch, 8 * cw, ch);
}

static void ask_alert(const char *name, short def, const char *s)
{
	short r;
	say("DLGT ask %s\n", name, 0);
	r = alert(def, "%s", s);
	say("DLGT got %s %d %s %d\n", name, r);
}

static void ask_dialog(const char *name, short cancel)
{
	short r, cw, ch, bw, bh;
	graf_handle(&cw, &ch, &bw, &bh);
	make_tree(cw, ch);
	if (!strcmp(name, "d_band"))
		dlg_band(TITLE);	/* "New folder" names the window instead */
	say("DLGT ask %s\n", name, 0);
	r = dlg_modal(tree, 0, cancel);
	say("DLGT got %s %d %s %d\n", name, r);
}

int main(void)
{
	short cw, ch, bw, bh, x, y, w, h;
	char b[80];
	appl_init();
	dlg_title = "DLGT";
	graf_mouse(ARROW, 0);
	graf_handle(&cw, &ch, &bw, &bh);
	wind_get(0, WF_CURRXYWH, &x, &y, &w, &h);
	snprintf(b, sizeof(b), "DLGT screen %d %d cell %d %d\n", w, h, cw, ch);
	nf_debug(b);
	evnt_timer_(1500);
	ask_alert("a_return", 1, "[2][Delete the message?][Delete|Keep]");
	ask_alert("a_esc", 1, "[2][Delete the message?][Delete|Keep]");
	ask_alert("a_undo", 1, "[2][Delete the message?][Delete|Keep]");
	ask_alert("a_esc3", 1, "[3][Can't reach the server.|Check the network and try again.][Retry|Skip|Cancel]");
	ask_alert("a_click", 1, "[3][Can't reach the server.|Check the network and try again.][Retry|Skip|Cancel]");
	ask_alert("a_ok", 1, "[1][The folder was saved.][ OK ]");
	ask_dialog("d_esc", CANCEL);
	ask_dialog("d_return", CANCEL);
	ask_dialog("d_undo", CANCEL);
	ask_dialog("d_none", -1);
	ask_dialog("d_click", CANCEL);
	ask_dialog("d_close", CANCEL);
	ask_dialog("d_band", CANCEL);
	nf_debug("DLGT done\n");
	appl_exit();
	return 0;
}
