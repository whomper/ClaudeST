/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * eye.c - the eye next to a password field (field.h; the guidelines' 5.4, C11, C12). A 16 pixel wide
 * image, 16 rows (8 on ST medium): an open eye, crossed out while the password shows. EFTP's,
 * EMail's and EUpdate's, here once. Its own file so that an app that has no password pays nothing:
 * dialog.c and field.c call it through weak references. db_pass (dlgbuild.c) makes the field and its
 * eye; dlg_do turns it.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "field.h"
#include "dialog.h"

#define LF_EYES 4

extern short gl_hchar;			/* the AES's character height (gemui.h: every app has it) */

/* the open eye, one row a short, top row first */
static const unsigned short eye16[16] = {
	0x0000, 0x0000, 0x0000, 0x07e0, 0x1818, 0x2184, 0x43c2, 0x83c1,
	0x43c2, 0x2184, 0x1818, 0x07e0, 0x0000, 0x0000, 0x0000, 0x0000,
};
static unsigned short eye_open[16], eye_shut[16];
static short eye_rows;
static BITBLK eye_blk[LF_EYES];
static LFIELD *eye_own[LF_EYES];

static void eye_make(void)
{
	short tall = gl_hchar >= 16, rows = tall ? 16 : 8, r;
	if (eye_rows == rows)
		return;
	for (r = 0; r < rows; r++) {
		unsigned short v = eye16[tall ? r : r * 2 + 1];
		eye_open[r] = v;
		/* crossed out: a diagonal from bottom left to top right */
		eye_shut[r] = (unsigned short)(v | (0x8000 >> ((rows - 1 - r) * 16 / rows)) | (0x4000 >> ((rows - 1 - r) * 16 / rows)));
	}
	eye_rows = rows;
}

static void eye_pick(LFIELD *f)
{
	short i;
	for (i = 0; i < LF_EYES; i++)
		if (eye_own[i] == f)
			eye_blk[i].bi_pdata = f->clear ? eye_shut : eye_open;
}

void *lf_eye_image(LFIELD *f)
{
	short i;
	BITBLK *b;
	eye_make();
	for (i = 0; i < LF_EYES; i++)
		if (eye_own[i] == f)
			break;
	if (i == LF_EYES)
		for (i = 0; i < LF_EYES; i++)
			if (!eye_own[i])
				break;
	if (i == LF_EYES)
		return 0;
	eye_own[i] = f;
	f->clear = f->mask == 0;
	b = &eye_blk[i];
	b->bi_wb = 2;
	b->bi_hl = eye_rows;
	b->bi_x = b->bi_y = 0;
	b->bi_color = 1;
	eye_pick(f);
	return b;
}

void lf_eye_set(LFIELD *f, short obj)
{
	f->eye = obj;
}

int lf_eye_click(OBJECT *tree, short obj, short *idx, short edit)
{
	short i;
	LFIELD *f = 0;
	if (obj <= 0)
		return 0;
	for (i = 0; i < LF_EYES; i++)
		if (eye_own[i] && eye_own[i]->tree == (void *)tree && eye_own[i]->eye == obj)
			f = eye_own[i];
	if (!f)
		return 0;
	f->clear = !f->clear;
	eye_pick(f);
	lf_mask(f, f->clear ? 0 : '*', idx, edit == f->obj);	/* f->secret stays: a shown password is still not copied */
	dlg_draw(tree, obj);
	return 1;
}

/* the dialog is gone (lf_forget): its eyes are let go */
void lf_eye_forget(OBJECT *tree)
{
	short i;
	for (i = 0; i < LF_EYES; i++)
		if (eye_own[i] && eye_own[i]->tree == (void *)tree)
			eye_own[i] = 0;
}
