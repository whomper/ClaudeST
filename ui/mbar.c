/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mbar.c - the menu bar, built in code (see mbar.h), from EHypView.
 *
 * The tree has the layout the AES expects:
 *
 *   0 root (IBOX, whole screen)
 *   1   the bar (BOX)
 *   2     the active area (IBOX) with the titles (TITLE)
 *   n   the screen (IBOX) with one drop-down (BOX) per title,
 *         each holding its items (STRING)
 *
 * The first drop-down is the Desk menu: the app's items, a separator and
 * six slots the AES fills with the accessories' names.
 */
#include <stdlib.h>
#include <string.h>
#include "mbar.h"

#define G_IBOX_SPEC 0x00000000L
#define BAR_SPEC    0x00001100L	/* no frame, black on white */
#define DROP_SPEC   0x00FF1100L	/* one pixel frame */
#define ACC_SLOTS   6

/* the accessory slots must be writable: the AES fills them in */
static char acc_slot[ACC_SLOTS][24];

static short count_items(const MBAR_ITEM *it)
{
	short n = 0;
	while (it[n].text)
		n++;
	return n;
}

/* a drop-down's width in characters: "  Text  Key " for the longest */
static short menu_width(const MBAR_ITEM *it, int desk)
{
	short i, width = 0, n = count_items(it);
	for (i = 0; i < n; i++) {
		short need = (short)(strlen(it[i].text) + 4 + (it[i].key ? strlen(it[i].key) + 2 : 0));
		if (need > width)
			width = need;
	}
	if (desk && width < 22)
		width = 22;			/* room for accessory names */
	return width;
}

static void set(OBJECT *o, short next, short head, short tail, unsigned short type,
		long spec, short x, short y, short w, short h)
{
	o->ob_next = next;
	o->ob_head = head;
	o->ob_tail = tail;
	o->ob_type = type;
	o->ob_flags = 0;
	o->ob_state = 0;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
}

/* item texts: "  Text      Key " padded to the drop-down's width */
static char *item_text(char *pool, const MBAR_ITEM *it, short width)
{
	short n, tl, kl;
	if (!strcmp(it->text, "-")) {
		for (n = 0; n < width; n++)
			pool[n] = '-';
		pool[width] = 0;
		return pool;
	}
	memset(pool, ' ', (size_t)width);
	pool[width] = 0;
	tl = (short)strlen(it->text);
	kl = it->key ? (short)strlen(it->key) : 0;
	memcpy(pool + 2, it->text, (size_t)tl);
	if (kl > 0)
		memcpy(pool + width - kl - 1, it->key, (size_t)kl);
	return pool;
}

int mbar_build(MBAR *m, const MBAR_TITLE *titles, short cw, short ch, short screen_w, short screen_h)
{
	static const MBAR_ITEM sep = { "-", 0, 0 };
	short nt = 0, total = 0, t, i, obj, bar_h = (short)(ch + 3);
	short first_title, screen_obj, first_drop, x, last_drop = -1;
	size_t pool_size = 0;
	char *p;
	OBJECT *tree;

	memset(m, 0, sizeof(*m));
	while (titles[nt].title) {
		short n = count_items(titles[nt].items);
		total = (short)(total + n + (nt == 0 ? 1 + ACC_SLOTS : 0));
		/* the texts, each padded to the drop-down's width (the Desk
		   menu's separator too) */
		pool_size += (size_t)(n + (nt == 0)) * (size_t)(menu_width(titles[nt].items, nt == 0) + 1);
		nt++;
	}
	m->ntitles = nt;
	m->nobjects = (short)(3 + nt + 1 + nt + total);
	tree = calloc((size_t)m->nobjects, sizeof(OBJECT));
	m->cmd_of = calloc((size_t)m->nobjects, sizeof(int));
	p = m->pool = calloc(1, pool_size + 1);
	m->tree = tree;
	if (!tree || !m->cmd_of || !m->pool) {
		mbar_free(m);
		return -1;
	}

	first_title = 3;
	screen_obj = (short)(first_title + nt);
	first_drop = (short)(screen_obj + 1);
	set(&tree[0], -1, 1, screen_obj, G_IBOX, G_IBOX_SPEC, 0, 0, screen_w, screen_h);
	set(&tree[1], screen_obj, 2, 2, G_BOX, BAR_SPEC, 0, 0, screen_w, bar_h);

	/* titles */
	x = 0;
	for (t = 0; t < nt; t++) {
		short w = (short)(strlen(titles[t].title) * cw);
		set(&tree[first_title + t], (short)(t + 1 < nt ? first_title + t + 1 : 2), -1, -1, G_TITLE,
		    (long)titles[t].title, x, 0, w, bar_h);
		x = (short)(x + w);
	}
	set(&tree[2], 1, first_title, (short)(first_title + nt - 1), G_IBOX, G_IBOX_SPEC,
	    (short)(2 * cw), 0, x, (short)(bar_h - 1));

	/* drop-downs */
	obj = first_drop;
	for (t = 0; t < nt; t++) {
		const MBAR_ITEM *it = titles[t].items;
		short own = count_items(it), n = (short)(own + (t == 0 ? 1 + ACC_SLOTS : 0));
		short width = menu_width(it, t == 0), drop = obj, dx, dw;
		dw = (short)(width * cw);
		dx = (short)(tree[2].ob_x + tree[first_title + t].ob_x);
		if (dx + dw > screen_w)
			dx = (short)(screen_w - dw);
		if (dx < 0)
			dx = 0;
		set(&tree[drop], -1, (short)(drop + 1), (short)(drop + n), G_BOX, DROP_SPEC,
		    dx, 0, dw, (short)(n * ch));
		if (last_drop >= 0)
			tree[last_drop].ob_next = drop;
		last_drop = drop;
		for (i = 0; i < n; i++) {
			short o = (short)(drop + 1 + i);
			const MBAR_ITEM *def = i < own ? &it[i] : i == own ? &sep : 0;
			long spec;
			if (!def) {			/* an accessory slot */
				short len = width < (short)sizeof(acc_slot[0]) ? width :
					    (short)(sizeof(acc_slot[0]) - 1);
				memset(acc_slot[i - own - 1], ' ', (size_t)len);
				acc_slot[i - own - 1][len] = 0;
				spec = (long)acc_slot[i - own - 1];
			} else {
				spec = (long)item_text(p, def, width);
				p += width + 1;
			}
			set(&tree[o], (short)(i + 1 < n ? o + 1 : drop), -1, -1, G_STRING, spec,
			    0, (short)(i * ch), dw, ch);
			if (def && !strcmp(def->text, "-"))
				tree[o].ob_state = DISABLED;
			m->cmd_of[o] = def ? def->cmd : 0;
		}
		obj = (short)(drop + 1 + n);
	}
	tree[last_drop].ob_next = screen_obj;
	set(&tree[screen_obj], 0, first_drop, last_drop, G_IBOX, G_IBOX_SPEC,
	    0, bar_h, screen_w, (short)(screen_h - bar_h));
	tree[m->nobjects - 1].ob_flags |= LASTOB;
	return 0;
}

void mbar_free(MBAR *m)
{
	free(m->pool);
	free(m->cmd_of);
	free(m->tree);
	memset(m, 0, sizeof(*m));
}

short mbar_object(const MBAR *m, int cmd)
{
	short i;
	if (!cmd)
		return -1;
	for (i = 0; i < m->nobjects; i++)
		if (m->cmd_of[i] == cmd)
			return i;
	return -1;
}

int mbar_command(const MBAR *m, short item)
{
	if (item < 0 || item >= m->nobjects)
		return 0;
	return m->cmd_of[item];
}
