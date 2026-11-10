/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dlgbuild.c - the dialog builder of EMail's dialogs.c (d_begin, d_add,
 * d_text, d_edit, d_long, d_framed, d_tick, d_button, d_band, d_end), lifted
 * into common (dlgbuild.h; Erez, 9 Nov 2026). The objects it makes are the
 * ones EMail 2.14 made, so a dialog built here is pixel for pixel EMail's.
 */
#include <stdlib.h>
#include <string.h>
#include "dlgbuild.h"
#include "dialog.h"

/* a field's template and valid characters: every field's are the same
   characters, so one string of each serves all, from its end (3 KB less
   than a pair a field, EMail 2.12) */
#define S10(c) c c c c c c c c c c
#define S80(c) S10(c) S10(c) S10(c) S10(c) S10(c) S10(c) S10(c) S10(c)
#define FMAX 80
static const char t_line[] = S80("_"), v_any[] = S80("X"), v_digit[] = S80("9");

void db_init(DLGB *b, OBJECT *tree, short omax, TEDINFO *ted, short tmax, DB_TICK *tick, short kmax,
	     LFIELD *lf, short lmax)
{
	memset(b, 0, sizeof(*b));
	b->tree = tree;
	b->omax = omax;
	b->ted = ted;
	b->tmax = tmax;
	b->tick = tick;
	b->kmax = kmax;
	b->lf = lf;
	b->lmax = lmax;
}

/* a size rounded up to 8 bytes, so each array starts where any of them may */
#define UP8(n) (((long)(n) + 7) & ~7L)

int db_alloc(DLGB *b, short omax, short tmax, short kmax, short lmax, long extra)
{
	long so = UP8((long)omax * sizeof(OBJECT)), st = UP8((long)tmax * sizeof(TEDINFO)),
	     sk = UP8((long)kmax * sizeof(DB_TICK)), sl = UP8((long)lmax * sizeof(LFIELD)), se = UP8(extra);
	char *m = malloc((size_t)(so + st + sk + sl + se + 8));
	if (!m)
		return 0;
	memset(m, 0, (size_t)(so + st + sk + sl + se));
	db_init(b, (OBJECT *)m, omax, (TEDINFO *)(m + so), tmax, (DB_TICK *)(m + so + st), kmax,
		(LFIELD *)(m + so + st + sk), lmax);
	b->mem = m;
	b->extra = extra ? m + so + st + sk + sl : 0;
	return 1;
}

void db_free(DLGB *b)
{
	if (b->tree)
		lf_forget(b->tree);
	free(b->mem);
	b->mem = 0;
	b->tree = 0;
}

void db_begin(DLGB *b, short w, short h)
{
	short d;
	OBJECT *t = b->tree;
	lf_forget(t);
	graf_handle(&b->wc, &b->hc, &d, &d);
	memset(t, 0, (size_t)b->omax * sizeof(OBJECT));
	b->nobj = 1;
	b->nted = b->nticks = b->nlf = 0;
	b->last = 0;
	b->band = 0;
	b->lost = 0;
	b->keep = 0;
	t[0].ob_next = -1;
	t[0].ob_head = t[0].ob_tail = -1;
	t[0].ob_type = G_BOX;
	t[0].ob_state = OUTLINED;
	t[0].ob_spec = 0x00021100L;
	t[0].ob_width = w;
	t[0].ob_height = h;
}

void db_keep(DLGB *b, short n)
{
	b->keep = n;
}

int db_spare(const DLGB *b, short i)
{
	return i >= b->omax - 1;
}

short db_add(DLGB *b, short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i;
	OBJECT *o;
	if (b->nobj >= b->omax - 1 - b->keep) {
		/* full: the spare, not in the tree */
		b->lost++;
		i = b->omax - 1;
		o = &b->tree[i];
		o->ob_next = 0;
		o->ob_head = o->ob_tail = -1;
		o->ob_flags = 0;
	} else {
		i = b->nobj++;
		o = &b->tree[i];
		o->ob_next = 0;
		o->ob_head = o->ob_tail = -1;
		if (b->last)
			b->tree[b->last].ob_next = i;
		else
			b->tree[0].ob_head = i;
		b->tree[0].ob_tail = i;
		b->last = i;
	}
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	return i;
}

short db_text(DLGB *b, short x, short y, const char *s)
{
	return db_add(b, G_STRING, 0, 0, (long)s, x, y, (short)strlen(s), 1);
}

TEDINFO *db_ted(DLGB *b, const char *text, short just, short color)
{
	TEDINFO *t;
	if (b->nted >= b->tmax - 1) {
		b->lost++;
		t = &b->ted[b->tmax - 1];	/* the spare, shared */
	} else {
		t = &b->ted[b->nted++];
	}
	t->te_ptext = (char *)text;
	t->te_ptmplt = (char *)"";
	t->te_pvalid = (char *)"";
	t->te_font = 3;
	t->te_just = just;
	t->te_color = color;
	t->te_thickness = 0;
	t->te_txtlen = (short)strlen(text) + 1;
	t->te_tmplen = 1;
	return t;
}

short db_edit(DLGB *b, short x, short y, char *buf, short len, char kind)
{
	TEDINFO *t = db_ted(b, buf, 0, 0x1180);
	if (len > FMAX)
		len = FMAX;
	buf[len] = 0;
	t->te_ptmplt = (char *)t_line + (FMAX - len);
	t->te_pvalid = (char *)(kind == '9' ? v_digit : v_any) + (FMAX - len);
	t->te_txtlen = len + 1;
	t->te_tmplen = len + 1;
	return db_add(b, G_FTEXT, EDITABLE, 0, (long)t, x, y, len, 1);
}

short db_framed(DLGB *b, short i)
{
	TEDINFO *t = (TEDINFO *)b->tree[i].ob_spec;
	b->tree[i].ob_type = G_FBOXTEXT;
	t->te_thickness = -1;
	t->te_color = 0x1180;
	return i;
}

short db_field(DLGB *b, short x, short y, char *buf, short len, char kind)
{
	return db_framed(b, db_edit(b, x, y, buf, len, kind));
}

short db_long(DLGB *b, short x, short y, char *text, short len, short cap, char mask)
{
	LFIELD *f;
	static char spare[FMAX + 1];
	short obj;
	if (b->nlf >= b->lmax) {
		/* no room for its keys: an ordinary field on a scratch text, so that nothing is lost but the text */
		b->lost++;
		return db_edit(b, x, y, spare, len, 'X');
	}
	f = &b->lf[b->nlf++];
	obj = db_edit(b, x, y, f->view, len, 'X');
	lf_init(f, b->tree, obj, text, cap, mask);
	return obj;
}

short db_button(DLGB *b, short x, short y, short w, const char *label, short flags)
{
	return db_add(b, G_BUTTON, SELECTABLE | flags, 0, (long)label, x, y, w, 1);
}

short db_band(DLGB *b, short w, const char *title)
{
	b->band = db_add(b, G_BOXTEXT, 0, 0, (long)db_ted(b, title, 0, 0x1071), 0, 0, w, 1);
	return b->band;
}

/* ---------------- the tick box ---------------- */

/* a tick box, 16 pixels square, and the tick in it (EMail's) */
static const char *const tick_art[16] = {
	"................",
	".##############.",
	".#............#.",
	".#..........#.#.",
	".#.........##.#.",
	".#........##..#.",
	".#.......##...#.",
	".#.##...##....#.",
	".#.###.##.....#.",
	".#..####......#.",
	".#...##.......#.",
	".#............#.",
	".#............#.",
	".#............#.",
	".##############.",
	"................",
};
static unsigned short tick_on[16], tick_off[16];
BITBLK db_tick_on, db_tick_off;
static short tick_rows;

static void make_ticks(short hc)
{
	/* half height on 8-pixel screens: every other line, and the frame */
	short tall = hc >= 16, rows = tall ? 16 : 8, r, c;
	for (r = 0; r < rows; r++) {
		const char *src = tick_art[tall ? r : (r == rows - 1 ? 14 : r * 2 + (r ? 0 : 1))];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (unsigned short)((v << 1) | (src[c] == '#'));
		tick_on[r] = v;
		tick_off[r] = tall ? (r == 1 || r == 14 ? v : (r == 0 || r == 15 ? 0 : 0x4002))
				   : (r == 0 || r == rows - 1 ? v : 0x4002);
	}
	db_tick_on.bi_pdata = tick_on;
	db_tick_off.bi_pdata = tick_off;
	db_tick_on.bi_wb = db_tick_off.bi_wb = 2;
	db_tick_on.bi_hl = db_tick_off.bi_hl = rows;
	db_tick_on.bi_x = db_tick_off.bi_x = db_tick_on.bi_y = db_tick_off.bi_y = 0;
	db_tick_on.bi_color = db_tick_off.bi_color = 1;
	tick_rows = rows;
}

/* the tick box's two bitmaps (db_tick_on, db_tick_off) made for a cell height
   hc (8 or 16), for an app that draws its own objects (EContact's fields) */
void db_tick_bits(short hc)
{
	if (tick_rows != (hc >= 16 ? 16 : 8))
		make_ticks(hc);
}

short db_tick(DLGB *b, short x, short y, const char *label, short *var)
{
	short k, lbl;
	DB_TICK *t;
	if (!b->hc)
		b->hc = 16;
	if (tick_rows != (b->hc >= 16 ? 16 : 8))
		make_ticks(b->hc);
	if (b->nticks >= b->kmax) {
		b->lost++;
		return -1;
	}
	k = b->nticks++;
	t = &b->tick[k];
	t->var = var;
	t->was = *var;
	t->group = 0;
	t->box = db_add(b, G_IMAGE, TOUCHEXIT, 0, (long)(*var ? &db_tick_on : &db_tick_off), x, y, 2, 1);
	lbl = db_add(b, G_STRING, TOUCHEXIT, 0, (long)label, x + 3, y, (short)strlen(label), 1);
	if (db_spare(b, t->box) || db_spare(b, lbl)) {
		/* it didn't fit: not a tick the loop may find (the spare isn't in the tree); a box that did fit
		   alone is made inert */
		if (!db_spare(b, t->box))
			b->tree[t->box].ob_flags &= ~TOUCHEXIT;
		b->nticks--;
		return -1;
	}
	return k;
}

short db_tick_hit(const DLGB *b, short obj)
{
	short k;
	if (obj <= 0)
		return -1;
	for (k = 0; k < b->nticks; k++)
		if (obj == b->tick[k].box || obj == b->tick[k].box + 1)
			return k;
	return -1;
}

static void tick_draw(DLGB *b, short k)
{
	b->tree[b->tick[k].box].ob_spec = (long)(*b->tick[k].var ? &db_tick_on : &db_tick_off);
	if (b->draw)
		b->draw(b->tree, b->tick[k].box);
	else
		dlg_draw(b->tree, b->tick[k].box);
}

void db_tick_flip(DLGB *b, short k)
{
	DB_TICK *t;
	short j;
	if (k < 0 || k >= b->nticks)
		return;
	t = &b->tick[k];
	if (b->tree[t->box].ob_state & DISABLED)
		return;
	if (t->group) {
		if (*t->var)
			return;			/* the chosen one stays */
		for (j = 0; j < b->nticks; j++)
			if (b->tick[j].group == t->group && *b->tick[j].var) {
				*b->tick[j].var = 0;
				tick_draw(b, j);
			}
		*t->var = 1;
	} else {
		*t->var = !*t->var;
	}
	tick_draw(b, k);
}

void db_tick_group(DLGB *b, short k, short group)
{
	if (k >= 0 && k < b->nticks)
		b->tick[k].group = group;
}

void db_tick_enable(DLGB *b, short k, int on)
{
	short i;
	if (k < 0 || k >= b->nticks)
		return;
	for (i = 0; i < 2; i++) {
		OBJECT *o = &b->tree[b->tick[k].box + i];
		if (on)
			o->ob_state &= ~DISABLED;
		else
			o->ob_state |= DISABLED;
	}
}

void db_end(DLGB *b)
{
	OBJECT *t = b->tree;
	if (b->last) {
		t[b->last].ob_next = 0;		/* the last child's next is its parent */
		t[b->nobj - 1].ob_flags |= LASTOB;
	} else {
		t[0].ob_flags |= LASTOB;
	}
	rsrc_obfix_all(t, b->nobj);
	dlg_3d(t);			/* the AES's 3D look where it has one (5.6) */
}
