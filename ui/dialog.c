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

short dlg_do(OBJECT *tree, short start, short cancel)
{
	short next, edit = 0, idx = 0, cont = 1, m[8];
	AEVENT e;
	wind_update(BEG_UPDATE);
	wind_update(BEG_MCTRL);
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
		evnt_multi_(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);
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
	wind_update(END_MCTRL);
	wind_update(END_UPDATE);
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
	short i, x, y;
	char b[80];
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
		if (t == G_BUTTON || (t == G_BOXCHAR && (f & (EXIT | TOUCHEXIT))))
			f |= (f & (EXIT | TOUCHEXIT)) && !(f & RBUTTON) ? FL3DACT : FL3DIND;
		else if (t >= 0 && t != G_FTEXT && t != G_FBOXTEXT)
			f |= FL3DBAK;		/* the box, its text, its pictures */
		tree[i].ob_flags = f;
		if (tree[i].ob_flags & LASTOB)
			break;
	}
}

static short modal(OBJECT *tree, short start, short cancel, int save)
{
	short x, y, w, h, r;
	SAVED s;
	dlg_3d(tree);
	wind_update(BEG_UPDATE);
	wind_update(BEG_MCTRL);
	form_center(tree, &x, &y, &w, &h);
	save = save && screen_save(&s, x, y, w, h);
	form_dial(FMD_START, x, y, w, h);
	objc_draw(tree, 0, 8, x, y, w, h);
	trace(tree);
	r = dlg_do(tree, start, cancel);
	if (r != cancel || cancel >= 0)
		r &= 0x7fff;		/* not the double-click bit */
	if (save)
		screen_back(&s);
	else
		form_dial(FMD_FINISH, x, y, w, h);
	wind_update(END_MCTRL);
	wind_update(END_UPDATE);
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
