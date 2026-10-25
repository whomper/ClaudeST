/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialog.c - form_do with Esc and Undo as Cancel (see dialog.h).
 *
 * The loop is the AES's own form_do (GEM 2.x fm_do, as the Atari
 * Compendium and the Profibuch write it out): evnt_multi for a key or a
 * click, form_keybd for Return, Tab and the arrows, objc_edit for the
 * rest of the keys, form_button for clicks. Esc and Undo are taken out
 * before form_keybd and objc_edit see them.
 */
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "dialog.h"
#include "field.h"

/* the fields that hold more than they show (field.c), in an app that
   has field.c; weak, so an app without it needn't build it */
LFIELD *lf_of(OBJECT *tree, short obj) __attribute__((weak));
void lf_enter(LFIELD *f, short *idx) __attribute__((weak));
int lf_key(LFIELD *f, short key, short ch, short kstate, short *idx) __attribute__((weak));

#define BEG_MCTRL 3
#define END_MCTRL 2

int dlg_esc(short key)
{
	short scan = (key >> 8) & 0xff;
	return scan == 0x01 || scan == 0x61 || (key & 0xff) == 0x1b;
}

/* the first editable field, as form_do finds it; 0 when there is none */
static short first_field(OBJECT *tree)
{
	short i;
	for (i = 0;; i++) {
		if ((tree[i].ob_flags & EDITABLE) && !(tree[i].ob_flags & HIDETREE) &&
		    !(tree[i].ob_state & DISABLED))
			return i;
		if (tree[i].ob_flags & LASTOB)
			return 0;
	}
}

/* keys typed before the dialog came don't answer it (form_do does the
   same) */
static void flush_keys(void)
{
	short m[8];
	AEVENT e;
	do
		evnt_multi_(MU_KEYBD | MU_TIMER, 0, 0, 0, 0, m, &e);
	while (e.which & MU_KEYBD);
}

/* an object drawn again as it is now, through the tree from its root */
static void redraw(OBJECT *tree, short obj)
{
	short x, y;
	objc_offset(tree, obj, &x, &y);
	objc_draw(tree, 0, 8, x - 3, y - 3, tree[obj].ob_width + 6, tree[obj].ob_height + 6);
}

/* ---------------- a dialog in a window (a multitasking AES) ----------------
 * Where the AES runs several programs at once (MiNT with XaAES, N.AES or
 * MyAES; MagiC), a dialog is a window of the AES's (Erez, 25 Oct 2026):
 * the AES draws its frame, other programs go on running and drawing,
 * and it can be moved. On TOS and EmuTOS it is drawn over the screen as
 * before. */

const char *dlg_title = "";
static const char *next_name;		/* dlg_name's, for the next dlg_open */
static short next_band;			/* dlg_band's, for the next dlg_open */
void (*dlg_redraw_hook)(const short *msg);
int dlg_window_on = -1;

#define DLG_KIND (NAME | CLOSER | MOVER)
#define DLG_NEST 4

static struct {
	OBJECT *tree;			/* 0: drawn by dlg_draw_fn */
	void (*draw)(short x, short y, short w, short h);
	short h;			/* the window; 0 on TOS */
	short x, y, w, h_;		/* form_dial's space on TOS */
	unsigned short state;		/* the root's, as it was */
	long spec;
	short band, band_h;		/* dlg_band's object, hidden, and its height */
} dw[DLG_NEST];
static short ndw;

/* the app's own windows to draw again, asked while a dialog window was
   open and the app has no dlg_redraw_hook: each window's area, given back
   as one WM_REDRAW when the last dialog closes */
#define DLG_LATER 8
static struct {
	short h, x1, y1, x2, y2;
} later[DLG_LATER];
static short nlater;

static void keep_redraw(const short *msg)
{
	short k, x2 = msg[4] + msg[6], y2 = msg[5] + msg[7];
	for (k = 0; k < nlater && later[k].h != msg[3]; k++)
		;
	if (k == nlater) {
		if (nlater == DLG_LATER)
			k = DLG_LATER - 1;	/* more windows than that: the last grows */
		else
			nlater++;
		later[k].h = msg[3];
		later[k].x1 = msg[4], later[k].y1 = msg[5], later[k].x2 = x2, later[k].y2 = y2;
		return;
	}
	if (msg[4] < later[k].x1)
		later[k].x1 = msg[4];
	if (msg[5] < later[k].y1)
		later[k].y1 = msg[5];
	if (x2 > later[k].x2)
		later[k].x2 = x2;
	if (y2 > later[k].y2)
		later[k].y2 = y2;
}

static void give_back_redraws(void)
{
	short k, m[8];
	for (k = 0; k < nlater; k++) {
		m[0] = WM_REDRAW;
		m[1] = appl_id();
		m[2] = 0;
		m[3] = later[k].h;
		m[4] = later[k].x1;
		m[5] = later[k].y1;
		m[6] = later[k].x2 - later[k].x1;
		m[7] = later[k].y2 - later[k].y1;
		appl_write(m[1], 16, m);
	}
	nlater = 0;
}

int dlg_windowed(void)
{
	static int magic = -1;
	if (dlg_window_on >= 0)
		return dlg_window_on;
	if (magic < 0)
		magic = get_cookie(0x4D616758L) != 0;	/* MagX */
	return aes_count() != 1 || magic;
}

int dlg_in_window(void)
{
	return ndw > 0 && dw[ndw - 1].h > 0;
}

static int clip(short *x, short *y, short *w, short *h, short cx, short cy, short cw, short ch)
{
	short x2 = *x + *w < cx + cw ? *x + *w : cx + cw;
	short y2 = *y + *h < cy + ch ? *y + *h : cy + ch;
	if (*x < cx)
		*x = cx;
	if (*y < cy)
		*y = cy;
	*w = x2 - *x;
	*h = y2 - *y;
	return *w > 0 && *h > 0;
}

/* while a piece of a dialog window is drawn: that piece, which the app's
   own drawing keeps within (dlg_cut) */
static short lim[4];
static int lim_on;

/* the dialog's window, the part (x, y, w, h) of it drawn again through
   the AES's list of what shows of it: the tree, then what the app draws
   over it */
static void win_paint(short i, short x, short y, short w, short h)
{
	short rx, ry, rw, rh;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(dw[i].h, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
	while (rw > 0 && rh > 0) {
		if (clip(&rx, &ry, &rw, &rh, x, y, w, h)) {
			lim[0] = rx, lim[1] = ry, lim[2] = rw, lim[3] = rh;
			lim_on = 1;
			if (dw[i].tree)
				objc_draw(dw[i].tree, 0, 8, rx, ry, rw, rh);
			if (dw[i].draw)
				dw[i].draw(rx, ry, rw, rh);
			lim_on = 0;
		}
		wind_get(dw[i].h, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

int dlg_cut(short *x, short *y, short *w, short *h)
{
	short top, d, i = ndw - 1;
	if (lim_on)
		return clip(x, y, w, h, lim[0], lim[1], lim[2], lim[3]);
	if (i < 0 || dw[i].h <= 0)
		return 1;
	wind_get(0, WF_TOP, &top, &d, &d, &d);
	if (top == dw[i].h)
		return 1;		/* in front: all of it shows */
	{
		/* behind another program's window: the AES is asked to have it
		   drawn where it shows, by the next dlg_evnt */
		short m[8];
		m[0] = WM_REDRAW;
		m[1] = appl_id();
		m[2] = 0;
		m[3] = dw[i].h;
		m[4] = *x, m[5] = *y, m[6] = *w, m[7] = *h;
		appl_write(m[1], 16, m);
	}
	return 0;
}

void dlg_more(void (*draw)(short x, short y, short w, short h))
{
	if (ndw > 0)
		dw[ndw - 1].draw = draw;
}

/* the band's text, the window's name where the app gave none */
static const char *band_text(OBJECT *tree, short obj)
{
	short t;
	if (obj <= 0)
		return 0;
	t = tree[obj].ob_type & 0xff;
	if (t == G_STRING || t == G_BUTTON || t == G_TITLE)
		return (const char *)tree[obj].ob_spec;
	if (t == G_TEXT || t == G_BOXTEXT || t == G_FTEXT || t == G_FBOXTEXT)
		return ((TEDINFO *)tree[obj].ob_spec)->te_ptext;
	return 0;
}

/* the dialog's own title band out of the way in a window: hidden, what
   was below it moved up by its height, the box as much shorter; and back */
static void band_move(OBJECT *tree, short obj, short d)
{
	short c;
	for (c = tree[0].ob_head; c > 0; c = tree[c].ob_next) {
		if (c != obj && tree[c].ob_y > tree[obj].ob_y)
			tree[c].ob_y -= d;
		if (tree[c].ob_next == 0)
			break;			/* back at the root */
	}
	tree[0].ob_height -= d;
}

static void band_hide(OBJECT *tree, short i, short obj)
{
	dw[i].band = obj;
	dw[i].band_h = tree[obj].ob_height;
	tree[obj].ob_flags |= HIDETREE;
	band_move(tree, obj, dw[i].band_h);
}

static void band_show(OBJECT *tree, short i)
{
	band_move(tree, dw[i].band, -dw[i].band_h);
	tree[dw[i].band].ob_flags &= ~HIDETREE;
	dw[i].band = 0;
}

static short open_any(OBJECT *tree, void (*draw)(short, short, short, short),
		      short *x, short *y, short *w, short *h)
{
	short i = ndw, dx, dy, dwid, dh, bx, by, bw, bh;
	if (i == DLG_NEST)
		i = DLG_NEST - 1;	/* deeper than any app goes: the last one is reused */
	dw[i].tree = tree;
	dw[i].draw = draw;
	dw[i].h = 0;
	if (tree) {
		form_center(tree, x, y, w, h);
		dw[i].state = tree[0].ob_state;
		dw[i].spec = tree[0].ob_spec;
	} else {
		wind_get(0, WF_WORKXYWH, &dx, &dy, &dwid, &dh);
		*x = dx + (dwid - *w) / 2;
		*y = dy + (dh - *h) / 2;
	}
	dw[i].band = 0;
	if (dlg_windowed() && tree && next_band > 0)
		band_hide(tree, i, next_band);
	if (dlg_windowed()) {
		short wx = tree ? tree[0].ob_x : *x, wy = tree ? tree[0].ob_y : *y;
		short ww = tree ? tree[0].ob_width : *w, wh = tree ? tree[0].ob_height : *h;
		wind_get(0, WF_WORKXYWH, &dx, &dy, &dwid, &dh);
		wind_calc(WC_BORDER, DLG_KIND, wx, wy, ww, wh, &bx, &by, &bw, &bh);
		if (by < dy) {			/* below the menu bar */
			wy += dy - by;
			by = dy;
		}
		dw[i].h = wind_create(DLG_KIND, bx, by, bw, bh);
		if (dw[i].h > 0) {
			wind_set_str(dw[i].h, WF_NAME, next_name ? next_name : band_text(tree, dw[i].band) ?
				     band_text(tree, dw[i].band) : dlg_title ? dlg_title : "");
			if (tree) {
				/* in the window the AES's frame is the frame */
				tree[0].ob_state &= ~(OUTLINED | SHADOWED);
				if ((tree[0].ob_type & 0xff) == G_BOX || (tree[0].ob_type & 0xff) == G_IBOX)
					tree[0].ob_spec &= ~0x00ff0000L;
				tree[0].ob_x = wx;
				tree[0].ob_y = wy;
			}
			wind_open(dw[i].h, bx, by, bw, bh);
			wind_get(dw[i].h, WF_WORKXYWH, x, y, w, h);
			if (tree) {
				tree[0].ob_x = *x;
				tree[0].ob_y = *y;
			}
			ndw = i + 1;
			win_paint(i, *x, *y, *w, *h);
			wind_update(BEG_UPDATE);	/* as on TOS: the caller draws and runs it */
			return 1;
		}
		/* no window to be had: over the screen, as on TOS */
		if (dw[i].band)
			band_show(tree, i);
		form_center(tree, x, y, w, h);
	}
	wind_update(BEG_UPDATE);
	wind_update(BEG_MCTRL);
	dw[i].x = *x;
	dw[i].y = *y;
	dw[i].w = *w;
	dw[i].h_ = *h;
	form_dial(FMD_START, *x, *y, *w, *h);
	graf_mouse(M_OFF, 0);
	if (tree)
		objc_draw(tree, 0, 8, *x, *y, *w, *h);
	else if (draw)
		draw(*x, *y, *w, *h);
	graf_mouse(M_ON, 0);
	ndw = i + 1;
	return 1;
}

void dlg_name(const char *name)
{
	next_name = name;
}

void dlg_band(short obj)
{
	next_band = obj;
}

short dlg_open(OBJECT *tree, short *x, short *y, short *w, short *h)
{
	short r = open_any(tree, 0, x, y, w, h);
	next_name = 0;
	next_band = 0;
	return r;
}

short dlg_open_area(void (*draw)(short x, short y, short w, short h),
		    short *x, short *y, short *w, short *h)
{
	short r = open_any(0, draw, x, y, w, h);
	next_name = 0;
	next_band = 0;
	return r;
}

void dlg_close(void)
{
	short i;
	if (!ndw)
		return;
	i = --ndw;
	if (dw[i].h > 0) {
		wind_update(END_UPDATE);
		wind_close(dw[i].h);
		wind_delete(dw[i].h);
		if (dw[i].tree) {
			dw[i].tree[0].ob_state = dw[i].state;
			dw[i].tree[0].ob_spec = dw[i].spec;
			if (dw[i].band)
				band_show(dw[i].tree, i);
		}
		if (!ndw)
			give_back_redraws();
	} else {
		form_dial(FMD_FINISH, dw[i].x, dw[i].y, dw[i].w, dw[i].h_);
		wind_update(END_MCTRL);
		wind_update(END_UPDATE);
	}
}

void dlg_area(short *x, short *y, short *w, short *h)
{
	short i = ndw - 1;
	if (i < 0)
		return;
	if (dw[i].h > 0)
		wind_get(dw[i].h, WF_WORKXYWH, x, y, w, h);
	else {
		*x = dw[i].x;
		*y = dw[i].y;
		*w = dw[i].w;
		*h = dw[i].h_;
	}
}

void dlg_draw(OBJECT *tree, short obj)
{
	short x, y, w, h, i = ndw - 1;
	objc_offset(tree, obj, &x, &y);
	w = tree[obj].ob_width;
	h = tree[obj].ob_height;
	x -= 3;			/* an outline, a 3D edge */
	y -= 3;
	w += 6;
	h += 6;
	if (i >= 0 && dw[i].h > 0)
		win_paint(i, x, y, w, h);
	else
		objc_draw(tree, 0, 8, x, y, w, h);
}

short dlg_evnt(short flags, short clicks, short mask, short state,
	       unsigned long timer, short *msg, AEVENT *e)
{
	short i, k, top, d1;
	if (!dlg_in_window())
		return evnt_multi_(flags, clicks, mask, state, timer, msg, e);
	i = ndw - 1;
	for (;;) {
		wind_update(END_UPDATE);	/* the others draw while we wait */
		evnt_multi_(flags | MU_MESAG, clicks, mask, state, timer, msg, e);
		wind_update(BEG_UPDATE);
		if (e->which & MU_MESAG) {
			int mine = 1;		/* taken here: the caller doesn't see it */
			for (k = 0; k <= i && dw[k].h != msg[3]; k++)
				;
			if (msg[0] == WM_REDRAW && k <= i && dw[k].h > 0) {
				wind_update(END_UPDATE);
				win_paint(k, msg[4], msg[5], msg[6], msg[7]);
				wind_update(BEG_UPDATE);
			} else if (msg[0] == WM_MOVED && k <= i && dw[k].h > 0) {
				wind_set(dw[k].h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
				if (dw[k].tree)
					wind_get(dw[k].h, WF_WORKXYWH, &dw[k].tree[0].ob_x,
						 &dw[k].tree[0].ob_y, &d1, &d1);
			} else if (msg[0] == WM_CLOSED && k == i) {
				/* the closer: as Esc */
				e->which |= MU_KEYBD;
				e->kreturn = 0x011b;
				e->kstate = 0;
			} else if (msg[0] == WM_REDRAW && dlg_redraw_hook) {
				wind_update(END_UPDATE);
				dlg_redraw_hook(msg);
				wind_update(BEG_UPDATE);
			} else if (msg[0] == WM_REDRAW && !(flags & MU_MESAG)) {
				keep_redraw(msg);	/* for when the dialog is gone */
			} else if (msg[0] == WM_TOPPED || msg[0] == WM_NEWTOP || msg[0] == WM_ONTOP) {
				wind_set(dw[i].h, WF_TOP, 0, 0, 0, 0);	/* the dialog stays in front of ours */
			} else {
				mine = 0;
			}
			if (mine || !(flags & MU_MESAG))
				e->which &= ~MU_MESAG;
		}
		if (e->which & (MU_BUTTON | MU_KEYBD)) {
			wind_get(0, WF_TOP, &top, &d1, &d1, &d1);
			if (top != dw[i].h) {
				/* the click or key that brings it to the front only does that */
				wind_set(dw[i].h, WF_TOP, 0, 0, 0, 0);
				e->which &= ~(MU_BUTTON | MU_KEYBD);
			} else if ((e->which & MU_BUTTON) && wind_find(e->mx, e->my) != dw[i].h) {
				Bconout(2, 7);	/* outside the dialog: the bell */
				e->which &= ~MU_BUTTON;
			}
		}
		if (e->which)
			return e->which;
	}
}

short dlg_do(OBJECT *tree, short start, short cancel)
{
	short next, edit = 0, idx = 0, cont = 1, m[8];
	int win = dlg_in_window();
	AEVENT e;
	if (!win) {
		wind_update(BEG_UPDATE);
		wind_update(BEG_MCTRL);
	}
	flush_keys();
	next = start > 0 ? start : first_field(tree);
	while (cont) {
		LFIELD *lf;
		if (next > 0 && next != edit) {
			edit = next;
			next = 0;
			lf = lf_of ? lf_of(tree, edit) : 0;
			if (lf)
				lf_enter(lf, &idx);
			else
				objc_edit(tree, edit, 0, &idx, ED_INIT);
		}
		lf = lf_of && edit > 0 ? lf_of(tree, edit) : 0;
		dlg_evnt(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);
		if (e.which & MU_KEYBD) {
			if (lf && !dlg_esc(e.kreturn) && lf_key(lf, e.kreturn, e.kreturn & 0xff, e.kstate, &idx)) {
				/* the long field took it */
			} else if (dlg_esc(e.kreturn)) {
				/* Cancel: the field isn't given the key, so it keeps its text */
				next = cancel;
				cont = 0;
				if (cancel > 0 && (tree[cancel].ob_state & SELECTED)) {
					tree[cancel].ob_state &= ~SELECTED;
					redraw(tree, cancel);
				}
			} else {
				short kr;
				cont = form_keybd(tree, edit, next, e.kreturn, &next, &kr);
				if (kr && edit > 0 && !lf)	/* a long field's text isn't objc_edit's */
					objc_edit(tree, edit, kr, &idx, ED_CHAR);
			}
		}
		if ((e.which & MU_BUTTON) && cont) {
			next = objc_find(tree, 0, 8, e.mx, e.my);
			if (next < 0) {
				Bconout(2, 7);		/* outside the box: the bell, as form_do */
				next = 0;
			} else {
				cont = form_button(tree, next, e.breturn, &next);
			}
		}
		if ((!cont || (next > 0 && next != edit)) && edit > 0)
			objc_edit(tree, edit, 0, &idx, ED_END);
	}
	if (!win) {
		wind_update(END_MCTRL);
		wind_update(END_UPDATE);
	}
	return next;
}

/* the screen under the box, kept in memory while it shows (form_alert's
   way); vro_cpyfm through a workstation of our own */
typedef struct {
	short vh, pxy[8];
	MFDB scr, mem;
} SAVED;

static int screen_save(SAVED *s, short x, short y, short w, short h)
{
	short cw, ch, bw, bh, wo[57], ex[57], i, planes, words;
	short phys = graf_handle(&cw, &ch, &bw, &bh);
	long size;
	s->vh = v_opnvwk_(phys, wo);
	if (s->vh <= 0)
		return 0;
	vq_extnd(s->vh, 1, ex);
	planes = ex[4];
	/* the outline and shadow too, within the screen */
	x -= 4;
	y -= 4;
	w += 8;
	h += 8;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > wo[0] + 1)
		w = wo[0] + 1 - x;
	if (y + h > wo[1] + 1)
		h = wo[1] + 1 - y;
	words = (w + 15) >> 4;
	size = (long)words * 2 * h * planes;
	if (w <= 0 || h <= 0 || planes <= 0 || !(s->mem.fd_addr = malloc(size))) {
		v_clsvwk(s->vh);
		return 0;
	}
	s->scr.fd_addr = 0;			/* the screen */
	s->mem.fd_w = words * 16;
	s->mem.fd_h = h;
	s->mem.fd_wdwidth = words;
	s->mem.fd_stand = 0;
	s->mem.fd_nplanes = planes;
	s->mem.fd_r1 = s->mem.fd_r2 = s->mem.fd_r3 = 0;
	s->pxy[0] = x;
	s->pxy[1] = y;
	s->pxy[2] = x + w - 1;
	s->pxy[3] = y + h - 1;
	s->pxy[4] = 0;
	s->pxy[5] = 0;
	s->pxy[6] = w - 1;
	s->pxy[7] = h - 1;
	graf_mouse(M_OFF, 0);
	vro_cpyfm(s->vh, 3, s->pxy, &s->scr, &s->mem);	/* S_ONLY */
	graf_mouse(M_ON, 0);
	for (i = 0; i < 4; i++) {		/* the other way, for screen_back */
		short t = s->pxy[i];
		s->pxy[i] = s->pxy[i + 4];
		s->pxy[i + 4] = t;
	}
	return 1;
}

static void screen_back(SAVED *s)
{
	graf_mouse(M_OFF, 0);
	vro_cpyfm(s->vh, 3, s->pxy, &s->mem, &s->scr);
	graf_mouse(M_ON, 0);
	free(s->mem.fd_addr);
	v_clsvwk(s->vh);
}

#ifdef DLG_TRACE
#include <stdio.h>
/* built with -DDLG_TRACE: each exit object's place on the screen to the
   emulator's console (NatFeats), "DLG exit OBJ X Y W H TEXT", so a test
   rig in Hatari can click it */
static void trace(OBJECT *tree)
{
	short i, x, y, w, h;
	char b[80];
	if (dlg_in_window()) {
		wind_get(dw[ndw - 1].h, WF_CURRXYWH, &x, &y, &w, &h);
		snprintf(b, sizeof(b), "DLG window %d %d %d %d\n", x, y, w, h);
		nf_debug(b);
	}
	for (i = 0;; i++) {
		if (tree[i].ob_flags & (EXIT | TOUCHEXIT)) {
			objc_offset(tree, i, &x, &y);
			snprintf(b, sizeof(b), "DLG exit %d %d %d %d %d %s\n", i, x, y,
				 tree[i].ob_width, tree[i].ob_height,
				 (tree[i].ob_type & 0xff) == G_BUTTON ? (const char *)tree[i].ob_spec : "");
			nf_debug(b);
		}
		if (tree[i].ob_flags & LASTOB)
			break;
	}
}
#else
#define trace(tree) ((void)0)
#endif

#define FL3DIND 0x0200
#define FL3DBAK 0x0400
#define FL3DACT 0x0600
#define FL3DMASK 0x0600

int dlg_3d_ok(void)
{
	static int ok = -1;
	short v, a, b, c, d;
	if (ok < 0) {
		v = aes_version();
		ok = v >= 0x0340;
		/* appl_getinfo(13): 3D objects; asked only where it exists (an
		   AES 4, or ?AGI): EmuTOS's hangs */
		if (ok && (v >= 0x0400 || get_cookie(0x3F414749L)) && appl_getinfo(13, &a, &b, &c, &d))
			ok = a != 0;
	}
	return ok;
}

int dlg_3d_on = 1;

/* the object that holds i (objc_parent's way: along its brothers to the
   one whose last child it is) */
static short parent_of(OBJECT *tree, short i)
{
	short p;
	for (;;) {
		p = tree[i].ob_next;
		if (p < 0 || tree[p].ob_tail == i)
			return p;
		i = p;
	}
}

/* a white box with a frame, not the dialog's own: a list or a text area
   (EFTP's Sites, EMail's signature), which stays white in 3D, as fields
   and the Font dialog's lists do, and so does what is in it */
static int is_well(OBJECT *tree, short i)
{
	long spec = tree[i].ob_spec;
	return i > 0 && (tree[i].ob_type & 0xff) == G_BOX && (signed char)(spec >> 16) != 0 &&
	       ((spec & 0x70) == 0 || (spec & 0x0f) == 0);
}

/* a G_BOXTEXT or G_BOX with a frame */
static int framed(OBJECT *tree, short i)
{
	short t = tree[i].ob_type & 0xff;
	if (t == G_BOXTEXT)
		return ((TEDINFO *)tree[i].ob_spec)->te_thickness != 0;
	return t == G_BOX && (signed char)(tree[i].ob_spec >> 16) != 0;
}

static int in_well(OBJECT *tree, short i)
{
	for (i = parent_of(tree, i); i > 0; i = parent_of(tree, i))
		if (is_well(tree, i))
			return 1;
	return 0;
}

void dlg_3d(OBJECT *tree)
{
	short i, t, f;
	if (!dlg_3d_ok())
		return;
	for (i = 0;; i++) {
		t = tree[i].ob_type & 0xff;
		f = tree[i].ob_flags & ~FL3DMASK;
		if (!dlg_3d_on)		/* flat, as asked (the setting) */
			t = -1;
		/* buttons, and what is clicked to act or to choose (a pop-up, an
		   arrow box), out of a list, raised as the AES's buttons; a box
		   only with a frame (a pop-up has one: a page in a sidebar has
		   none and stays on the background; EHypView) */
		if (t == G_BUTTON ||
		    (t >= 0 && (t == G_BOXCHAR || framed(tree, i)) && (f & (EXIT | TOUCHEXIT)) && !in_well(tree, i)))
			f |= (f & (EXIT | TOUCHEXIT)) && !(f & RBUTTON) ? FL3DACT : FL3DIND;
		else if (t >= 0 && t != G_FTEXT && t != G_FBOXTEXT && (i == 0 || (!is_well(tree, i) && !in_well(tree, i))))
			f |= FL3DBAK;		/* the box, its text, its pictures */
		tree[i].ob_flags = f;
		if (tree[i].ob_flags & LASTOB)
			break;
	}
}

static short modal(OBJECT *tree, short start, short cancel, int save)
{
	short x, y, w, h, r;
	int win = dlg_windowed();
	SAVED s;
	dlg_3d(tree);
	if (win) {
		dlg_open(tree, &x, &y, &w, &h);
		save = 0;
	} else {
		wind_update(BEG_UPDATE);
		wind_update(BEG_MCTRL);
		form_center(tree, &x, &y, &w, &h);
		save = save && screen_save(&s, x, y, w, h);
		form_dial(FMD_START, x, y, w, h);
		objc_draw(tree, 0, 8, x, y, w, h);
	}
	trace(tree);
	r = dlg_do(tree, start, cancel);
	if (r != cancel || cancel >= 0)
		r &= 0x7fff;		/* not the double-click bit */
	if (win) {
		dlg_close();
	} else {
		if (save)
			screen_back(&s);
		else
			form_dial(FMD_FINISH, x, y, w, h);
		wind_update(END_MCTRL);
		wind_update(END_UPDATE);
	}
	if (r > 0)
		tree[r].ob_state &= ~SELECTED;
	return r;
}

short dlg_modal(OBJECT *tree, short start, short cancel)
{
	return modal(tree, start, cancel, 0);
}

short dlg_modal_save(OBJECT *tree, short start, short cancel)
{
	return modal(tree, start, cancel, 1);
}
