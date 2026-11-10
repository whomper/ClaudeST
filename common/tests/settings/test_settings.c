/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The family's Settings dialog (ui/settings.c, ui/dlgbuild.c; the guidelines, 6.1) on the build machine,
 * against a stand-in AES (aes_stub.c): the built tree's geometry, the pages, ticks, fields, pop-ups,
 * buttons, and OK, Cancel, Esc, Undo and Return in the order the guidelines (3.5, 6.1) give. A check that
 * reads the tree is a CALL step of the script: the dialog is up then. Hatari checks the pixels (run.py).
 *   make test
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "dialog.h"
#include "settings.h"
#include "dlgbuild.h"
#include "aes_stub.h"

OBJECT *stub_tree;
static int failed, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

static void checkf(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void checkf(int ok, const char *fmt, ...)
{
	char b[240];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	check(ok, b);
}

/* ---- the example dialog, written as an app writes it: three pages */
static short s_tick1, s_tick2, s_tick3, s_group1, s_group2, s_when, s_dis, s_long_sel;
static char s_num[4], s_name[8], s_long[100], s_pw[20];
static const char *const when[] = { "When first opened", "At start", "After a minute", "After 5 minutes" };
static const char *const longname[] = { "A very long choice that does not fit the button at all", "Short" };
static SETTINGS *cur;
static short o_pop, o_pop2, o_btn, o_val, o_num, o_name, o_long, o_box1, o_lbl1, o_pw, o_box2, o_boxdis, o_lbldis, o_g1, o_g2;
static short first[4], last[4];		/* each page's objects: heading, rule, its own */
static int acts, val_acts, hook_calls, check_calls;
static void *act_user;
static char valtext[16];
static short s_x;			/* the page's column, as the builder was given it */

static void act_hit(SETTINGS *s, void *user)
{
	(void)s;
	acts++;
	act_user = user;
}

static void val_hit(SETTINGS *s, void *user)
{
	(void)s, (void)user;
	val_acts++;
	memcpy(valtext, " changed...    ", 16);
}

static void page_a(SETTINGS *s, short x, short y, void *user)
{
	short k;
	(void)user;
	cur = s;
	stub_tree = set_tree(s);
	s_x = x;
	first[1] = (short)(set_db(s)->nobj - 2);
	set_text(s, x, y, "The accessory looks through the library:");
	o_pop = set_popup(s, x + 2, set_y(s, 1), 23, when, 4, &s_when);
	o_num = set_field(s, x + SET_FX, set_y(s, 2), s_num, 3, '9');
	o_name = set_field(s, x, set_y(s, 3), s_name, 7, 'X');
	k = set_tick(s, x, set_y(s, 4), "Keep read messages", &s_tick1);
	o_box1 = set_db(s)->tick[k].box;
	o_lbl1 = o_box1 + 1;
	o_long = set_long(s, x, set_y(s, 5), s_long, 20, 99, 0);
	last[1] = (short)(set_db(s)->nobj - 1);
}

/* the tabs and buttons, known once the dialog is built (they come before and after the pages) */
static void refs(void)
{
	short k;
	if (!cur || !stub_tree)
		return;
	for (k = 0; k < 3; k++)
		ref_tab[k + 1] = set_obj_tab(cur, k);
	ref_ok = set_obj_ok(cur);
	ref_cancel = set_obj_cancel(cur);
}

static void page_b(SETTINGS *s, short x, short y, void *user)
{
	short k, g1, g2, dis;
	(void)user;
	first[2] = (short)(set_db(s)->nobj - 2);
	k = set_tick(s, x, y, "Use colours", &s_tick2);
	o_box2 = set_db(s)->tick[k].box;
	dis = set_tick(s, x, set_y(s, 1), "Greyed", &s_dis);
	set_tick_enable(s, dis, 0);
	o_boxdis = set_db(s)->tick[dis].box;
	o_lbldis = o_boxdis + 1;
	g1 = set_tick(s, x, set_y(s, 2), "Never", &s_group1);
	g2 = set_tick(s, x, set_y(s, 3), "Always", &s_group2);
	set_tick_group(s, g1, 1);
	set_tick_group(s, g2, 1);
	o_g1 = set_db(s)->tick[g1].box;
	o_g2 = set_db(s)->tick[g2].box;
	o_btn = set_button(s, x, set_y(s, 4), 14, "Languages...", act_hit, (void *)0x1234);
	o_val = set_value(s, x + 11, set_y(s, 5), 16, valtext, val_hit, 0);
	o_pop2 = set_popup(s, x + 26, set_y(s, 6), 14, longname, 2, &s_long_sel);
	last[2] = (short)(set_db(s)->nobj - 1);
}

static void page_c(SETTINGS *s, short x, short y, void *user)
{
	short k;
	(void)user;
	first[3] = (short)(set_db(s)->nobj - 2);
	set_text(s, x, y, "Nothing to type here");
	k = set_tick(s, x, set_y(s, 1), "Secure", &s_tick3);
	(void)k;
	last[3] = (short)(set_db(s)->nobj - 1);
}

static void page_c_pw(SETTINGS *s, short x, short y, void *user)
{
	page_c(s, x, y, user);
	o_pw = set_long(s, x, set_y(s, 3), s_pw, 15, 18, '*');
	last[3] = (short)(set_db(s)->nobj - 1);
}

static const SET_PAGE pages3[] = { { "General", page_a }, { "Look", page_b }, { "Secure", page_c } };

static void reset(void)
{
	s_tick1 = 0, s_tick2 = 1, s_tick3 = 0, s_group1 = 1, s_group2 = 0, s_when = 1, s_dis = 0, s_long_sel = 0;
	strcpy(s_num, "30");
	strcpy(s_name, "Dana");
	strcpy(s_long, "C:\\GUIDES");
	strcpy(s_pw, "secret");
	memcpy(valtext, " Europe/Moscow ", 16);
	acts = val_acts = popups = alerts = hook_calls = check_calls = 0;
	pop_answer = -1;
	pop_kept = 1;
	updates = update_low = dial_start = dial_finish = draws = bells = timers = 0;
	ndraw_log = 0;
	fake_ms = 0;
	draw_cost = 0;
	win_made = win_open = 0;
	win_name[0] = 0;
	ap_count = 1;
	cell_w = 8, cell_h = 16;
	memset(ref_tab, 0, sizeof(ref_tab));
	ref_ok = ref_cancel = 0;
	stub_tree = 0;
	cur = 0;
	refs_hook = refs;
}

static SET_DEF def3(const SET_PAGE *p, short n)
{
	SET_DEF d;
	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = p;
	d.npages = n;
	d.height = 17;
	return d;
}

/* runs the dialog on a script: its answer, or -1 when the script ended with the dialog still open
   (a failure: the memory is left to the test's exit) */
static int run(const SET_DEF *d, const STEP *s)
{
	script = s;
	if (setjmp(script_out)) {
		check(0, "the script ended with the dialog still open");
		return -1;
	}
	return settings_dialog(d);
}

#define TREE (stub_tree)

/* the objects of page p (1 to 3) not hidden */
static int shown_of(short p)
{
	short i, n = 0;
	for (i = first[p]; i <= last[p]; i++)
		n += !(TREE[i].ob_flags & HIDETREE);
	return n;
}

/* ---------------- geometry ---------------- */

static struct {
	short cw, ch, sw, pw, h;
	const char *label;
} G;

static void geom(void)
{
	OBJECT *t = TREE;
	short cw = G.cw, ch = G.ch, sw = G.sw, pw = G.pw, H = G.h, SX = (short)(sw + 2), W = (short)(SX + pw);
	short gap = (short)(ch / 2), k, i;
	const char *label = G.label;
	const char *want[3] = { "  General", "  Look", "  Secure" };
	short heading[3] = { first[1], first[2], first[3] };

	checkf(t[0].ob_width == W * cw && t[0].ob_height == H * ch && t[0].ob_type == G_BOX && (t[0].ob_state & OUTLINED) &&
	       t[0].ob_spec == 0x00021100L,
	       "%s: the box %d x %d characters (%d x %d px), an OUTLINED G_BOX", label, W, H, t[0].ob_width, t[0].ob_height);
	checkf(t[1].ob_type == G_BOXTEXT && t[1].ob_x == 0 && t[1].ob_y == 0 && t[1].ob_width == W * cw && t[1].ob_height == ch &&
	       ((TEDINFO *)t[1].ob_spec)->te_color == 0x1071 && !strcmp(((TEDINFO *)t[1].ob_spec)->te_ptext, " Settings"),
	       "%s: the band \" Settings\" on row 0, the box's width, white on black (0x1071)", label);
	checkf(t[2].ob_type == G_BOX && t[2].ob_x == sw * cw && t[2].ob_y == ch && t[2].ob_width == 1 && t[2].ob_height == (H - 1) * ch &&
	       t[2].ob_spec == 0x00001171L,
	       "%s: the list's line at column %d (x %d), 1 pixel wide, from row 1 to the bottom (%d rows)", label, sw, t[2].ob_x, H - 1);
	for (k = 0; k < 3; k++) {
		OBJECT *o = &t[3 + k];
		TEDINFO *te = (TEDINFO *)o->ob_spec;
		short wy = (short)((2 + 2 * k) * ch + k * gap);
		checkf(o->ob_type == G_BOXTEXT && (o->ob_flags & TOUCHEXIT) && o->ob_x == 2 && o->ob_y == wy &&
		       o->ob_width == (sw - 1) * cw + 6 && o->ob_height == ch && !strcmp(te->te_ptext, want[k]) && te->te_thickness == 0 &&
		       o->ob_state == 0,
		       "%s: page %d '%s': y %d (row %d + %d px), %d wide, TOUCHEXIT, no frame (got y %d w %d)", label, k + 1, want[k] + 2, wy,
		       2 + 2 * k, k * gap, (sw - 1) * cw + 6, o->ob_y, o->ob_width);
	}
	checkf(((TEDINFO *)t[3].ob_spec)->te_color == 0x1071 && ((TEDINFO *)t[4].ob_spec)->te_color == 0x1180 &&
	       ((TEDINFO *)t[5].ob_spec)->te_color == 0x1180,
	       "%s: the chosen page is the black bar (0x1071), the others plain (0x1180)", label);
	checkf(t[6].ob_type == G_IBOX && t[6].ob_x == (SX - 1) * cw && t[6].ob_y == 2 * ch && t[6].ob_width == pw * cw &&
	       t[6].ob_height == (H - 4) * ch,
	       "%s: the pages' box: column %d, row 2, %d wide, %d rows (down to the buttons' row)", label, SX - 1, pw, H - 4);
	for (k = 0; k < 3; k++) {
		OBJECT *h = &t[heading[k]], *r = &t[heading[k] + 1];
		checkf(h->ob_type == G_STRING && h->ob_x == SX * cw && h->ob_y == 2 * ch &&
		       !strcmp((const char *)h->ob_spec, want[k] + 2) && r->ob_type == G_BOX && r->ob_x == SX * cw &&
		       r->ob_y == 3 * ch + 2 && r->ob_width == (pw - 2) * cw && r->ob_height == 1 && r->ob_spec == 0x00001171L,
		       "%s: page %d: its name on row 2 at column %d; a 1-pixel line across (%d wide) on row 3, 2 pixels down", label, k + 1, SX,
		       pw - 2);
	}
	i = (short)(first[1] + 2);
	checkf(t[i].ob_type == G_STRING && t[i].ob_x == SX * cw && t[i].ob_y == 4 * ch,
	       "%s: the first setting on row 4 (y %d), column %d (x %d)", label, 4 * ch, SX, t[i].ob_x);
	checkf(t[o_num].ob_x == (SX + 30) * cw && t[o_num].ob_type == G_FBOXTEXT && t[o_num].ob_y == 6 * ch + 2 + 2 * (ch * 3 / 8) &&
	       ((TEDINFO *)t[o_num].ob_spec)->te_thickness == -1,
	       "%s: a field in the values' column SX + 30 = %d, in a frame (G_FBOXTEXT, 1 line); row 6 + %d px", label, SX + 30,
	       2 + 2 * (ch * 3 / 8));
	checkf(t[o_pop].ob_x == (SX + 2) * cw && t[o_pop].ob_width == 23 * cw && t[o_pop].ob_y == 5 * ch + 2 + (ch * 3 / 8),
	       "%s: the pop-up button 2 characters in, 23 wide, row 5", label);
	{
		OBJECT *c = &t[ref_cancel], *ok = &t[ref_ok];
		checkf(c->ob_type == G_BUTTON && !strcmp((const char *)c->ob_spec, "Cancel") && (c->ob_flags & EXIT) &&
		       !(c->ob_flags & DEFAULT) && c->ob_x == (W - 24) * cw && c->ob_y == (H - 2) * ch && c->ob_width == 10 * cw &&
		       c->ob_height == ch,
		       "%s: Cancel at column %d (W - 24), row %d (H - 2), 10 wide, EXIT (x %d y %d)", label, W - 24, H - 2, c->ob_x, c->ob_y);
		checkf(ok->ob_type == G_BUTTON && !strcmp((const char *)ok->ob_spec, "OK") && (ok->ob_flags & EXIT) &&
		       (ok->ob_flags & DEFAULT) && ok->ob_x == (W - 12) * cw && ok->ob_y == (H - 2) * ch && ok->ob_width == 10 * cw &&
		       ok->ob_height == ch,
		       "%s: OK at column %d (W - 12), the same row, 10 wide, EXIT | DEFAULT", label, W - 12);
		check(ref_ok == ref_cancel + 1 && (t[ref_ok].ob_flags & LASTOB) && !(t[ref_cancel].ob_flags & LASTOB),
		      "Cancel then OK: OK on the right and the last object");
	}
	/* the page is on screen once: only page 1's objects shown, a box the size of the dialog */
	checkf(shown_of(1) == last[1] - first[1] + 1 && shown_of(2) == 0 && shown_of(3) == 0, "%s: page 1 shown, 2 and 3 hidden", label);
	/* the links: every object is in the root's list, in the order added, the last LASTOB */
	{
		short n = 0, j;
		for (j = t[0].ob_head; ; j = t[j].ob_next) {
			n++;
			if (t[j].ob_next == 0 || n > 400)
				break;
		}
		checkf(t[j].ob_flags & LASTOB && t[t[0].ob_tail].ob_flags & LASTOB && n == ref_ok,
		       "%s: %d objects in the root's list, in order, the last one LASTOB and the root's tail", label, n);
	}
}

static void geometry(short cw, short ch, short sw, short pw, short h, const char *label)
{
	static const STEP s[] = { CALL(geom), KEY(KEY_UNDO), END };
	SET_DEF d = def3(pages3, 3);
	reset();
	cell_w = cw, cell_h = ch;
	G.cw = cw, G.ch = ch, G.sw = sw ? sw : SET_SW, G.pw = pw ? pw : SET_PW, G.h = h, G.label = label;
	d.list_w = sw, d.page_w = pw, d.height = h;
	run(&d, s);
}

/* ---------------- the cases ---------------- */

static void find_check(void)
{
	OBJECT *t = TREE;
	short x0 = t[0].ob_x, y0 = t[0].ob_y;
	short tab2 = ref_tab[2];
	checkf(dlg_find(t, x0 + t[tab2].ob_x + 5, y0 + t[tab2].ob_y + 5) == tab2, "dlg_find: a click on page 2's name finds it");
	checkf(dlg_find(t, x0 + t[o_num].ob_x + 4, y0 + t[o_num].ob_y + 4) == o_num, "dlg_find: a click on a field of the shown page finds it");
	checkf(dlg_find(t, x0 + t[ref_ok].ob_x + 3, y0 + t[ref_ok].ob_y + 3) == ref_ok, "dlg_find: OK");
	checkf(dlg_find(t, x0 + t[ref_cancel].ob_x + 3, y0 + t[ref_cancel].ob_y + 3) == ref_cancel, "dlg_find: Cancel (the one before OK)");
	checkf(dlg_find(t, x0 + t[o_box1].ob_x + 3, y0 + t[o_box1].ob_y + 3) == o_box1, "dlg_find: a tick's box");
	checkf(dlg_find(t, x0 + t[o_box1].ob_x + 3, y0 + t[o_box1].ob_y + 3) == objc_find(t, 0, 8, x0 + t[o_box1].ob_x + 3, y0 + t[o_box1].ob_y + 3),
	       "dlg_find answers what the stand-in AES's objc_find does");
	checkf(dlg_find(t, x0 + 2, y0 + t[0].ob_height - 3) == 0, "dlg_find: the box where nothing else is: the root (0)");
	checkf(dlg_find(t, x0 - 1, y0 + 5) == -1 && dlg_find(t, x0 + t[0].ob_width, y0 + 5) == -1 && dlg_find(t, x0 + 5, y0 - 1) == -1,
	       "dlg_find: outside the box: -1");
	/* the hidden page's button is not found: page 2's button is at a place page 1 has a field */
	checkf(dlg_find(t, x0 + t[o_btn].ob_x + 5, y0 + t[o_btn].ob_y + 5) != o_btn, "dlg_find: an object of a page not shown is passed over");
	t[o_num].ob_flags |= HIDETREE;
	checkf(dlg_find(t, x0 + t[o_num].ob_x + 4, y0 + t[o_num].ob_y + 4) != o_num, "dlg_find: HIDETREE objects are skipped");
	t[o_num].ob_flags &= ~HIDETREE;
}

static void pg_after_tab2(void)
{
	short i;
	checkf(set_page_now(cur) == 1, "page 2 is the page now");
	checkf(shown_of(1) == 0 && shown_of(2) == last[2] - first[2] + 1 && shown_of(3) == 0,
	       "page 2 chosen: its objects shown, page 1's and page 3's hidden");
	for (i = 1; i <= 3; i++) {
		short j, ed = 0;
		for (j = first[i]; j <= last[i]; j++)
			ed += (TREE[j].ob_flags & EDITABLE) != 0;
		if (i == 1)
			checkf(ed == 0, "page 1's fields are not EDITABLE while it is hidden (Tab skips them)");
	}
	check(((TEDINFO *)TREE[ref_tab[2]].ob_spec)->te_color == 0x1071 && ((TEDINFO *)TREE[ref_tab[1]].ob_spec)->te_color == 0x1180 &&
	      TREE[ref_tab[1]].ob_state == 0 && TREE[ref_tab[2]].ob_state == 0 && TREE[ref_tab[3]].ob_state == 0,
	      "the bar moved to page 2 by its colour word; no tab has SELECTED (5.6)");
	check(objc_find(TREE, 0, 8, TREE[0].ob_x + TREE[o_num].ob_x + 4, TREE[0].ob_y + TREE[o_num].ob_y + 4) != o_num,
	      "a hidden field can't be clicked: the hit test passes over it");
}

static void pg_back_to_1(void)
{
	check(set_page_now(cur) == 0 && shown_of(1) == last[1] - first[1] + 1 && shown_of(2) == 0 && (TREE[o_num].ob_flags & EDITABLE) &&
	      !(TREE[o_btn].ob_flags & EDITABLE),
	      "page 3, then page 1 again: its objects back and editable, the others hidden");
}

static void pg_ticks1(void)
{
	checkf(s_tick1 == 1, "a tick: its box, its words, its words again (3 clicks): on (%d)", s_tick1);
}

static void pg_ticks2(void)
{
	checkf(s_tick2 == 0, "a tick on page 2 turned off by a click on its box");
	checkf(s_dis == 0, "a greyed tick (DISABLED) isn't turned over by a click on its words");
	checkf(s_group2 == 1 && s_group1 == 0, "a group: a click on 'Always' chose it and cleared 'Never' (%d, %d)", s_group1, s_group2);
}

static void pg_ticks3(void)
{
	checkf(s_group2 == 1 && s_group1 == 0 && (TREE[o_g2].ob_spec == (long)&db_tick_on), "a chosen tick of a group stays on when clicked again; the box shows it");
	checkf(TREE[o_box2].ob_spec == (long)&db_tick_off && TREE[o_g1].ob_spec == (long)&db_tick_off,
	       "the boxes show the variables (the tick drawing is the family's: on and off bitmaps)");
}

static void test_pages_and_ticks(void)
{
	SET_DEF d = def3(pages3, 3);
	int r;
	reset();
	{
		static const STEP s[] = { TAB(2), CALL(pg_after_tab2), CANCELB, END };
		r = run(&d, s);
	}
	reset();
	{
		static const STEP s[] = { TAB(3), TAB(1), CALL(pg_back_to_1), CANCELB, END };
		r = run(&d, s);
	}
	reset();
	{
		static const STEP s[] = { OBJ(o_box1), OBJ(o_lbl1), OBJ(o_lbl1), CALL(pg_ticks1), OKB, END };
		r = run(&d, s);
		checkf(r == 1 && s_tick1 == 1, "OK keeps the tick (%d)", s_tick1);
	}
	reset();
	{
		static const STEP s[] = { TAB(2), OBJ(o_box2), OBJ(o_lbldis), OBJ(o_g2), CALL(pg_ticks2), OBJ(o_g2), CALL(pg_ticks3), CANCELB, END };
		r = run(&d, s);
		checkf(s_tick2 == 1 && s_group1 == 1 && s_group2 == 0, "Cancel puts the ticks back (%d %d %d)", s_tick2, s_group1, s_group2);
	}
}


/* ---------------- the page switch draws only what changed (9 Nov 2026) ---------------- */

static int log_at;
static void sw_mark(void)
{
	log_at = ndraw_log;
	timers = 0;
}

static int in_log(short obj, int from)
{
	int i, n = 0;
	for (i = from; i < ndraw_log; i++)
		n += draw_log[i] == obj;
	return n;
}

static void sw_check(void)
{
	int i, deep = 0, ground = 0, once = 1, old_drawn = 0;
	short k, missed = -1;
	for (i = log_at; i < ndraw_log; i++) {
		deep += draw_depth[i] != 0;		/* a walk through the tree is depth 8 */
		ground += draw_log[i] == 0;
	}
	checkf(deep == 0 && ndraw_log > log_at, "a page switch: %d objc_draw calls, every one of a single object (depth 0), none a walk",
	       ndraw_log - log_at);
	checkf(ground == 1, "the root is drawn once, alone, as the page's ground (%d times)", ground);
	checkf(in_log(ref_tab[1], log_at) == 1 && in_log(ref_tab[2], log_at) == 1 && in_log(ref_tab[3], log_at) == 0,
	       "the two names that changed are drawn, once each, the third is not");
	for (k = first[2]; k <= last[2]; k++)
		if (!(TREE[k].ob_flags & HIDETREE) && in_log(k, log_at) != 1) {
			once = 0;
			missed = k;
		}
	checkf(once, "every object of the new page is drawn once (object %d is not)", missed);
	for (k = first[1]; k <= last[1]; k++)
		old_drawn += in_log(k, log_at);
	checkf(old_drawn == 0, "none of the old page's objects (hidden) is drawn (%d)", old_drawn);
	checkf(in_log(ref_cancel, log_at) == 1 && in_log(ref_ok, log_at) == 1,
	       "Cancel and OK are drawn again, once: the area's 3 pixels reach their frames");
	checkf(in_log(set_obj_pages(cur), log_at) == 0 && in_log(1, log_at) == 0, "neither the band nor the pages' own box is drawn");
}

static void sw_banded(void)
{
	checkf(timers >= 4, "at 15 ms an object and turns of 20 ms the switch made %d event calls between its parts", timers);
	sw_check();
}

static void test_page_switch(void)
{
	SET_DEF d = def3(pages3, 3);
	reset();
	{
		static const STEP s[] = { CALL(sw_mark), TAB(2), CALL(sw_check), CANCELB, END };
		run(&d, s);
	}
	/* every object takes 15 ms: the page is drawn in turns, an event call between, each object once */
	reset();
	draw_cost = 15;
	{
		static const STEP s[] = { CALL(sw_mark), TAB(2), CALL(sw_banded), CANCELB, END };
		run(&d, s);
	}
	draw_cost = 0;
	/* dlg_find: the AES's hit test in C. A tab, a field, a hidden object's place, the box's gap, outside */
	reset();
	{
		static const STEP s[] = { CALL(find_check), CANCELB, END };
		run(&d, s);
	}
}


/* ---------------- radio buttons and the keyboard's reach (D24, D25; 9 Nov 2026) ---------------- */

static short r_sel, r_sel2, r_tick, r_tick2, r_pop, r_acts, r_big_n;
static char r_text[6];
static short o_rad, o_rad2, o_rtick, o_rpop, o_rfield, o_rbtn, o_rtick2, o_r0, o_r1, o_r2, o_r3, o_big;
static const char *const rlab[] = { "Auto", "FDI", "DSP", "Both" };
static const char *const rlab2[] = { "On", "Off" };
static const char *const rpop[] = { "One", "Two", "Three" };
static const char *const rbig[] = { "a", "b", "c", "d", "e", "f" };
static short r_big_sel;

/* choice 2 (DSP) applies while the tick is off: a choice that follows another setting */
static int rwhen(SETTINGS *s, short obj, short c, void *user)
{
	(void)s, (void)obj, (void)user;
	return c == 2 ? !r_tick : 1;
}

static void r_act(SETTINGS *s, void *user)
{
	(void)s, (void)user;
	r_acts++;
}

static void page_r1(SETTINGS *s, short x, short y, void *user)
{
	short k;
	(void)user;
	cur = s;
	stub_tree = set_tree(s);
	o_rfield = set_field(s, x, y, r_text, 5, 'X');
	k = set_tick(s, x, set_y(s, 1), "Use the DSP", &r_tick);
	o_rtick = set_db(s)->tick[k].box;
	o_rad = set_radio(s, x, set_y(s, 2), 9, rlab, 4, &r_sel);
	set_choice_when(s, o_rad, rwhen, 0);
	o_rad2 = set_radio(s, x, set_y(s, 3), 8, rlab2, 2, &r_sel2);
	k = set_tick(s, x, set_y(s, 4), "Another", &r_tick2);
	o_rtick2 = set_db(s)->tick[k].box;
	o_rpop = set_popup(s, x + 2, set_y(s, 5), 23, rpop, 3, &r_pop);
	set_popup_enable(s, o_rpop, 1, 0);
	o_rbtn = set_button(s, x, set_y(s, 6), 14, "Languages...", r_act, 0);
}

static void page_r2(SETTINGS *s, short x, short y, void *user)
{
	short before = set_db(s)->nobj;
	(void)user;
	set_tick(s, x, y, "Alone on a page", &s_tick3);
	before = set_db(s)->nobj;
	o_big = set_radio(s, x, set_y(s, 2), 9, rbig, 6, &r_big_sel);
	r_big_n = (short)(set_db(s)->nobj - before);
}

static const SET_PAGE pages_r[] = { { "Radios", page_r1 }, { "Plain", page_r2 } };

static void refs_r(void)
{
	if (!cur || !stub_tree)
		return;
	ref_tab[1] = set_obj_tab(cur, 0);
	ref_tab[2] = set_obj_tab(cur, 1);
	ref_ok = set_obj_ok(cur);
	ref_cancel = set_obj_cancel(cur);
	o_r0 = o_rad, o_r1 = (short)(o_rad + 1), o_r2 = (short)(o_rad + 2), o_r3 = (short)(o_rad + 3);
}

static void r_reset(void)
{
	reset();
	refs_hook = refs_r;
	r_sel = 0, r_sel2 = 1, r_tick = 0, r_tick2 = 0, r_pop = 0, r_acts = 0, r_big_sel = 0;
	strcpy(r_text, "ab");
	ring_draws = 0;
	ndraw_log = 0;
	s_tick3 = 0;
}

static int st(short o)
{
	return TREE[o].ob_state;
}

/* the ring round object o as settings.c draws it: 2 pixels outside */
static int ring_round(short o)
{
	short x, y;
	objc_offset(TREE, o, &x, &y);
	return ring_draws > 0 && ring_last.x == x - 2 && ring_last.y == y - 2 && ring_last.w == TREE[o].ob_width + 4 &&
	       ring_last.h == TREE[o].ob_height + 4;
}

static void r_geom(void)
{
	OBJECT *t = TREE;
	short k;
	int ok = 1;
	for (k = 0; k < 4; k++) {
		OBJECT *o = &t[o_rad + k];
		ok &= o->ob_type == G_BUTTON && (o->ob_flags & TOUCHEXIT) && (o->ob_flags & RBUTTON) && !(o->ob_flags & SELECTABLE) &&
		      !(o->ob_flags & EXIT) && o->ob_width == 9 * cell_w && o->ob_height == cell_h &&
		      o->ob_x == t[o_rad].ob_x + k * (10 * cell_w + 4) && o->ob_y == t[o_rad].ob_y;
	}
	check(ok, "a radio group: four G_BUTTONs, RBUTTON | TOUCHEXIT (not SELECTABLE: the AES would unchoose the other groups'), 9 wide, 10 characters and 4 pixels apart");
	checkf(t[o_rad2].ob_width == 8 * cell_w && o_rad2 == o_rad + 4, "the second group follows the first, 8 wide (%d, %d)", o_rad, o_rad2);
	checkf((st(o_rad) & SELECTED) && !(st(o_rad + 1) & SELECTED) && !(st(o_rad2) & SELECTED) && (st(o_rad2 + 1) & SELECTED),
	       "the chosen button of each group is SELECTED (the variables are 0 and 1)");
	checkf(!(st(o_rad + 2) & DISABLED) && !(st(o_rad + 1) & DISABLED),
	       "the app's function answered as the dialog opened: DSP applies while the tick is off");
	check(!strcmp((const char *)t[o_rad + 3].ob_spec, "Both"), "the labels are the app's text, not copied");
	checkf(ring_draws == 0, "a page with a text field opens with the cursor in it and no ring (%d)", ring_draws);
}

static void r_greyed(void)
{
	checkf(r_tick == 1 && (st(o_rad + 2) & DISABLED) && !(st(o_rad + 1) & DISABLED),
	       "the tick turned on: the app's function greys DSP (DISABLED) and no other (tick %d)", r_tick);
}

static void r_click_greyed(void)
{
	checkf(r_sel == 0 && !(st(o_rad + 2) & SELECTED), "a click on the greyed DSP chooses nothing (%d)", r_sel);
}

static void r_click_fdi(void)
{
	checkf(r_sel == 1 && (st(o_rad + 1) & SELECTED) && !(st(o_rad) & SELECTED),
	       "a click on FDI: the variable is 1, FDI turned over, Auto not (%d)", r_sel);
	checkf(r_sel2 == 1 && (st(o_rad2 + 1) & SELECTED), "the other group is as it was: not unchosen by the click (%d)", r_sel2);
}

static void r_drawn_two(void)
{
	checkf(in_log(o_rad, 0) == 1 && in_log(o_rad + 1, 0) == 1 && in_log(o_rad + 2, 0) == 0 && in_log(o_rad2, 0) == 0 &&
	       in_log(o_rad2 + 1, 0) == 0,
	       "a click on FDI draws the two buttons that changed, once each, and no other");
}

static void r_mark(void)
{
	ndraw_log = 0;
}

static void r_pop_check(void)
{
	checkf(popups == 1 && pop_n == 3 && pop_lab[1][0] == '~' && pop_lab[0][0] != '~' && pop_lab[2][0] != '~' &&
	       strstr(pop_lab[1], "Two") && r_pop == 2,
	       "a pop-up item greyed by the app: its label starts '~' (popup.h), the others don't; the choice taken is %d", r_pop);
}

static void test_radios(void)
{
	SET_DEF d = def3(pages_r, 2);
	int r;
	r_reset();
	{
		static const STEP s[] = { CALL(r_geom), CANCELB, END };
		run(&d, s);
	}
	r_reset();
	{
		static const STEP s[] = { OBJ(o_rtick), CALL(r_greyed), OBJ(o_r2), CALL(r_click_greyed), CALL(r_mark), OBJ(o_r1),
					  CALL(r_click_fdi), CALL(r_drawn_two), OBJ(o_r1), CANCELB, END };
		run(&d, s);
		checkf(r_sel == 0 && r_tick == 0 && r_sel2 == 1, "Cancel puts the group and the tick back (%d, %d)", r_sel, r_tick);
	}
	r_reset();
	{
		static const STEP s[] = { OBJ(o_r3), OBJ(o_r3), OKB, END };
		r = run(&d, s);
		checkf(r == 1 && r_sel == 3, "a click on Both and OK: the group keeps it (%d, %d)", r, r_sel);
	}
	r_reset();
	pop_answer = 2;
	{
		static const STEP s[] = { OBJ(o_rpop), CALL(r_pop_check), CANCELB, END };
		run(&d, s);
	}
	r_reset();
	d.first = 1;
	{
		static const STEP s[] = { CANCELB, END };
		run(&d, s);
		checkf(r_big_n == 4, "set_radio with six choices makes four buttons (%d)", r_big_n);
	}
}

static void k_tick(void)
{
	check(ring_round(o_rtick) && st(o_rpop) == 0, "Tab from the first field: the ring is round the first tick's box");
}

static void k_space1(void)
{
	checkf(r_tick == 0 && !(st(o_rad + 2) & DISABLED), "Space ticked the tick: off (%d), DSP applies again", r_tick);
}

static void k_radio_on(void)
{
	checkf(r_tick == 1 && (st(o_rad + 2) & DISABLED), "Space again: on (%d), DSP greyed", r_tick);
}

static void k_group(void)
{
	check(ring_round(o_rad), "Tab to the radio group: the ring is round its chosen button (Auto)");
}

static void k_right_end(void)
{
	checkf(r_sel == 3 && ring_round(o_rad + 3), "Right, Right, Right: 1, then 3 (the greyed DSP skipped), then the end holds (%d)", r_sel);
}

static void k_left_up(void)
{
	checkf(r_sel == 0 && (st(o_rad) & SELECTED) && !(st(o_rad + 1) & SELECTED) && !(st(o_rad + 3) & SELECTED) && ring_round(o_rad),
	       "Left (1, skipping DSP), Up (0), Up: Auto, the start holds (%d), the ring went with it", r_sel);
}

static void k_down(void)
{
	checkf(r_sel == 1 && ring_round(o_rad + 1), "Down: FDI (%d), the ring on it", r_sel);
}

static void k_radio2(void)
{
	checkf(r_sel2 == 0 && ring_round(o_rad2), "Tab, Up: the second group chose its first button (%d)", r_sel2);
}

static void k_tick2(void)
{
	check(r_tick2 == 1 && ring_round(o_rtick2), "Tab, Space: the second tick is on, the ring on its box");
}

static void k_popup(void)
{
	checkf(popups == 1 && r_pop == 2 && ring_round(o_rpop), "Tab, Space: the pop-up opened, the choice taken (%d)", r_pop);
}

static void k_button(void)
{
	checkf(r_acts == 1 && ring_round(o_rbtn), "Tab, Space: the button did its work (%d)", r_acts);
}

static void k_list(void)
{
	check(ring_round(ref_tab[1]) && set_page_now(cur) == 0, "Tab from the last control: the list, the ring round the chosen name");
}

static void k_page2(void)
{
	checkf(set_page_now(cur) == 1 && ring_round(ref_tab[2]) && ((TEDINFO *)TREE[ref_tab[2]].ob_spec)->te_color == 0x1071,
	       "Down in the list: page 2 shown (the black bar moved), the ring with it (page %d)", set_page_now(cur) + 1);
}

static void k_page2_hold(void)
{
	check(set_page_now(cur) == 1, "Down at the last page holds (no wrapping)");
}

static void k_page1(void)
{
	checkf(set_page_now(cur) == 0 && ring_round(ref_tab[1]), "Up: page 1 again, the ring on its name (page %d)", set_page_now(cur) + 1);
}

static void k_shift_tab(void)
{
	check(ring_round(o_rbtn), "Shift+Tab from the list: the last control (the button)");
}

static void k_wrap(void)
{
	check(!strcmp(r_text, "abx "), "Tab, Tab from the button: list, then round to the first field; 'x' and Space typed into it");
}

static void test_keys(void)
{
	SET_DEF d = def3(pages_r, 2);
	int r;
	r_reset();
	r_tick = 1;
	pop_answer = 2;
	{
		static const STEP s[] = {
			KEY(KEY_TAB), CALL(k_tick), KEY(KEY_SPACE), CALL(k_space1), KEY(KEY_SPACE), CALL(k_radio_on),
			KEY(KEY_TAB), CALL(k_group), KEY(KEY_RIGHT), KEY(KEY_RIGHT), KEY(KEY_RIGHT), CALL(k_right_end),
			KEY(KEY_LEFT), KEY(KEY_UP), KEY(KEY_UP), CALL(k_left_up), KEY(KEY_DOWN), CALL(k_down),
			KEY(KEY_TAB), KEY(KEY_UP), CALL(k_radio2),
			KEY(KEY_TAB), KEY(KEY_SPACE), CALL(k_tick2),
			KEY(KEY_TAB), KEY(KEY_SPACE), CALL(k_popup),
			KEY(KEY_TAB), KEY(KEY_SPACE), CALL(k_button),
			KEY(KEY_TAB), CALL(k_list), KEY(KEY_DOWN), CALL(k_page2), KEY(KEY_DOWN), CALL(k_page2_hold),
			KEY(KEY_UP), CALL(k_page1),
			KEYS(KEY_TAB, 1), CALL(k_shift_tab),
			KEY(KEY_TAB), KEY(KEY_TAB), KEY(K('x')), KEY(KEY_SPACE), CALL(k_wrap),
			KEY(KEY_RETURN), END
		};
		r = run(&d, s);
		checkf(r == 1 && r_sel == 1 && r_sel2 == 0 && r_tick == 1 && r_tick2 == 1 && r_pop == 2 && !strcmp(r_text, "abx "),
		       "Return is OK and keeps what the keys did (%d: %d %d %d %d %d '%s')", r, r_sel, r_sel2, r_tick, r_tick2, r_pop, r_text);
	}
}

static void k_start_list(void)
{
	checkf(ring_draws == 0 && set_page_now(cur) == 1,
	       "a page without a text field opens with the list in focus, and no ring: nothing on the screen differs from the old dialog (%d)", ring_draws);
}

static void k_esc_tick(void)
{
	check(ring_round(o_rtick), "Tab: the first tick");
}

static void test_keys2(void)
{
	SET_DEF d = def3(pages_r, 2);
	int r;
	r_reset();
	d.first = 1;
	{
		static const STEP s[] = { CALL(k_start_list), KEY(KEY_UP), CALL(k_page1), KEY(KEY_ESC), END };
		r = run(&d, s);
		checkf(r == 0, "Esc with the focus on the list cancels (%d)", r);
	}
	d.first = 0;
	r_reset();
	{
		static const STEP s[] = { KEY(KEY_TAB), CALL(k_esc_tick), KEY(KEY_SPACE), KEY(KEY_ESC), END };
		r = run(&d, s);
		checkf(r == 0 && r_tick == 0, "Esc with the focus on a tick cancels, and the tick Space turned is back (%d, %d)", r, r_tick);
	}
	r_reset();
	{
		static const STEP s[] = { KEY(KEY_TAB), KEY(KEY_TAB), KEY(KEY_SPACE), KEY(KEY_RETURN), END };
		r = run(&d, s);
		checkf(r == 1 && r_sel == 0, "Space on a radio group does nothing; Return is OK (%d, %d)", r, r_sel);
	}
	r_reset();
	{
		/* the mouse after the keys: the ring is gone, the first field has the cursor again */
		static const STEP s[] = { KEY(KEY_TAB), OBJ(o_r1), KEY(K('q')), OKB, END };
		r = run(&d, s);
		checkf(r == 1 && r_sel == 1 && !strcmp(r_text, "abq"), "a click after Tab: the cursor is back in the first field ('%s')", r_text);
	}
}

static void test_keys_and_values(void)
{
	SET_DEF d = def3(pages3, 3);
	int r;
	/* OK: the typed values stay; the cursor starts in the first field of the page; Tab goes on */
	reset();
	{
		static const STEP s[] = { KEY(KEY_ESC), KEY(K('7')), KEY(K('5')), KEY(0x0f09), KEY(K('A')), KEY(K('b')), OKB, END };
		r = run(&d, s);
		checkf(r == 1 && !strcmp(s_num, "75") && !strcmp(s_name, "DanaAb"),
		       "OK: typed values kept (number '%s' after Esc emptied it, name '%s' after Tab)", s_num, s_name);
	}
	reset();
	{
		static const STEP s[] = { KEY(K('1')), KEY(KEY_RETURN), END };
		r = run(&d, s);
		checkf(r == 1 && !strcmp(s_num, "301"), "Return in a field is OK, the text kept ('%s')", s_num);
	}
	reset();
	{
		static const STEP s[] = { KEY(KEY_RETURN), END };
		r = run(&d, s);
		checkf(r == 1, "Return with nothing typed: OK");
	}
	/* Cancel puts every value back */
	reset();
	{
		static const STEP s[] = { KEY(K('9')), OBJ(o_box1), OBJ(o_pop), OBJ(o_long), KEY(K('x')), CANCELB, END };
		pop_answer = 3;
		r = run(&d, s);
		checkf(r == 0 && !strcmp(s_num, "30") && s_tick1 == 0 && s_when == 1 && !strcmp(s_long, "C:\\GUIDES"),
		       "Cancel: 0, and number '%s', tick %d, pop-up %d, long field '%s' are as they were", s_num, s_tick1, s_when, s_long);
		checkf(popups == 1, "(the pop-up had been opened and its choice taken before Cancel)");
	}
	/* Undo, and Esc with no field under the cursor: Cancel; Esc in a field: empties it, the dialog stays */
	reset();
	{
		static const STEP s[] = { KEY(K('9')), KEY(KEY_UNDO), END };
		r = run(&d, s);
		checkf(r == 0 && !strcmp(s_num, "30"), "Undo: Cancel; the typed '9' is thrown away ('%s')", s_num);
	}
	reset();
	{
		static const STEP s[] = { KEY(KEY_ESC), KEY(K('4')), KEY(KEY_ESC), KEY(K('5')), OKB, END };
		r = run(&d, s);
		checkf(r == 1 && !strcmp(s_num, "5") && !strcmp(s_name, "Dana"),
		       "Esc in a field empties that field only (number '%s', name still '%s') and the dialog stays open", s_num, s_name);
	}
	reset();
	{
		static const STEP s[] = { TAB(3), KEY(KEY_ESC), END };
		r = run(&d, s);
		checkf(r == 0, "Esc on a page with no text field: Cancel (%d)", r);
	}
	reset();
	{
		static const STEP s[] = { KEY(KEY_ESC), KEY(KEY_UNDO), END };
		r = run(&d, s);
		checkf(r == 0 && !strcmp(s_num, "30"), "Esc (emptied the field), then Undo: Cancel, the number as it was ('%s')", s_num);
	}
	/* a long field holds more than it shows */
	reset();
	{
		static STEP s[48];
		short n = 0, i;
		const char *typed = "\\MORE\\GUIDES\\AND\\MORE";
		s[n++] = (STEP)OBJ(o_long);
		for (i = 0; typed[i]; i++)
			s[n++] = (STEP)KEY(K(typed[i]));
		s[n++] = (STEP)OKB;
		s[n] = (STEP)END;
		r = run(&d, s);
		checkf(r == 1 && !strcmp(s_long, "C:\\GUIDES\\MORE\\GUIDES\\AND\\MORE") && strlen(s_long) > 20,
		       "a long field showing 20 holds the %d characters typed: '%s'", (int)strlen(s_long), s_long);
		s[n - 1] = (STEP)CANCELB;
		strcpy(s_long, "C:\\GUIDES");
		r = run(&d, s);
		checkf(r == 0 && !strcmp(s_long, "C:\\GUIDES"), "... Cancel puts the long field's text back ('%s')", s_long);
	}
}

static int check_hook(SETTINGS *s, void *user)
{
	check_calls++;
	if (check_calls == 1) {
		set_show(s, *(short *)user);
		return 0;
	}
	return 1;
}

static void pg_after_check(void)
{
	checkf(check_calls == 1 && set_page_now(cur) == 2, "check: OK refused the first time, the dialog is open on page 3 (the app moved it)");
}

static void test_check(void)
{
	SET_DEF d = def3(pages3, 3);
	short to = 2;
	int r;
	static const STEP s[] = { OKB, CALL(pg_after_check), OKB, END };
	reset();
	d.check = check_hook;
	d.user = &to;
	r = run(&d, s);
	checkf(r == 1 && check_calls == 2, "check: OK refused once and accepted once: the dialog gives 1 (%d calls)", check_calls);
}

static void pg_pop(void)
{
	char want[40];
	short bx, by;
	OBJECT *t = TREE;
	checkf(popups == 1 && pop_n == 4, "a click on the pop-up button opens the pop-up with its 4 choices");
	checkf(s_when == 2, "the choice taken is the app's variable at once (%d)", s_when);
	checkf(pop_new && !strcmp(pop_lab[1], "\x08" "At start") && !strcmp(pop_lab[0], "When first opened") && !strcmp(pop_lab[2], "After a minute"),
	       "popup_m's labels: the chosen one '\\x08At start' (the check mark), the others plain (popup.h)");
	objc_offset(t, o_pop, &bx, &by);
	checkf(pop_x == bx && pop_y == by - 3 - 1 * (16 + 2),
	       "opened over the button, its chosen row (1) on it: x %d, y %d (button at %d, %d)", pop_x, pop_y, bx, by);
	memset(want, ' ', 23);
	want[23] = 0;
	memcpy(want + 1, "After a minute", 14);
	want[22] = 2;
	checkf(!strcmp(((TEDINFO *)t[o_pop].ob_spec)->te_ptext, want) && strlen(want) == 23,
	       "the button then shows ' %%-21s\\x02': 23 characters, the down arrow last");
	checkf(t[o_pop].ob_type == G_BOXTEXT && (t[o_pop].ob_flags & TOUCHEXIT) && ((TEDINFO *)t[o_pop].ob_spec)->te_thickness == -1 &&
	       t[o_pop].ob_width == 23 * 8 && !(t[o_pop].ob_state & SELECTED),
	       "it is a G_BOXTEXT, TOUCHEXIT, with an outline (te_thickness -1), 23 wide, not SELECTED");
}

static void pg_pop_none(void)
{
	checkf(popups == 1 && s_when == 1, "a pop-up closed with nothing chosen leaves the choice (%d)", s_when);
}

static void pg_pop_long(void)
{
	const char *t = ((TEDINFO *)TREE[o_pop2].ob_spec)->te_ptext;
	checkf(strlen(t) == 14 && t[13] == 2 && t[12] == ' ' && !strncmp(t, " A very long", 12),
	       "a choice too long for its button (14 wide) is cut, a space before the arrow ('%s')", t);
}

static void pg_widgets(void)
{
	checkf(acts == 1 && act_user == (void *)0x1234, "set_button: the app's function was called once, with its user pointer");
	checkf(val_acts == 0, "set_value not yet");
}

static void pg_widgets2(void)
{
	checkf(val_acts == 1 && !strcmp(valtext, " changed...    "), "set_value: the app's function called, its label changed");
	check(TREE[o_val].ob_type == G_BOXTEXT && ((TEDINFO *)TREE[o_val].ob_spec)->te_thickness == -1 && TREE[o_val].ob_width == 16 * 8 &&
	      !(TREE[o_val].ob_state & SELECTED) && !(TREE[o_btn].ob_state & SELECTED) && TREE[o_btn].ob_type == G_BUTTON,
	      "the value button is an outlined G_BOXTEXT without the arrow, the button a G_BUTTON, neither left SELECTED");
}

static void test_widgets(void)
{
	SET_DEF d = def3(pages3, 3);
	int r, d0;
	reset();
	{
		static const STEP s[] = { OBJ(o_pop), CALL(pg_pop), CANCELB, END };
		pop_answer = 2;
		r = run(&d, s);
	}
	reset();
	{
		static const STEP s[] = { OBJ(o_pop), CALL(pg_pop_none), CANCELB, END };
		pop_answer = -1;
		r = run(&d, s);
	}
	reset();
	{
		static const STEP s[] = { TAB(2), OBJ(o_pop2), CALL(pg_pop_long), CANCELB, END };
		pop_answer = 0;
		r = run(&d, s);
	}
	reset();
	{
		static const STEP s[] = { TAB(2), OBJ(o_btn), CALL(pg_widgets), OBJ(o_val), CALL(pg_widgets2), OKB, END };
		d0 = draws;
		r = run(&d, s);
		check(r == 1 && draws > d0 + 2, "each button's work was followed by the dialog drawn again (dlg_draw)");
	}
}

static int uncovers;
static short unc[4];
static void uncover(short x, short y, short w, short h)
{
	uncovers++;
	unc[0] = x, unc[1] = y, unc[2] = w, unc[3] = h;
}

static void test_uncover(void)
{
	SET_DEF d = def3(pages3, 3);
	int r, d0;
	(void)r;
	/* a pop-up whose screen wasn't kept (no memory): the app's windows in its area, then the whole dialog again */
	reset();
	{
		static const STEP s[] = { OBJ(o_pop), CANCELB, END };
		settings_uncover = uncover;
		uncovers = 0;
		pop_answer = 3;
		pop_kept = 0;
		d0 = draws;
		r = run(&d, s);
		pop_kept = 1;
		checkf(uncovers == 1 && unc[0] == pop_x && unc[2] == 200 && draws > d0,
		       "a pop-up whose screen wasn't saved: the app's uncover hook gets the pop-up's area, the dialog is drawn again (%d)", uncovers);
	}
	/* kept: nothing to draw but the button */
	reset();
	{
		static const STEP s[] = { OBJ(o_pop), CANCELB, END };
		uncovers = 0;
		pop_answer = 3;
		r = run(&d, s);
		checkf(uncovers == 0, "a pop-up whose screen was put back: no uncover call (%d)", uncovers);
	}
	/* a button's dialog (a file selector) covered the screen: the hook gets the dialog's area (TOS) */
	reset();
	{
		static const STEP s[] = { TAB(2), OBJ(o_btn), CANCELB, END };
		uncovers = 0;
		r = run(&d, s);
		checkf(uncovers == 1 && unc[2] == 640 && unc[3] == 381, "a button's own dialog over ours (TOS): the uncover hook gets the desktop, then the dialog is drawn again (%d x %d)",
		       unc[2], unc[3]);
	}
	/* in a window the AES asks for what was covered: no hook */
	reset();
	ap_count = -1;
	{
		static const STEP s[] = { TAB(2), OBJ(o_btn), CANCELB, END };
		uncovers = 0;
		r = run(&d, s);
		checkf(uncovers == 0, "in a window the AES redraws what a button's dialog covered: no uncover call");
	}
	ap_count = 1;
	settings_uncover = 0;
}

static void hook(OBJECT *tree, short band)
{
	(void)tree;
	hook_calls += band == 1;
}

static void pg_window(void)
{
	checkf(win_made == 1 && win_open == 1 && !strcmp(win_name, "Settings"), "a multitasking AES: one window, named '%s'", win_name);
	checkf(TREE[1].ob_flags & HIDETREE, "in the window the title band is hidden (the window's title says it, 3.5)");
	checkf(TREE[ref_ok].ob_y == (17 - 2) * 16 - 16 && TREE[2].ob_y == 0,
	       "... and what was below it is a row higher (OK at y %d, the list's line at y %d)", TREE[ref_ok].ob_y, TREE[2].ob_y);
}

static void test_window(void)
{
	SET_DEF d = def3(pages3, 3);
	int r;
	reset();
	{
		static const STEP s[] = { KEY(KEY_UNDO), END };
		settings_drawn = hook;
		r = run(&d, s);
		settings_drawn = 0;
		checkf(r == 0 && dial_start == 1 && win_made == 0 && updates == 0 && update_low >= 0,
		       "TOS: over the screen (form_dial start; the screen under it put back), no window, wind_update balanced (%d)", updates);
		checkf(hook_calls == 1, "settings_drawn called once, as the dialog opened, with the band (%d)", hook_calls);
	}
	reset();
	ap_count = -1;
	{
		static const STEP s[] = { CALL(pg_window), KEY(K('9')), MSG(WM_CLOSED), END };
		r = run(&d, s);
		checkf(r == 0 && win_open == 0 && dial_start == 0 && !strcmp(s_num, "30"),
		       "in a window: the closer is Cancel ('9' typed is gone: '%s'), the window closed", s_num);
	}
	reset();
	ap_count = -1;
	{
		static const STEP s[] = { TAB(2), KEY(KEY_UNDO), END };
		r = run(&d, s);
		checkf(r == 0 && win_open == 0, "in a window: a page change, then Undo: Cancel");
	}
	reset();
	ap_count = -1;
	{
		static const STEP s[] = { KEY(K('1')), KEY(KEY_RETURN), END };
		r = run(&d, s);
		checkf(r == 1 && !strcmp(s_num, "301"), "in a window: Return is OK ('%s')", s_num);
	}
	ap_count = 1;
}

/* ---------------- limits ---------------- */

static short extra_count;
static void page_many(SETTINGS *s, short x, short y, void *user)
{
	short i;
	(void)user;
	for (i = 0; i < extra_count; i++)
		set_text(s, x, y, "x");
	for (i = 0; i < 40; i++) {
		static short v;
		set_tick(s, x, y, "t", &v);
	}
	for (i = 0; i < 40; i++)
		set_popup(s, x, y, 10, when, 4, &s_when);
}

/* EMail 2.14's six pages: how many objects the real dialog has (counted from its code: 69 for the pages, band,
   list, tabs, headings and buttons included) */
static void page_emailish(SETTINGS *s, short x, short y, void *user)
{
	short i, n = *(short *)user;
	(void)y;
	for (i = 0; i < n; i++)
		set_text(s, x, set_y(s, i % 8), "a setting");
}

static short seen_nobj, seen_lost;
static void pg_counts(void)
{
	seen_nobj = set_db(cur)->nobj;
	seen_lost = set_db(cur)->lost;
}

static void page_many_cur(SETTINGS *s, short x, short y, void *user)
{
	cur = s;
	page_many(s, x, y, user);
}

static void page_emailish_cur(SETTINGS *s, short x, short y, void *user)
{
	cur = s;
	page_emailish(s, x, y, user);
}

static void test_limits(void)
{
	static SET_PAGE pg[6];
	SET_DEF d;
	short n[6] = { 9, 2, 7, 6, 10, 18 }, k;
	int r;
	static const STEP counts[] = { CALL(pg_counts), KEY(KEY_UNDO), END };
	reset();
	/* EMail's six pages: 9 + 2 + 7 + 6 + 10 + 18 = 52 objects on the pages, 12 for their headings and lines, 6 + 3 for
	   the list, 2 buttons: about 75 of the 160 */
	for (k = 0; k < 6; k++)
		pg[k] = (SET_PAGE){ "Page", page_emailish_cur };
	d = def3(pg, 6);
	d.user = &n[0];
	{
		short per[6] = { 9, 9, 9, 9, 9, 9 };
		d.user = per;
		r = run(&d, counts);
		checkf(r == 0 && seen_lost == 0 && seen_nobj > 60, "six pages of 9 settings (%d objects of %d): all fit, nothing lost", seen_nobj,
		       SET_OBJECTS);
		per[0] = per[1] = per[2] = per[3] = per[4] = per[5] = 22;
		r = run(&d, counts);
		checkf(r == 0 && seen_lost == 0 && seen_nobj > 150, "six pages of 22 (%d objects, 150 and more): still in the table of %d", seen_nobj,
		       SET_OBJECTS);
	}
	/* too many: what doesn't fit is left out, nothing is written past the table */
	for (k = 0; k < 3; k++)
		pg[k] = (SET_PAGE){ "Many", page_many_cur };
	d = def3(pg, 3);
	extra_count = 300;
	r = run(&d, counts);
	checkf(r == 0 && seen_lost > 0 && seen_nobj <= SET_OBJECTS - 1,
	       "300 texts, 40 ticks and 40 pop-ups on each of 3 pages: %d objects made, %d lost; the dialog opens and closes (AddressSanitizer: no write past a table)",
	       seen_nobj, seen_lost);
	/* a bigger table in the definition */
	d.max_objects = 1200;
	extra_count = 100;
	r = run(&d, counts);
	checkf(r == 0 && seen_nobj > SET_OBJECTS && seen_nobj <= 1200, "max_objects 1200: the table is the app's size (%d objects, %d lost)", seen_nobj,
	       seen_lost);
	extra_count = 0;
	{
		DLGB b;
		static OBJECT tr[20];
		static TEDINFO te[4];
		static DB_TICK tk[2];
		static LFIELD lf[1];
		short i;
		db_init(&b, tr, 20, te, 4, tk, 2, lf, 1);
		db_begin(&b, 40, 10);
		for (i = 0; i < 40; i++)
			db_text(&b, 1, 1, "x");
		checkf(b.lost == 40 - 18 && b.nobj == 19, "a static builder, 20 objects: 18 of 40 made, 22 lost, the spare unlinked (%d, %d)", b.nobj,
		       b.lost);
		db_end(&b);
		check(tr[18].ob_flags & LASTOB, "... and the list ends at the last object made");
		for (i = 0; i < 6; i++)
			db_ted(&b, "t", 0, 0);
		check(b.nted == 3 && b.lost > 22, "a full text table: the spare is shared, and counted");
		db_tick(&b, 0, 0, "a", &i);
		db_tick(&b, 0, 0, "b", &i);
		check(db_tick(&b, 0, 0, "c", &i) == -1, "a full tick table gives -1");
	}
}

static void test_pw(void)
{
	static const SET_PAGE pw[] = { { "General", page_a }, { "Look", page_b }, { "Secure", page_c_pw } };
	SET_DEF d = def3(pw, 3);
	int r;
	static STEP s[40];
	short n = 0, i;
	reset();
	s[n++] = (STEP)TAB(3);
	s[n++] = (STEP)OBJ(o_pw);
	for (i = 0; i < 6; i++)
		s[n++] = (STEP)KEY(K('Z'));
	s[n++] = (STEP)OKB;
	s[n] = (STEP)END;
	r = run(&d, s);
	checkf(r == 1 && !strcmp(s_pw, "secretZZZZZZ"), "a password field (mask '*') holds what is typed and shows stars ('%s' kept)", s_pw);
}

/* dlgbuild.h's example, as written there: a dialog of its own that stays open while a tick is clicked */
static char ex_name[31], ex_port[6];
static short ex_on;
static short ex_f, ex_ok, ex_cancel, ex_box;
static DLGB ex_b;

static int example(void)
{
	short x, y, w, h, r, k;
	if (!db_alloc(&ex_b, 40, 20, 4, 4, 0))
		return -2;
	db_begin(&ex_b, 40, 10);
	db_text(&ex_b, 2, 1, "Server");
	db_text(&ex_b, 2, 3, "Name:");
	ex_f = db_framed(&ex_b, db_long(&ex_b, 10, 3, ex_name, 20, 30, 0));
	db_text(&ex_b, 2, 4, "Port:");
	db_field(&ex_b, 10, 4, ex_port, 5, '9');
	db_tick(&ex_b, 2, 6, "Use TLS", &ex_on);
	ex_box = ex_b.tick[0].box;
	ex_cancel = db_button(&ex_b, 16, 8, 10, "Cancel", EXIT);
	ex_ok = db_button(&ex_b, 28, 8, 10, "OK", EXIT | DEFAULT);
	db_end(&ex_b);
	stub_tree = ex_b.tree;
	dlg_name("Server");
	dlg_open(ex_b.tree, &x, &y, &w, &h);
	for (;;) {
		r = dlg_do(ex_b.tree, ex_f, ex_cancel) & 0x7fff;
		if ((k = db_tick_hit(&ex_b, r)) >= 0) {
			db_tick_flip(&ex_b, k);
			continue;
		}
		break;
	}
	dlg_close();
	k = r == ex_ok;
	db_free(&ex_b);
	return k;
}

static void ex_refs(void)
{
	if (ex_b.tree) {
		ref_ok = ex_ok;
		ref_cancel = ex_cancel;
	}
}

static void test_example(void)
{
	int r;
	reset();
	ex_name[0] = ex_port[0] = 0;
	ex_on = 0;
	refs_hook = ex_refs;
	{
		static const STEP s[] = { KEY(K('a')), KEY(K('b')), KEY(0x0f09), KEY(K('8')), KEY(K('0')), OBJ(ex_box), OKB, END };
		script = s;
		if (setjmp(script_out)) {
			check(0, "dlgbuild.h's example: the script ended with the dialog open");
			return;
		}
		r = example();
		checkf(r == 1 && !strcmp(ex_name, "ab") && !strcmp(ex_port, "80") && ex_on == 1,
		       "dlgbuild.h's example, as written: typed 'ab', Tab, '80', the tick clicked, OK (%d '%s' '%s' %d)", r, ex_name, ex_port, ex_on);
	}
	ex_on = 0;
	{
		static const STEP s[] = { OBJ(ex_box), KEY(KEY_UNDO), END };
		script = s;
		if (setjmp(script_out)) {
			check(0, "the example's Undo: the script ended with the dialog open");
			return;
		}
		r = example();
		checkf(r == 0 && ex_on == 1, "the example's Undo ends it (0); db_tick_flip changed the variable (%d: the app decides)", ex_on);
	}
	refs_hook = refs;
}

/* settings.h's example, as written there (opt stands in for the app's options) */
static struct { short when, log, port; char dir[100]; } opt = { 1, 0, 8180, "C:\\GUIDES" };
static short h_when, h_log;
static char h_port[6], h_dir[100];
static const char *const h_choices[] = { "When first opened", "At start", "After a minute", "After 5 minutes" };

static void h_general(SETTINGS *s, short x, short y, void *user)
{
	(void)user;
	stub_tree = set_tree(s);	/* (the test's, not the example's: where the stand-in AES finds the objects) */
	set_text(s, x, y, "The accessory looks through the library:");
	set_popup(s, x + 2, set_y(s, 1), 23, h_choices, 4, &h_when);
	set_text(s, x, set_y(s, 3), "Folder:");
	set_long(s, x, set_y(s, 4), h_dir, 30, 99, 0);
	set_text(s, x, set_y(s, 6), "Port:");
	set_field(s, x + SET_FX, set_y(s, 6), h_port, 5, '9');
	set_tick(s, x, set_y(s, 7), "Write a log", &h_log);
}

static void h_library(SETTINGS *s, short x, short y, void *user)
{
	(void)user;
	set_text(s, x, y, "Layout:");
}

static int h_settings(void)
{
	static const SET_PAGE pages[] = { { "General", h_general }, { "Library", h_library } };
	h_when = opt.when;
	h_log = opt.log;
	snprintf(h_port, sizeof h_port, "%u", (unsigned)opt.port % 100000u);
	strcpy(h_dir, opt.dir);
	if (!settings_run("Settings", pages, 2, 42, 17, 0))
		return 0;
	opt.when = h_when;
	opt.log = h_log;
	opt.port = (short)atoi(h_port);
	strcpy(opt.dir, h_dir);
	return 1;
}

static short hx_pop, hx_tick;
static void h_refs(void)
{
	if (stub_tree) {
		/* the example's objects are the first page's: its pop-up is the first G_BOXTEXT after the list, the tick the first G_IMAGE */
		short i;
		for (i = 1; i < 80; i++) {
			if (stub_tree[i].ob_flags & LASTOB) {
				ref_ok = i;		/* Cancel and OK are the last two objects */
				ref_cancel = (short)(i - 1);
				break;
			}
			if (stub_tree[i].ob_type == G_BOXTEXT && (stub_tree[i].ob_flags & TOUCHEXIT) && stub_tree[i].ob_width == 23 * 8)
				hx_pop = i;
			if (stub_tree[i].ob_type == G_IMAGE && !hx_tick)
				hx_tick = i;
		}
	}
}

static void test_header_example(void)
{
	int r;
	static const STEP s[] = { OBJ(hx_pop), KEY(0x0f09), KEY(KEY_ESC), KEY(K('9')), KEY(K('0')), KEY(K('0')), KEY(K('0')), OBJ(hx_tick), OKB, END };
	reset();
	opt.when = 1, opt.log = 0, opt.port = 8180;
	strcpy(opt.dir, "C:\\GUIDES");
	hx_pop = hx_tick = 0;
	pop_answer = 2;
	/* the example is not given the stub's tree: take it from the open dialog through the AES stand-in */
	refs_hook = h_refs;
	script = s;
	if (setjmp(script_out)) {
		check(0, "settings.h's example: the script ended with the dialog open");
		refs_hook = refs;
		return;
	}
	r = h_settings();
	checkf(r == 1 && opt.when == 2 && opt.log == 1 && opt.port == 9000 && !strcmp(opt.dir, "C:\\GUIDES"),
	       "settings.h's example, as written: pop-up chose 'After a minute', Tab to the port, Esc emptied it, 9000 typed, the tick clicked, OK: %d, when %d, log %d, port %d",
	       r, opt.when, opt.log, opt.port);
	refs_hook = refs;
}

int main(void)
{
	geometry(8, 16, 0, 0, 17, "EMail: 3 pages, 8x16, 57 x 17");
	geometry(8, 16, 12, 44, 20, "EWeb: list 12, page 44, 20 rows");
	geometry(8, 8, 0, 0, 17, "8x8 cells (ST medium)");
	test_pages_and_ticks();
	test_page_switch();
	test_radios();
	test_keys();
	test_keys2();
	test_keys_and_values();
	test_check();
	test_widgets();
	test_window();
	test_uncover();
	test_limits();
	test_pw();
	test_example();
	test_header_example();
	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
