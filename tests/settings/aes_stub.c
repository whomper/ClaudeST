/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The host test's stand-in AES for settings.c (and dlgbuild.c, dialog.c, field.c): objects have real places
 * (rsrc_obfix_all is the AES's arithmetic, objc_find a real hit test that skips hidden objects), the events
 * come from a script of keys, messages and clicks on objects (found by their places, as a mouse would),
 * popup() answers from a script. What Hatari checks on the Atari is pixels; this is the tree and the keys.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "ui.h"
#include "dialog.h"
#include "field.h"
#include "alert.h"
#include "popup.h"
#include "clip.h"
#include "aes_stub.h"

/* the script */
const STEP *script;
jmp_buf script_out;
short ref_tab[SMAXTAB + 1];		/* page k's tab object, from 1, filled in by the test */
short ref_ok, ref_cancel;
short cell_w = 8, cell_h = 16;		/* the system font's cell: 8x16 (VGA, mono), 8x8 (medium) */
short ap_count = 1;			/* 1 TOS; -1 a multitasking AES */
short pre_key;				/* a key typed before the dialog came */

int draws, bells, updates, update_low, dial_start, dial_finish, alerts, win_made, win_open, timers;
short last_draw[4];
char win_name[40];
short win[4] = { 100, 60, 456, 272 };
static short top_win;
#define DLGWIN 5

/* ---- the popup */
GRECT popup_area;
short popup_put_back = 1;
int popups;
short pop_x, pop_y, pop_n, pop_answer = -1, pop_kept = 1;
char pop_lab[20][40];
int pop_new;				/* 1: the last call was popup_m's (the margin is popup's) */
static short pop_run(short x, short y, const char *const *labels, short n)
{
	short i;
	popups++;
	pop_x = x;
	pop_y = y;
	pop_n = n;
	for (i = 0; i < n && i < 20; i++) {
		strncpy(pop_lab[i], labels[i], 39);
		pop_lab[i][39] = 0;
	}
	popup_put_back = pop_kept;
	popup_area.x = x;
	popup_area.y = y;
	popup_area.w = 200;
	popup_area.h = n * (cell_h + 2) + 4;
	return pop_answer;
}
short popup(short x, short y, const char *const *labels, short n)
{
	pop_new = 0;
	return pop_run(x, y, labels, n);
}
short popup_m(short x, short y, const char *const *labels, short n)
{
	pop_new = 1;
	return pop_run(x, y, labels, n);
}
short popup_over(short x, short y, const char *const *labels, short n, short sel)
{
	pop_new = 1;	/* the real one puts row sel a pixel above the button: rows cell_h + 2 high from 2 down */
	return pop_run(x, (short)(y - 3 - sel * (cell_h + 2)), labels, n);
}

short alert(short def, const char *fmt, ...)
{
	(void)def, (void)fmt;
	alerts++;
	return 1;
}

/* ---- the clipboard */
int clip_key(short key)
{
	(void)key;
	return CLIP_NONE;
}
int clip_put(const char *text, long n)
{
	(void)text, (void)n;
	return 1;
}
long clip_get(char *buf, long size)
{
	(void)buf, (void)size;
	return 0;
}
char *clip_line(char *buf)
{
	return buf;
}

void fake_bconout(short dev, short c)
{
	if (dev == 2 && c == 7)
		bells++;
}

void nf_debug(const char *s)
{
	(void)s;
}

long get_cookie(long id)
{
	(void)id;
	return 0;
}

short aes_version(void)
{
	return 0x0410;
}

short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4)
{
	*o1 = type == 13;
	*o2 = *o3 = *o4 = 0;
	return 1;
}

short aes_count(void)
{
	return ap_count;
}

/* ---- windows (a multitasking AES) */
short wind_create(short kind, short x, short y, short w, short h)
{
	(void)kind, (void)x, (void)y, (void)w, (void)h;
	win_made++;
	return DLGWIN;
}

short wind_open(short h, short x, short y, short w, short ht)
{
	(void)x, (void)y, (void)w, (void)ht;
	if (h == DLGWIN) {
		win_open++;
		top_win = DLGWIN;
	}
	return 1;
}

short wind_close(short h)
{
	if (h == DLGWIN)
		win_open--;
	return 1;
}

short wind_delete(short h)
{
	(void)h;
	return 1;
}

short wind_set_str(short h, short field, const char *str)
{
	if (h == DLGWIN && field == WF_NAME && str)
		strncpy(win_name, str, sizeof(win_name) - 1);
	return 1;
}

short wind_calc(short type, short kind, short x, short y, short w, short h, short *ox, short *oy, short *ow,
		short *oh)
{
	(void)kind;
	if (type == WC_BORDER) {
		*ox = x - 1, *oy = y - 20, *ow = w + 2, *oh = h + 21;
	} else {
		*ox = x + 1, *oy = y + 20, *ow = w - 2, *oh = h - 21;
	}
	return 1;
}

short wind_get(short h, short field, short *a, short *b, short *c, short *d)
{
	*a = *b = *c = *d = 0;
	if (field == WF_WORKXYWH && h == 0) {
		*a = 0, *b = 19, *c = 640, *d = 381;
	} else if ((field == WF_WORKXYWH || field == WF_FIRSTXYWH || field == WF_CURRXYWH) && h == DLGWIN) {
		*a = win[0], *b = win[1], *c = win[2], *d = win[3];
	} else if (field == WF_TOP) {
		*a = top_win;
	}
	return 1;
}

short wind_set(short h, short field, short a, short b, short c, short d)
{
	(void)a, (void)b, (void)c, (void)d;
	if (field == WF_TOP)
		top_win = h;
	return 1;
}

short wind_find(short x, short y)
{
	(void)x, (void)y;
	return DLGWIN;
}

short appl_id(void)
{
	return 1;
}

short appl_write(short id, short len, const void *buf)
{
	(void)id, (void)len, (void)buf;
	return 1;
}

short wind_update(short mode)
{
	updates += mode == BEG_UPDATE || mode == 3 ? 1 : -1;
	if (updates < update_low)
		update_low = updates;
	return 1;
}

/* ---- the screen under a box (dlg_under) */
void v_clsvwk(short h)
{
	(void)h;
}

short v_opnvwk_(short phys, short *work_out)
{
	(void)phys;
	work_out[0] = 639;
	work_out[1] = 399;
	return 7;
}

void vq_extnd(short h, short owflag, short *work_out)
{
	(void)h, (void)owflag;
	work_out[4] = 1;
}

void vq_color(short h, short color, short *rgb)
{
	(void)h, (void)color;
	rgb[0] = rgb[1] = rgb[2] = 0;
}

void vro_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst)
{
	MFDB *m = src->fd_addr ? src : dst;
	(void)h, (void)mode, (void)pxy;
	memset(m->fd_addr, 0x55, (size_t)m->fd_wdwidth * 2 * m->fd_h * m->fd_nplanes);
}

/* ---- objects */
static short obfix(short v, short unit)
{
	short off = (short)(((unsigned short)v >> 8) & 0xff);
	if (off > 128)
		off -= 256;
	return (short)((v & 0xff) * unit + off);
}

void rsrc_obfix_all(OBJECT *t, short n)
{
	short i;
	for (i = 0; i < n; i++) {
		t[i].ob_x = obfix(t[i].ob_x, cell_w);
		t[i].ob_y = obfix(t[i].ob_y, cell_h);
		t[i].ob_width = obfix(t[i].ob_width, cell_w);
		t[i].ob_height = obfix(t[i].ob_height, cell_h);
	}
}

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
	*wchar = cell_w;
	*hchar = cell_h;
	*wbox = cell_w + 2;
	*hbox = cell_h + 3;
	return 1;
}

short graf_mouse(short num, void *form)
{
	(void)num, (void)form;
	return 1;
}

short graf_mkstate(short *mx, short *my, short *mb, short *ks)
{
	*mx = *my = *mb = *ks = 0;
	return 1;
}

short graf_watchbox(OBJECT *t, short obj, short in, short out)
{
	(void)t, (void)obj, (void)in, (void)out;
	return 1;
}

short form_center(OBJECT *t, short *x, short *y, short *w, short *h)
{
	t[0].ob_x = (640 - t[0].ob_width) / 2;
	t[0].ob_y = (400 - t[0].ob_height) / 2;
	*x = t[0].ob_x;
	*y = t[0].ob_y;
	*w = t[0].ob_width;
	*h = t[0].ob_height;
	return 1;
}

short form_dial(short flag, short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	if (flag == FMD_START)
		dial_start++;
	if (flag == FMD_FINISH)
		dial_finish++;
	return 1;
}

short objc_offset(OBJECT *t, short obj, short *x, short *y)
{
	*x = t[0].ob_x + (obj ? t[obj].ob_x : 0);
	*y = t[0].ob_y + (obj ? t[obj].ob_y : 0);
	return 1;
}

/* the menu bar's title shown normal: dlg_evnt's MD17 (dialog.c), which a window dialog's test can send a MN_SELECTED for */
int tnormal_calls;
short menu_tnormal(OBJECT *t, short title, short normal)
{
	(void)t, (void)title, (void)normal;
	tnormal_calls++;
	return 1;
}

/* the focus ring (ui.h's drawing): frame() is the ring, drawn by settings.c after the tree */
int ring_draws;
GRECT ring_last;
void frame(GRECT *r, short color)
{
	(void)color;
	ring_draws++;
	ring_last = *r;
}
void clip_on(GRECT *r)
{
	(void)r;
}
void clip_off(void)
{
}

/* the clock the page turns are timed by (dialog.c's budget): it moves draw_cost ms for each object drawn */
unsigned long fake_ms;
short draw_cost;
unsigned long tos_ms(void)
{
	return fake_ms;
}

short draw_log[1024], draw_depth[1024];		/* the objects objc_draw was asked for, and how deep */
int ndraw_log;

short objc_draw(OBJECT *t, short start, short depth, short x, short y, short w, short h)
{
	(void)t;
	if (ndraw_log < 1024) {
		draw_log[ndraw_log] = start;
		draw_depth[ndraw_log++] = depth;
	}
	fake_ms += draw_cost;
	draws++;
	last_draw[0] = x, last_draw[1] = y, last_draw[2] = w, last_draw[3] = h;
	return 1;
}

/* the object under the pointer: the last child drawn there, none that is hidden; 0 the box, -1 outside */
short objc_find(OBJECT *t, short start, short depth, short x, short y)
{
	short i, hit = 0;
	(void)start, (void)depth;
	if (x < t[0].ob_x || y < t[0].ob_y || x >= t[0].ob_x + t[0].ob_width || y >= t[0].ob_y + t[0].ob_height)
		return -1;
	for (i = t[0].ob_head; i > 0; i = t[i].ob_next) {
		if (t[i].ob_flags & HIDETREE)
			continue;
		if (x >= t[0].ob_x + t[i].ob_x && x < t[0].ob_x + t[i].ob_x + t[i].ob_width &&
		    y >= t[0].ob_y + t[i].ob_y && y < t[0].ob_y + t[i].ob_y + t[i].ob_height)
			hit = i;
		if (t[i].ob_flags & LASTOB)
			break;
	}
	return hit;
}

static short find_flag(OBJECT *t, short from, short inc, unsigned short flag)
{
	short i;
	for (i = from + inc; i > 0 && i < 400; i += inc) {
		if ((t[i].ob_flags & flag) && !(t[i].ob_flags & HIDETREE))
			return i;
		if (inc > 0 && (t[i].ob_flags & LASTOB))
			break;
	}
	return from;
}

static short find_default(OBJECT *t)
{
	short i;
	for (i = 1;; i++) {
		if ((t[i].ob_flags & DEFAULT) && !(t[i].ob_flags & HIDETREE))
			return i;
		if (t[i].ob_flags & LASTOB)
			return 0;
	}
}

short form_keybd(OBJECT *t, short obj, short next, short ch, short *onext, short *och)
{
	short scan = (ch >> 8) & 0xff;
	*och = 0;
	*onext = next;
	if (scan == 0x1c || scan == 0x72) {
		*onext = find_default(t);
		if (*onext > 0) {
			t[*onext].ob_state |= SELECTED;
			return 0;
		}
		*onext = 0;
	} else if (scan == 0x0f || scan == 0x50) {
		*onext = find_flag(t, obj, 1, EDITABLE);
	} else if (scan == 0x48) {
		*onext = find_flag(t, obj, -1, EDITABLE);
	} else {
		*och = ch;
	}
	return 1;
}

short form_button(OBJECT *t, short obj, short clicks, short *onext)
{
	short cont = 1, high = 0;
	if (t[obj].ob_flags & TOUCHEXIT) {
		if (clicks == 2)
			high = (short)0x8000;
		cont = 0;
	}
	if (t[obj].ob_flags & SELECTABLE)
		t[obj].ob_state ^= SELECTED;
	if ((t[obj].ob_state & SELECTED) && (t[obj].ob_flags & EXIT))
		cont = 0;
	if (cont && !(t[obj].ob_flags & EDITABLE))
		obj = 0;
	*onext = obj | high;
	return cont;
}

short objc_edit(OBJECT *t, short obj, short ch, short *idx, short kind)
{
	char *s = ((TEDINFO *)t[obj].ob_spec)->te_ptext;
	short max = ((TEDINFO *)t[obj].ob_spec)->te_txtlen - 1;
	if (kind == ED_INIT) {
		*idx = (short)strlen(s);
	} else if (kind == ED_CHAR) {
		if (ch == 0x011b || (ch & 0xff) == 0x1b) {
			s[0] = 0;
			*idx = 0;
		} else if ((ch & 0xff) >= ' ' && *idx < max) {
			s[(*idx)++] = (char)ch;
			s[*idx] = 0;
		}
	}
	return 1;
}

void evnt_timer_wait(void);
short evnt_timer_(unsigned long ms)
{
	(void)ms;
	timers++;
	return 1;
}

/* ---- the events */
extern OBJECT *stub_tree;
void (*refs_hook)(void);		/* the dialog's tree, set by the test's page builder (set_tree) */

static void click_at(OBJECT *t, short obj, short *x, short *y)
{
	*x = t[0].ob_x + t[obj].ob_x + t[obj].ob_width / 2;
	*y = t[0].ob_y + t[obj].ob_y + t[obj].ob_height / 2;
}

short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev)
{
	(void)clicks, (void)mask, (void)state, (void)timer;
	memset(ev, 0, sizeof(*ev));
	if (refs_hook)
		refs_hook();
	if (flags & MU_TIMER) {
		if (pre_key) {
			ev->which = MU_KEYBD;
			ev->kreturn = pre_key;
			pre_key = 0;
		} else {
			ev->which = MU_TIMER;
		}
		return ev->which;
	}
	while (script->kind == S_CALL) {
		script->fn();
		script++;
	}
	if (script->kind == S_END)
		longjmp(script_out, 1);	/* the dialog is still waiting when the script ended */
	switch (script->kind) {
	case S_KEY:
		ev->which = MU_KEYBD;
		ev->kreturn = script->a;
		ev->kstate = script->b;
		break;
	case S_MSG:
		ev->which = MU_MESAG;
		memset(msg, 0, 16);
		msg[0] = script->a;
		msg[3] = DLGWIN;
		break;
	default: {
		short obj = -1;
		ev->which = MU_BUTTON;
		ev->breturn = 1;
		switch (script->kind) {
		case S_TAB: obj = ref_tab[script->a]; break;
		case S_OK: obj = ref_ok; break;
		case S_CANCEL: obj = ref_cancel; break;
		case S_OBJ: obj = *script->p; break;
		case S_XY: ev->mx = script->a; ev->my = script->b; break;
		}
		if (script->kind == S_XY)
			break;
		click_at(stub_tree, obj, &ev->mx, &ev->my);
		break;
	}
	}
	script++;
	return ev->which;
}
