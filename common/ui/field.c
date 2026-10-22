/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * field.c - a text field that holds more than it shows (see field.h).
 * The text is kept aside; the field's TEDINFO shows the piece of it
 * around the cursor. The keys are the field's own here, not objc_edit's:
 * objc_edit only draws the cursor, placed with the arrow keys, which
 * every AES takes (ED_INIT puts it at the end on TOS, where it was asked
 * on some others).
 */
#include <string.h>
#ifndef LF_HOST
#include "tos.h"
#include "gem.h"
#include "clip.h"
#endif
#include "field.h"

#define SC_BACKSPACE 0x0e
#define SC_DELETE    0x53
#define SC_LEFT      0x4b
#define SC_RIGHT     0x4d
#define SC_CLRHOME   0x47
#define LEFT_KEY     0x4b00

/* what the field shows: the text from off, as far as it shows, with the
   cursor in it; a quarter of the field is kept to the cursor's left when
   it goes back, so what it comes to is seen */
void lf_view(LFIELD *f)
{
	short len = (short)strlen(f->text), n, i;
	if (f->cur > len)
		f->cur = len;
	if (f->cur < 0)
		f->cur = 0;
	if (f->cur < f->off)
		f->off = f->cur - f->show / 4;
	if (f->cur > f->off + f->show)
		f->off = f->cur - f->show;
	if (f->off > len - f->show)
		f->off = len - f->show;	/* no empty room at the end */
	if (f->off < 0)
		f->off = 0;
	n = len - f->off;
	if (n > f->show)
		n = f->show;
	for (i = 0; i < n; i++)
		f->view[i] = f->mask ? f->mask : f->text[f->off + i];
	f->view[n] = 0;
}

void lf_end(LFIELD *f)
{
	f->cur = (short)strlen(f->text);
	lf_view(f);
}

/* the editing: 1 when the key was the field's */
int lf_edit(LFIELD *f, short key, short ch, short kstate)
{
	short scan = (short)((key >> 8) & 0xff), len = (short)strlen(f->text);
	unsigned char c = (unsigned char)ch;
	/* Shift: TOS's keyboard gives the shifted arrows and ClrHome a
	   character ('4', '6', '7'), and TOS 1.04's AES sends the key with
	   kstate 0 */
	int shift = (kstate & 3) != 0 || ((scan == SC_LEFT || scan == SC_RIGHT || scan == SC_CLRHOME) && c);
	switch (scan) {
	case SC_LEFT:
		f->cur = shift ? 0 : f->cur - 1;
		break;
	case SC_RIGHT:
		f->cur = shift ? len : f->cur + 1;
		break;
	case SC_CLRHOME:
		f->cur = shift ? len : 0;
		break;
	case SC_BACKSPACE:
		if (f->cur > 0) {
			memmove(f->text + f->cur - 1, f->text + f->cur, (size_t)(len - f->cur + 1));
			f->cur--;
		}
		break;
	case SC_DELETE:
		if (f->cur < len)
			memmove(f->text + f->cur, f->text + f->cur + 1, (size_t)(len - f->cur));
		break;
	default:
		if (c < 32 || c == 127)
			return 0;		/* Return, Tab, Esc, a function key */
		if (len < f->cap) {
			memmove(f->text + f->cur + 1, f->text + f->cur, (size_t)(len - f->cur + 1));
			f->text[f->cur++] = (char)c;
		}
		break;
	}
	lf_view(f);
	return 1;
}

#ifndef LF_HOST

#define NREG 16
static LFIELD *reg[NREG];

void lf_init(LFIELD *f, OBJECT *tree, short obj, char *text, short cap, char mask)
{
	TEDINFO *t = (TEDINFO *)tree[obj].ob_spec;
	short i;
	f->tree = tree;
	f->obj = obj;
	f->text = text;
	f->cap = cap;
	f->show = (short)strlen(t->te_ptmplt);
	if (f->show > LF_VIEW)
		f->show = LF_VIEW;
	f->mask = mask;
	f->off = 0;
	text[cap] = 0;
	lf_end(f);
	t->te_ptext = f->view;
	t->te_txtlen = (short)(f->show + 1);
	for (i = 0; i < NREG; i++)
		if (reg[i] == f || (reg[i] && reg[i]->tree == (void *)tree && reg[i]->obj == obj))
			reg[i] = 0;
	for (i = 0; i < NREG; i++)
		if (!reg[i]) {
			reg[i] = f;
			break;
		}
}

LFIELD *lf_of(OBJECT *tree, short obj)
{
	short i;
	for (i = 0; i < NREG; i++)
		if (reg[i] && reg[i]->tree == (void *)tree && reg[i]->obj == obj)
			return reg[i];
	return 0;
}

void lf_forget(OBJECT *tree)
{
	short i;
	for (i = 0; i < NREG; i++)
		if (reg[i] && reg[i]->tree == (void *)tree)
			reg[i] = 0;
}

/* the cursor drawn where it is in the text */
static void place(LFIELD *f, short *idx)
{
	OBJECT *tree = f->tree;
	short at = f->cur - f->off;
	*idx = at;
	objc_edit(tree, f->obj, 0, idx, ED_INIT);
	while (*idx > at && *idx > 0) {
		short was = *idx;
		objc_edit(tree, f->obj, LEFT_KEY, idx, ED_CHAR);
		if (*idx >= was)
			break;			/* an AES that doesn't move it */
	}
}

static void draw(LFIELD *f)
{
	OBJECT *tree = f->tree;
	short x, y;
	objc_offset(tree, f->obj, &x, &y);
	objc_draw(tree, 0, 8, x - 2, y - 2, tree[f->obj].ob_width + 4, tree[f->obj].ob_height + 4);
}

void lf_enter(LFIELD *f, short *idx)
{
	char was[LF_VIEW + 1];
	strcpy(was, f->view);
	lf_end(f);
	if (strcmp(was, f->view))
		draw(f);
	place(f, idx);
}

/* ^C ^X ^V on the whole text; a password is never copied out */
static int clip(LFIELD *f, short key)
{
	char buf[256], *p;
	switch (clip_key(key)) {
	case CLIP_COPY:
		if (!f->mask)
			clip_put(f->text, (long)strlen(f->text));
		return 1;
	case CLIP_CUT:
		if (!f->mask && clip_put(f->text, (long)strlen(f->text))) {
			f->text[0] = 0;
			lf_view(f);
		}
		return 1;
	case CLIP_PASTE:
		clip_get(buf, sizeof(buf));
		for (p = clip_line(buf); *p; p++)
			lf_edit(f, (unsigned char)*p, (unsigned char)*p, 0);
		return 1;
	}
	return 0;
}

int lf_key(LFIELD *f, short key, short ch, short kstate, short *idx)
{
	short cur = f->cur, off = f->off;
	char was[LF_VIEW + 1];
	strcpy(was, f->view);
	if (clip_key(key) == CLIP_NONE) {
		if (!lf_edit(f, key, ch, kstate))
			return 0;
	} else if (!clip(f, key)) {
		return 0;
	}
	if (!strcmp(was, f->view) && off == f->off && cur == f->cur)
		return 1;			/* nothing moved: at an end, or full */
	objc_edit(f->tree, f->obj, 0, idx, ED_END);
	if (strcmp(was, f->view))
		draw(f);
	place(f, idx);
	return 1;
}

/* a password shown as stars or in clear */
void lf_mask(LFIELD *f, char mask, short *idx, int has_cursor)
{
	if (has_cursor)
		objc_edit(f->tree, f->obj, 0, idx, ED_END);
	f->mask = mask;
	lf_view(f);
	draw(f);
	if (has_cursor)
		place(f, idx);
}

#endif
