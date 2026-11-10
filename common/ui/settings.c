/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.c - the family's Settings dialog (settings.h; the guidelines, 6.1): EMail 2.14's settings_sidebar,
 * show_page and settings_run, with the pages left to the app. Built with dlgbuild.c, run by dialog.c's dlg_do.
 */
#include <stdlib.h>
#include <string.h>
#include "gemui.h"
#include "dialog.h"
#include "popup.h"
#include "alert.h"
#include "settings.h"

#define SET_WMAX 16		/* pop-ups and buttons the app may add */
#define SET_FMAX 24		/* fields: their text is kept for Cancel */
#define SET_POPMAX 16		/* choices of a pop-up */
#define SET_NAME 31		/* characters of a choice shown */
#define SET_TURN_MS 20		/* a page is drawn in turns of this long (and 1 object at least), an event call between */
#define SET_LAB 48		/* a pop-up button's text, w at most 46 */

enum { W_POPUP = 1, W_ACT, W_VALUE, W_RADIO };

typedef struct {
	short obj, kind, w, n, was;
	short *sel;
	const char *const *ch;
	char *label;		/* what the button shows (w characters): lab for a pop-up, the app's for a value */
	SET_ACT act;
	void *user;
	unsigned short off;	/* bit c: choice c is greyed (a radio group's, a pop-up's) */
	SET_WHEN when;		/* decides which choices apply, or NULL */
	void *wuser;
	char lab[SET_LAB];
} SET_W;

typedef struct {
	char *buf;
	short cap;		/* its bytes, the end not counted */
} SET_F;

struct SETTINGS {
	DLGB b;
	const SET_DEF *d;
	char band_text[40];		/* " Settings" */
	short sw, sx, pw, w, h, gap;
	short npages, cur, adding;
	short row0, rgap;		/* set_y: the page's first row, the pixels a row more */
	short ok, cancel, pages_obj;
	short tab[SET_MAXPAGES + 1];	/* from 1 */
	short pfirst[SET_MAXPAGES + 2];	/* page k's objects are pfirst[k] to pfirst[k + 1] - 1 (from 1) */
	char tabname[SET_MAXPAGES][16];
	unsigned char *page;		/* the page of each object, 0: on every page */
	SET_W *wk;
	short nw;
	SET_F *fl;
	short nf;
	char *pool;			/* the fields' text when the dialog opened */
	short line_obj;			/* the list's line (the ring round a page name reaches it) */
	short want;			/* set_focus: the field the text cursor goes to when dlg_do is entered next (0 none) */
	short focus;			/* the keyboard's: 0 the text cursor (or nothing), -1 the page list, else the control's object */
	short kb;			/* a key has been used since the last click: only then is the focus shown (a ring) */
	short ringed;			/* the ring is on the screen */
	short live;			/* the dialog is open: a change is drawn */
};

void (*settings_drawn)(OBJECT *tree, short band);
void (*settings_uncover)(short x, short y, short w, short h);

/* ---------------- what a page's builder calls ---------------- */

short set_y(const SETTINGS *s, short n)
{
	short px = (short)(2 + n * s->rgap);
	if (px > 127)
		px = 127;		/* the high byte is a signed number of pixels */
	return (short)((s->row0 + n) | (px << 8));
}

short set_text(SETTINGS *s, short x, short y, const char *text)
{
	return db_text(&s->b, x, y, text);
}

short set_tick(SETTINGS *s, short x, short y, const char *label, short *var)
{
	return db_tick(&s->b, x, y, label, var);
}

void set_tick_group(SETTINGS *s, short tick, short group)
{
	db_tick_group(&s->b, tick, group);
}

void set_tick_enable(SETTINGS *s, short tick, int on)
{
	db_tick_enable(&s->b, tick, on);
}

static void keep_field(SETTINGS *s, char *buf, short cap)
{
	if (s->nf >= SET_FMAX) {
		s->b.lost++;
		return;
	}
	s->fl[s->nf].buf = buf;
	s->fl[s->nf].cap = cap;
	s->nf++;
}

short set_field(SETTINGS *s, short x, short y, char *buf, short len, char kind)
{
	keep_field(s, buf, len);
	return db_field(&s->b, x, y, buf, len, kind);
}

short set_long(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask)
{
	keep_field(s, buf, cap);
	return db_framed(&s->b, db_long(&s->b, x, y, buf, len, cap, mask));
}

short set_long_bare(SETTINGS *s, short x, short y, char *buf, short len, short cap, char mask)
{
	keep_field(s, buf, cap);
	return db_long(&s->b, x, y, buf, len, cap, mask);
}

short set_frame(SETTINGS *s, short x, short y, short w, short rows)
{
	return db_add(&s->b, G_BOX, 0, 0, 0x00FF1100L, x, y, w, rows);
}

static SET_W *new_w(SETTINGS *s, short kind)
{
	SET_W *p;
	if (s->nw >= SET_WMAX) {
		s->b.lost++;
		return 0;
	}
	p = &s->wk[s->nw++];
	memset(p, 0, sizeof(*p));
	p->kind = kind;
	return p;
}

/* a widget that found no room (its object is the spare, which is not in the tree) is not kept: nothing may
   find it, a click on it or Cancel's object being the spare too would open it; 0 */
static short kept(SETTINGS *s, SET_W *p)
{
	if (db_spare(&s->b, p->obj)) {
		s->nw--;
		return 0;
	}
	return p->obj;
}

/* a pop-up button's text: a space, the choice (cut to leave one before the arrow), the system font's
   down arrow as the last character (5.7) */
static void pop_label(SET_W *p)
{
	short sel = *p->sel < 0 || *p->sel >= p->n ? 0 : *p->sel, n;
	const char *c = p->ch[sel];
	n = (short)strlen(c);
	if (n > p->w - 3)
		n = (short)(p->w - 3);
	memset(p->lab, ' ', (size_t)p->w);
	p->lab[p->w] = 0;
	memcpy(p->lab + 1, c, (size_t)n);
	p->lab[p->w - 1] = 2;
}

short set_popup(SETTINGS *s, short x, short y, short w, const char *const *choices, short n, short *sel)
{
	SET_W *p = new_w(s, W_POPUP);
	if (!p)
		return 0;
	if (w > SET_LAB - 2)
		w = SET_LAB - 2;
	if (n > SET_POPMAX)
		n = SET_POPMAX;
	p->w = w;
	p->n = n;
	p->sel = sel;
	p->was = *sel;
	p->ch = choices;
	pop_label(p);
	p->label = p->lab;
	p->obj = db_add(&s->b, G_BOXTEXT, TOUCHEXIT, 0, (long)db_ted(&s->b, p->lab, 0, 0x1180), x, y, w, 1);
	((TEDINFO *)s->b.tree[p->obj].ob_spec)->te_thickness = -1;	/* an outline (5.7) */
	return kept(s, p);
}

static SET_W *by_obj(SETTINGS *s, short obj);
static void radio_sync(SETTINGS *s, SET_W *p, int draw);

short set_radio(SETTINGS *s, short x, short y, short w, const char *const *labels, short n, short *sel)
{
	SET_W *p;
	short k;
	if (n < 1)
		return 0;
	if (n > SET_RADIOS)
		n = SET_RADIOS;
	if (w < 8)
		w = 8;
	if (w > 9)
		w = 9;
	if (s->b.nobj + n > s->b.omax - 1 - s->b.keep) {
		s->b.lost++;		/* all or none */
		return 0;
	}
	p = new_w(s, W_RADIO);
	if (!p)
		return 0;
	p->w = w;
	p->n = n;
	p->sel = sel;
	p->was = *sel;
	p->ch = labels;
	for (k = 0; k < n; k++) {
		/* 10 characters apart, and 4 pixels more for each (EFTP's, where the 3D buttons grow by a few) */
		short o = db_add(&s->b, G_BUTTON, TOUCHEXIT | RBUTTON, 0, (long)labels[k],
				 (short)(x + 10 * k + ((4 * k) << 8)), y, w, 1);
		if (!k)
			p->obj = o;
	}
	radio_sync(s, p, 0);
	return p->obj;
}

void set_radio_enable(SETTINGS *s, short radio, short choice, int on)
{
	SET_W *p = by_obj(s, radio);
	if (!p || (p->kind != W_RADIO && p->kind != W_POPUP) || choice < 0 || choice >= p->n)
		return;
	if (on)
		p->off &= (unsigned short)~(1u << choice);
	else
		p->off |= (unsigned short)(1u << choice);
	if (p->kind == W_RADIO)
		radio_sync(s, p, 1);
}

void set_popup_enable(SETTINGS *s, short popup, short item, int on)
{
	set_radio_enable(s, popup, item, on);
}

void set_choice_when(SETTINGS *s, short obj, SET_WHEN when, void *user)
{
	SET_W *p = by_obj(s, obj);
	if (!p || (p->kind != W_RADIO && p->kind != W_POPUP))
		return;
	p->when = when;
	p->wuser = user;
}

short set_button(SETTINGS *s, short x, short y, short w, const char *label, SET_ACT act, void *user)
{
	SET_W *p = new_w(s, W_ACT);
	if (!p)
		return 0;
	p->act = act;
	p->user = user;
	p->obj = db_button(&s->b, x, y, w, label, TOUCHEXIT);
	return kept(s, p);
}

short set_value(SETTINGS *s, short x, short y, short w, char *label, SET_ACT act, void *user)
{
	SET_W *p = new_w(s, W_VALUE);
	TEDINFO *t;
	if (!p)
		return 0;
	p->act = act;
	p->user = user;
	p->label = label;
	p->obj = db_add(&s->b, G_BOXTEXT, TOUCHEXIT, 0, (long)(t = db_ted(&s->b, label, 0, 0x1180)), x, y, w, 1);
	t->te_thickness = -1;
	if (t->te_txtlen < w + 1)
		t->te_txtlen = w + 1;
	return kept(s, p);
}

DLGB *set_db(SETTINGS *s)
{
	return &s->b;
}

OBJECT *set_tree(const SETTINGS *s)
{
	return s->b.tree;
}

short set_obj_tab(const SETTINGS *s, short k)
{
	return k >= 0 && k < s->npages ? s->tab[k + 1] : 0;
}

short set_obj_ok(const SETTINGS *s)
{
	return s->ok;
}

short set_obj_cancel(const SETTINGS *s)
{
	return s->cancel;
}

short set_obj_pages(const SETTINGS *s)
{
	return s->pages_obj;
}

short set_page_now(const SETTINGS *s)
{
	return (short)(s->cur - 1);
}

/* ---------------- the pages ---------------- */

/* page p (from 1): its objects shown, the others hidden (and not editable, so Tab skips them), the
   chosen name in a black bar, white words (never SELECTED: the 3D look draws that pressed in, 5.6) */
static void show_page(SETTINGS *s, short p, int draw)
{
	OBJECT *tree = s->b.tree;
	short i, n, ground, old = s->cur;
	s->cur = p;
	for (i = 1; i < s->b.nobj; i++) {
		if (!s->page[i])
			continue;
		if (s->page[i] == p) {
			tree[i].ob_flags &= ~HIDETREE;
			if (tree[i].ob_type == G_FTEXT || tree[i].ob_type == G_FBOXTEXT)
				tree[i].ob_flags |= EDITABLE;
		} else {
			tree[i].ob_flags |= HIDETREE;
			tree[i].ob_flags &= ~EDITABLE;
		}
	}
	for (i = 1; i <= s->npages; i++) {
		tree[s->tab[i]].ob_state = 0;
		((TEDINFO *)tree[s->tab[i]].ob_spec)->te_color = i == p ? 0x1071 : 0x1180;
	}
	if (draw) {
		/* the two names that changed (the bar's old place and new) and the page's area, each object by itself
		   (dlg_draw_one, dlg_draw_range): the AES's walk through a hundred objects cost an 8 MHz ST 35 ms a name
		   and most of the page's. The other names are as they were */
		if (old >= 1 && old <= s->npages && old != p) {
			dlg_draw_one(tree, s->tab[old]);
			evnt_timer_(0);
		}
		dlg_draw_one(tree, s->tab[p]);
		evnt_timer_(0);
		/* the page in turns of 20 ms, an event call between, so no stretch is long */
		for (n = s->pfirst[p], ground = 1; n < s->pfirst[p + 1]; ground = 0) {
			n = dlg_draw_range(tree, s->pages_obj, n, s->pfirst[p + 1], ground, SET_TURN_MS);
			if (n < s->pfirst[p + 1])
				evnt_timer_(0);
		}
		/* the area's 3 pixels below reach the frames of Cancel and OK (the ground filled over them): drawn again,
		   what shows of them in that piece, a turn of their own */
		evnt_timer_(0);
		dlg_draw_range(tree, s->pages_obj, s->cancel, (short)(s->ok + 1), 0, 0);
	}
}

void set_focus(SETTINGS *s, short field)
{
	if (field > 0 && field < s->b.nobj) {
		s->want = field;
		s->focus = 0;
	}
}

void set_show(SETTINGS *s, short n)
{
	if (n >= 0 && n < s->npages && n + 1 != s->cur)
		show_page(s, (short)(n + 1), 1);
}

/* the tree: the band, the list, the pages, Cancel and OK (the guidelines' 6.1) */
static void build(SETTINGS *s)
{
	const SET_DEF *d = s->d;
	DLGB *b = &s->b;
	short k, from;
	b->hc = b->hc ? b->hc : 16;
	db_begin(b, s->w, s->h);
	s->gap = (short)(b->hc / 2);
	s->rgap = (short)(b->hc * 3 / 8);
	s->npages = d->npages;
	s->adding = 0;
	db_band(b, s->w, s->band_text);
	/* the list: plain words on white, the chosen one in a black bar, a line between it and the settings */
	db_add(b, G_BOX, 0, 0, 0x00001171L, s->sw, 1, (short)(0 | (1 << 8)), (short)(s->h - 1));
	for (k = 0; k < d->npages; k++) {
		short n = (short)strlen(d->pages[k].name);
		if (n > 11)
			n = 11;
		memcpy(s->tabname[k], "  ", 2);
		memcpy(s->tabname[k] + 2, d->pages[k].name, (size_t)n);
		s->tabname[k][2 + n] = 0;
		s->tab[k + 1] = db_add(b, G_BOXTEXT, TOUCHEXIT, 0, (long)db_ted(b, s->tabname[k], 0, 0x1180),
				       (short)(0 | (2 << 8)), (short)((2 + k * 2) | ((k * s->gap) << 8)),
				       (short)((s->sw - 1) | (6 << 8)), 1);
	}
	db_keep(b, 2);			/* Cancel and OK always have room */
	s->pages_obj = db_add(b, G_IBOX, 0, 0, 0, (short)(s->sx - 1), 2, s->pw, (short)(s->h - 4));
	for (k = 1; k <= d->npages; k++) {
		from = b->nobj;
		s->pfirst[k] = from;
		s->adding = k;
		db_text(b, s->sx, 2, d->pages[k - 1].name);
		db_add(b, G_BOX, 0, 0, 0x00001171L, s->sx, (short)(3 | (2 << 8)), (short)(s->pw - 2), (short)(0 | (1 << 8)));
		s->row0 = 4;
		if (d->pages[k - 1].build)
			d->pages[k - 1].build(s, s->sx, 4, d->user);
		for (; from < b->nobj; from++)
			s->page[from] = (unsigned char)k;
	}
	s->adding = 0;
	s->pfirst[d->npages + 1] = b->nobj;
	db_keep(b, 0);
	s->cancel = db_button(b, (short)(s->w - 24), (short)(s->h - 2), 10, "Cancel", EXIT);
	s->ok = db_button(b, (short)(s->w - 12), (short)(s->h - 2), 10, "OK", EXIT | DEFAULT);
	db_end(b);
}

/* ---------------- the dialog ---------------- */

/* an object of the dialog drawn again, or all of it where the screen under a pop-up wasn't kept */
static void drawn_again(SETTINGS *s)
{
	if (settings_drawn)
		settings_drawn(s->b.tree, s->b.band);
}

/* ---------------- the keyboard's reach (the guidelines' 5.10, D25) ----------------
 * The focus is the text field that has the cursor, one control (a tick, a radio group, a pop-up or an action button)
 * with a ring round it, or the list of pages (the ring round the chosen name). Tab and Shift+Tab go through the page's
 * fields and controls in the order they were added, then the list, and round; Space ticks a tick, opens a pop-up
 * or does a button's work; the arrows change the choice of a radio group (Left, Right, Up, Down), the page in the
 * list (Up, Down) and go from control to control (Up, Down); in a text field every key is as it was. Return is
 * OK; Esc and Undo are Cancel. A dialog opens with the first text field as it always did, else the list. */

#define SET_STOPS 64		/* controls the keys visit on one page */
#define RING 2			/* the ring is this far outside its object, in pixels */

static SETTINGS *key_set;	/* the dialog whose keys settings_key takes */

static int is_field(const OBJECT *o)
{
	return (o->ob_type == G_FTEXT || o->ob_type == G_FBOXTEXT) && (o->ob_flags & EDITABLE);
}

/* the object the ring goes round: the control, a radio group's chosen button, the list's chosen name; 0 none */
static short ring_obj(const SETTINGS *s)
{
	if (s->focus < 0)
		return s->cur >= 1 && s->cur <= s->npages ? s->tab[s->cur] : 0;
	if (s->focus > 0) {
		short i;
		for (i = 0; i < s->nw; i++)
			if (s->wk[i].kind == W_RADIO && s->wk[i].obj == s->focus) {
				short c = *s->wk[i].sel;
				return (short)(s->focus + (c >= 0 && c < s->wk[i].n ? c : 0));
			}
		return s->focus;
	}
	return 0;
}

static void ring_draw(SETTINGS *s)
{
	short o = ring_obj(s), x, y;
	GRECT r, c;
	if (!o || !s->live || !s->kb)
		return;
	s->ringed = 1;
	objc_offset(s->b.tree, o, &x, &y);
	r.x = (short)(x - RING);
	r.y = (short)(y - RING);
	r.w = (short)(s->b.tree[o].ob_width + 2 * RING);
	r.h = (short)(s->b.tree[o].ob_height + 2 * RING);
	c = r;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return;
	graf_mouse(M_OFF, 0);
	clip_on(&c);
	frame(&r, 1);
	clip_off();
	graf_mouse(M_ON, 0);
}

/* the dialog's own drawing over its tree in a window: after any piece the AES asks for or we draw */
static void ring_more(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	if (key_set)
		ring_draw(key_set);
}

/* the ring taken away, if it is on the screen: the object and 3 pixels round it drawn again over the page's ground (the
   list's line too); the focus is 0 after it (the caller sets the next, so a redraw does not draw the ring again) */
static void ring_erase(SETTINGS *s)
{
	short o = ring_obj(s), list = s->focus < 0, was = s->ringed;
	s->focus = 0;
	s->ringed = 0;
	if (!was || !o || !s->live)
		return;
	dlg_draw_range(s->b.tree, o, o, (short)(o + 1), 1, 0);
	if (list)
		dlg_draw_one(s->b.tree, s->line_obj);
}

/* the page's controls the keys visit, in the order they were added: out[], their number */
static short stops(SETTINGS *s, short *out, short max)
{
	OBJECT *t = s->b.tree;
	short i, k, n = 0;
	for (i = s->pfirst[s->cur]; i < s->pfirst[s->cur + 1] && n < max; i++) {
		SET_W *w;
		if ((t[i].ob_flags & HIDETREE) || (t[i].ob_state & DISABLED))
			continue;
		if (is_field(&t[i])) {
			out[n++] = i;
			continue;
		}
		k = db_tick_hit(&s->b, i);
		if (k >= 0) {
			if (i == s->b.tick[k].box)
				out[n++] = i;
			continue;
		}
		w = by_obj(s, i);
		if (w && w->kind == W_RADIO) {
			for (k = 0; k < w->n && ((w->off >> k) & 1); k++)
				;
			if (k < w->n)
				out[n++] = i;
		} else if (w) {
			out[n++] = i;
		}
	}
	return n;
}

/* the focus goes to `to` (a control, a field, or -1 the list); *field tells dlg_do where the text cursor goes */
static void focus_to(SETTINGS *s, short to, short edit, short *field)
{
	if (s->ringed) {
		ring_erase(s);
		evnt_timer_(0);			/* an event call between the two draws: no stretch of 50 ms on an 8 MHz ST */
	}
	s->focus = 0;
	if (to > 0 && is_field(&s->b.tree[to])) {
		s->focus = 0;
		*field = to;
		return;
	}
	s->focus = to;
	if (edit > 0)
		*field = -1;
	ring_draw(s);
}

/* the focus the dialog starts a page with: its first text field (dlg_do puts the cursor there), else the list */
static void focus_default(SETTINGS *s)
{
	short k, n = stops(s, &k, 1);
	s->focus = 0;
	if (n && is_field(&s->b.tree[k]))
		return;
	{
		short i;
		for (i = s->pfirst[s->cur]; i < s->pfirst[s->cur + 1]; i++)
			if (is_field(&s->b.tree[i]) && !(s->b.tree[i].ob_flags & HIDETREE))
				return;
	}
	s->focus = -1;
	ring_draw(s);
}

static void choices_refresh(SETTINGS *s);
static void pop_run(SETTINGS *s, SET_W *p);
static void act_run(SETTINGS *s, SET_W *p);
static void radio_hit(SETTINGS *s, SET_W *p, short c, int by_key);

/* Space on the control that has the focus */
static void focus_act(SETTINGS *s)
{
	SET_W *w = by_obj(s, s->focus);
	short k = db_tick_hit(&s->b, s->focus);
	if (k >= 0) {
		db_tick_flip(&s->b, k);
		evnt_timer_(0);
		choices_refresh(s);
		ring_draw(s);
	} else if (w && w->kind == W_POPUP) {
		pop_run(s, w);
		choices_refresh(s);
		ring_draw(s);
	} else if (w && (w->kind == W_ACT || w->kind == W_VALUE)) {
		act_run(s, w);
		choices_refresh(s);
		ring_draw(s);
	}
}

/* the next (d 1) or previous (d -1) choice of the radio group that applies, chosen; 0 if there is none */
static int radio_step(SETTINGS *s, SET_W *w, short d)
{
	short c = *w->sel;
	short k;
	for (k = 0; k < w->n; k++) {
		c = (short)(c + d);
		if (c < 0 || c >= w->n)
			return 0;		/* the ends: no wrapping, as a list */
		if (!((w->off >> c) & 1)) {
			radio_hit(s, w, c, 1);
			return 1;
		}
	}
	return 0;
}

/* the page list's Up and Down */
static void page_key(SETTINGS *s, short d)
{
	short np = (short)(s->cur + d);
	if (np < 1 || np > s->npages)
		return;
	if (s->ringed) {
		ring_erase(s);
		evnt_timer_(0);
	}
	s->focus = 0;
	show_page(s, np, 1);
	s->focus = -1;
	ring_draw(s);
}

/* a radio button chosen (by a click or an arrow key): the variable, the buttons, the ring */
static void radio_hit(SETTINGS *s, SET_W *p, short c, int by_key)
{
	short old = *p->sel;
	if (c < 0 || c >= p->n || ((p->off >> c) & 1) || c == old)
		return;
	*p->sel = c;
	if (!by_key) {
		radio_sync(s, p, 1);
		return;
	}
	radio_sync(s, p, 0);
	if (s->live) {
		short ob = (short)(p->obj + (old >= 0 && old < p->n ? old : 0));
		dlg_draw_one(s->b.tree, (short)(p->obj + c));
		evnt_timer_(0);
		dlg_draw_range(s->b.tree, ob, ob, (short)(ob + 1), 1, 0);	/* the old one, and the ring round it gone */
		ring_draw(s);
	}
}

/* a tick box drawn again (db_tick_flip's): alone over the ground where the look is flat, else through the AES */
static void set_tick_draw(OBJECT *tree, short box)
{
	if (tree[0].ob_flags & 0x0600)		/* FL3DMASK: the AES's 3D look */
		dlg_draw(tree, box);
	else
		dlg_draw_range(tree, box, box, (short)(box + 1), 1, 0);
}

/* a click: the keyboard's focus is no longer where it was (a control's ring goes; the list's when a page is chosen) */
static void mouse_focus(SETTINGS *s, int page_change)
{
	if (s->focus > 0 || (s->focus < 0 && page_change) || s->ringed)
		ring_erase(s);
	s->kb = 0;
}

/* a key offered by dlg_do before its own: 1 when taken */
static int settings_key(OBJECT *tree, short key, short kstate, short edit, short *field)
{
	SETTINGS *s = key_set;
	short scan = (short)((key >> 8) & 0xff), ch = (short)(key & 0xff), d = 0, st[SET_STOPS], n, i, cur;
	if (!s || !s->live || tree != s->b.tree)
		return 0;
	if (scan == 0x0f)			/* Tab (Shift+Tab backwards) goes through every field and control and the list, and round */
		d = (kstate & 3) ? -1 : 1;
	if (d) {
		s->kb = 1;
		n = stops(s, st, SET_STOPS);
		{
			short at = edit > 0 ? edit : s->focus;	/* a field, a control, or the list (-1) */
			for (cur = 0; cur < n && st[cur] != at; cur++)
				;			/* n when it isn't one of them: the list */
		}
		cur = (short)((cur + d + n + 1) % (n + 1));
		focus_to(s, cur == n ? -1 : st[cur], edit, field);
		return 1;
	}
	if (edit > 0)
		return 0;				/* in a text field: its keys as they were */
	if (scan == 0x39 && ch == ' ') {		/* Space */
		s->kb = 1;
		if (s->focus > 0) {
			ring_draw(s);		/* shown now, if it was not */
			focus_act(s);
		}
		return 1;
	}
	if (scan == 0x48 || scan == 0x50 || scan == 0x4b || scan == 0x4d) {
		short up = (short)(scan == 0x48 || scan == 0x4b ? -1 : 1);
		SET_W *w = s->focus > 0 ? by_obj(s, s->focus) : 0;
		s->kb = 1;
		if (s->focus <= 0 && (scan == 0x48 || scan == 0x50)) {
			page_key(s, up);		/* the list, or nothing in focus: the page */
		} else if (w && w->kind == W_RADIO) {
			radio_step(s, w, up);
		} else if (scan == 0x48 || scan == 0x50) {
			n = stops(s, st, SET_STOPS);
			for (i = 0; i < n && st[i] != s->focus; i++)
				;
			cur = (short)((i + up + n + 1) % (n + 1));
			focus_to(s, cur == n ? -1 : st[cur], edit, field);
		}
		return 1;
	}
	return 0;
}

static void pop_run(SETTINGS *s, SET_W *p)
{
	char name[SET_POPMAX][SET_NAME + 4];
	const char *lab[SET_POPMAX];
	short i, k, x, y, r, sel = *p->sel < 0 || *p->sel >= p->n ? 0 : *p->sel;
	OBJECT *tree = s->b.tree;
	for (i = 0; i < p->n; i++) {
		short n = (short)strlen(p->ch[i]);
		if (n > SET_NAME)
			n = SET_NAME;
		/* popup_m's labels: the words, the ticked one with the check mark in front (popup.h); a greyed one (D24) is "~" and
		   the words, and not ticked */
		k = 0;
		if ((p->off >> i) & 1)
			name[i][k++] = '~';
		else if (i == *p->sel)
			name[i][k++] = '\x08';
		memcpy(name[i] + k, p->ch[i], (size_t)n);
		name[i][k + n] = 0;
		lab[i] = name[i];
	}
	objc_offset(tree, p->obj, &x, &y);
	r = popup_over(x, y, lab, p->n, sel);
	tree[p->obj].ob_state &= ~SELECTED;
	if (!popup_put_back && settings_uncover)
		settings_uncover(popup_area.x, popup_area.y, popup_area.w, popup_area.h);
	if (r >= 0 && r < p->n) {
		*p->sel = r;
		pop_label(p);
	}
	if (dlg_in_window() || popup_put_back) {
		dlg_draw(tree, p->obj);		/* the screen under the pop-up is back, or the AES draws it */
	} else {
		dlg_draw(tree, 0);		/* it wasn't kept (no memory): the dialog again */
		drawn_again(s);
	}
	ring_draw(s);
}

/* a button's work: the app's dialog or list over ours, then ours again over what it covered */
static void act_run(SETTINGS *s, SET_W *p)
{
	OBJECT *tree = s->b.tree;
	short x, y, w, h;
	tree[p->obj].ob_state &= ~SELECTED;
	if (p->act)
		p->act(s, p->user);
	tree[p->obj].ob_state &= ~SELECTED;
	if (!dlg_in_window() && settings_uncover) {
		/* what the app's file selector or list covered, whatever its size: the app's windows over the whole
		   desktop (EMail's ui_redraws), then ours on top */
		wind_get(0, WF_WORKXYWH, &x, &y, &w, &h);
		settings_uncover(x, y, w, h);
	}
	dlg_draw(tree, 0);
	drawn_again(s);
	ring_draw(s);
}

static SET_W *widget_of(SETTINGS *s, short obj)
{
	short i;
	for (i = 0; i < s->nw; i++)
		if (s->wk[i].obj == obj || (s->wk[i].kind == W_RADIO && obj > s->wk[i].obj && obj < s->wk[i].obj + s->wk[i].n))
			return &s->wk[i];
	return 0;
}

/* the widget whose (first) object is obj */
static SET_W *by_obj(SETTINGS *s, short obj)
{
	short i;
	for (i = 0; i < s->nw; i++)
		if (s->wk[i].obj == obj)
			return &s->wk[i];
	return 0;
}

/* a radio group's buttons as its variable and its greyed choices say; drawn where one changed */
static void radio_sync(SETTINGS *s, SET_W *p, int draw)
{
	short k;
	for (k = 0; k < p->n; k++) {
		OBJECT *o = &s->b.tree[p->obj + k];
		unsigned short st = (unsigned short)((k == *p->sel ? SELECTED : 0) | (((p->off >> k) & 1) ? DISABLED : 0));
		if (o->ob_state != st) {
			o->ob_state = st;
			if (draw && s->live && !(o->ob_flags & HIDETREE))
				dlg_draw_one(s->b.tree, (short)(p->obj + k));
		}
	}
}

/* the choices the app's functions say apply, after every change the user makes */
static void choices_refresh(SETTINGS *s)
{
	short i, c;
	for (i = 0; i < s->nw; i++) {
		SET_W *p = &s->wk[i];
		unsigned short off = 0;
		if (!p->when)
			continue;
		for (c = 0; c < p->n; c++)
			if (!p->when(s, p->obj, c, p->wuser))
				off |= (unsigned short)(1u << c);
		p->off = off;
		if (p->kind == W_RADIO)
			radio_sync(s, p, 1);
	}
}

/* Cancel: everything as it was when the dialog opened */
static void revert(SETTINGS *s)
{
	short i;
	char *q = s->pool;
	for (i = 0; i < s->b.nticks; i++)
		*s->b.tick[i].var = s->b.tick[i].was;
	for (i = 0; i < s->nw; i++)
		if (s->wk[i].kind == W_POPUP || s->wk[i].kind == W_RADIO)
			*s->wk[i].sel = s->wk[i].was;
	for (i = 0; i < s->nf; i++) {
		memcpy(s->fl[i].buf, q, (size_t)s->fl[i].cap + 1);
		q += s->fl[i].cap + 1;
	}
}

int settings_dialog(const SET_DEF *d)
{
	SETTINGS S, *s = &S;
	short omax = d->max_objects ? d->max_objects : SET_OBJECTS, k, x, y, w, h, r, nt;
	long pool = 0;
	int ok = 0;
	char *q;
	if (d->npages < 1 || d->npages > SET_MAXPAGES)
		return 0;
	memset(s, 0, sizeof(S));
	s->d = d;
	s->sw = d->list_w ? d->list_w : SET_SW;
	s->sx = (short)(s->sw + 2);
	s->pw = d->page_w ? d->page_w : SET_PW;
	s->w = (short)(s->sx + s->pw);
	s->h = d->height >= 8 ? d->height : 17;
	nt = (short)(omax / 4 + 8);
	if (!db_alloc(&s->b, omax, nt, 32, 12,
		      (long)omax + (long)SET_WMAX * sizeof(SET_W) + (long)SET_FMAX * sizeof(SET_F))) {
		alert(1, "[1][Not enough memory for|the settings.][ OK ]");
		return 0;
	}
	s->page = (unsigned char *)s->b.extra;
	s->wk = (SET_W *)(((long)s->page + omax + 7) & ~7L);
	s->fl = (SET_F *)((char *)s->wk + (long)SET_WMAX * sizeof(SET_W));
	s->band_text[0] = ' ';
	strncpy(s->band_text + 1, d->title ? d->title : "Settings", sizeof(s->band_text) - 2);
	s->b.draw = set_tick_draw;
	build(s);
	choices_refresh(s);
	/* what Cancel puts back */
	for (k = 0; k < s->nf; k++)
		pool += s->fl[k].cap + 1;
	s->pool = malloc((size_t)pool + 1);
	if (!s->pool) {
		db_free(&s->b);
		alert(1, "[1][Not enough memory for|the settings.][ OK ]");
		return 0;
	}
	for (q = s->pool, k = 0; k < s->nf; k++) {
		memcpy(q, s->fl[k].buf, (size_t)s->fl[k].cap + 1);
		q += s->fl[k].cap + 1;
	}
	show_page(s, (short)(d->first >= 0 && d->first < d->npages ? d->first + 1 : 1), 0);

	dlg_name(d->title ? d->title : "Settings");
	dlg_band(s->b.band);		/* in a window its title says it (3.5) */
	dlg_open(s->b.tree, &x, &y, &w, &h);
	drawn_again(s);
	{
		int (*was_keys)(OBJECT *, short, short, short, short *) = dlg_keys;
		SETTINGS *was_set = key_set;
		s->line_obj = 2;			/* the list's line: the band is 1 */
		s->live = 1;
		key_set = s;
		dlg_keys = settings_key;
		dlg_more(ring_more);
		focus_default(s);
		for (;;) {
			SET_W *p;
			{
				short start = s->want > 0 ? s->want : s->focus ? -1 : 0;
				s->want = 0;
				r = dlg_do(s->b.tree, start, s->cancel) & 0x7fff;
			}
			k = db_tick_hit(&s->b, r);
			if (k >= 0) {
				mouse_focus(s, 0);
				if (!(s->b.tree[s->b.tick[k].box].ob_state & DISABLED)) {
					db_tick_flip(&s->b, k);
					evnt_timer_(120);
					choices_refresh(s);
				}
				focus_default(s);
				continue;
			}
			for (k = 1; k <= s->npages; k++)
				if (r == s->tab[k])
					break;
			if (k <= s->npages) {
				mouse_focus(s, 1);
				if (k != s->cur)
					show_page(s, k, 1);
				focus_default(s);
				continue;
			}
			p = widget_of(s, r);
			if (p) {
				mouse_focus(s, 0);
				if (p->kind == W_RADIO)
					radio_hit(s, p, (short)(r - p->obj), 0);
				else if (p->kind == W_POPUP)
					pop_run(s, p);
				else
					act_run(s, p);
				choices_refresh(s);
				focus_default(s);
				continue;
			}
			if (r == s->ok) {
				if (d->check && !d->check(s, d->user)) {
					s->b.tree[r].ob_state &= ~SELECTED;
					dlg_draw(s->b.tree, r);
					continue;
				}
				ok = 1;
				break;
			}
			if (r == s->cancel)
				break;
			/* anything else (an object that found no room, the root): nothing to do */
			s->b.tree[r].ob_state &= ~SELECTED;
		}
		s->live = 0;
		key_set = was_set;
		dlg_keys = was_keys;
	}
	dlg_close();
	if (!ok)
		revert(s);
	free(s->pool);
	db_free(&s->b);
	return ok;
}

int settings_run(const char *title, const SET_PAGE *pages, short npages, short page_width, short height,
		 void *user)
{
	SET_DEF d;
	memset(&d, 0, sizeof(d));
	d.title = title;
	d.pages = pages;
	d.npages = npages;
	d.page_w = page_width;
	d.height = height;
	d.user = user;
	return settings_dialog(&d);
}
