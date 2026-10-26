/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people_file.c - EContact's CONTACTS.TXT when EContact isn't loaded (see
 * people_file.h): the names, read in pieces behind people_ask.c, and the
 * people dialog.
 */
#include <string.h>
#include <stdlib.h>
#include "plat.h"
#include "people_ask.h"
#include "people_file.h"

#define PIECE	4096		/* read a step for the names: a step stays short
				   on an ST (2.2), the file opened and closed in it */
#define LINE	(PEOPLE_ADDR + PEOPLE_NAME + 4)

static char path[160];
static void (*on_message)(short *m);
static unsigned long seen_date;	/* the file as last read whole */
static long seen_size = -2;
static unsigned long scan_date;	/* a reading in steps: the file's date, size */
static long scan_size, pos = -1;	/* where the next step reads; -1 none */
static char carry[LINE + 1];	/* a line cut by the last step */
static long have;
static int skip;

static int file_dialog(const char *partial, void (*picked)(const PERSON *list, int n));

/* a P line, cut in place: "P<TAB>name<TAB>email<TAB>label"; 1 with an
   address that fits */
static int p_line(char *s, char **name, char **email)
{
	char *t;
	if (s[0] != 'P' || s[1] != '\t')
		return 0;
	*name = s + 2;
	if (!(t = strchr(*name, '\t')))
		return 0;
	*t = 0;
	*email = t + 1;
	if ((t = strchr(*email, '\t')) != 0)
		*t = 0;
	if (strlen(*name) > PEOPLE_NAME - 1)
		(*name)[PEOPLE_NAME - 1] = 0;
	return **email && strlen(*email) < PEOPLE_ADDR;
}

/* a line's end: the 0 in place of its CR */
static void no_cr(char *s)
{
	long l = (long)strlen(s);
	if (l && s[l - 1] == '\r')
		s[l - 1] = 0;
}

/* EContact writes the file anew and renames it into place: for a moment
   there is none. Once read, a missing file counts as unchanged. */
static int gone(void)
{
	pos = -1;
	return seen_size >= 0 ? 0 : -1;
}

/* a step of reading the names: PIECE bytes, the file opened and closed;
   2 more to come, 1 read whole, 0 unchanged since, -1 no file */
static int scan(void (*line)(const char *addr, const char *name, void *ud), void *ud, int force)
{
	char buf[PIECE + LINE + 1], *name, *email, *p, *end;
	long got;
	int h;
	if (force < 0) {		/* every name asked for has come: stop */
		pos = -1;
		return 0;
	}
	if (pos < 0) {
		scan_date = pf_info(path, &scan_size);
		if (!scan_date && scan_size < 0)
			return gone();
		if (!force && scan_date == seen_date && scan_size == seen_size)
			return 0;
		pos = have = 0;
		skip = 0;
	}
	if ((h = pf_open(path, PF_READ)) < 0)
		return gone();
	got = pf_seek(h, pos) == pos ? pf_read(h, buf + have, PIECE) : -1;
	pf_close(h);
	if (got < 0)
		return gone();
	memcpy(buf, carry, (size_t)have);
	have += got;
	pos += got;
	buf[have] = 0;
	/* whole lines; the last one, cut, waits for the next step */
	for (p = buf; (end = strchr(p, '\n')) != 0 || (got == 0 && *p); p = end + 1) {
		if (end)
			*end = 0;
		no_cr(p);
		if (!skip && p_line(p, &name, &email))
			line(email, name, ud);
		skip = 0;
		if (!end)
			break;
	}
	if (got == 0) {			/* the end */
		pos = -1;
		seen_date = scan_date;
		seen_size = scan_size;
		return 1;
	}
	have = (long)strlen(p);
	if (have > LINE) {		/* a long line (a group's): passed over */
		skip = 1;
		have = 0;
	}
	memcpy(carry, p, (size_t)have);
	return 2;
}

int people_file_busy(void)
{
	return pos >= 0;
}

int people_file_scan(void (*line)(const char *addr, const char *name, void *ud), void *ud, int force)
{
	return path[0] ? scan(line, ud, force) : -1;
}

void people_file(const char *folder, void (*message)(short *m))
{
	long l;
	path[0] = 0;
	seen_size = -2;
	pos = -1;
	people_scan_hook = 0;
	people_pick_hook = 0;
	people_busy_hook = 0;
	if (!folder || !*folder || (l = (long)strlen(folder)) > (long)sizeof(path) - 14)
		return;
	strcpy(path, folder);
	if (path[l - 1] != PF_SEP)
		path[l++] = PF_SEP;
	strcpy(path + l, "CONTACTS.TXT");
	on_message = message;
	people_scan_hook = scan;
	people_pick_hook = file_dialog;
	people_busy_hook = people_file_busy;
}

#ifdef PEOPLE_FILE_HOST
static int file_dialog(const char *partial, void (*picked)(const PERSON *list, int n))
{
	(void)partial;
	(void)picked;
	return 0;
}
#else

/* ---------------- the people dialog ---------------- */

#include "gemui.h"
#include "dialog.h"

static char *text;		/* the file, its lines cut in place */
static char **la, **ln;		/* each line's address and name */
static char *mark;		/* marked lines */
static char *group;		/* 1: a group, la its members */
static short *shown;		/* the lines the Find text keeps */
static short nlines, nshown, top, cur, rows;
static char find[32];
static GRECT lr, fr;		/* the list's inside, the Find line */

#define DMAX 10
static OBJECT pd_tree[DMAX];
static short nobj, last_child, o_list, o_find, o_ok, o_cancel;

static short add(short type, short flags, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &pd_tree[i];
	memset(o, 0, sizeof(*o));
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		pd_tree[last_child].ob_next = i;
	else
		pd_tree[0].ob_head = i;
	pd_tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static void build(void)
{
	memset(pd_tree, 0, sizeof(pd_tree));
	pd_tree[0].ob_next = pd_tree[0].ob_head = pd_tree[0].ob_tail = -1;
	pd_tree[0].ob_type = G_BOX;
	pd_tree[0].ob_state = OUTLINED;
	pd_tree[0].ob_spec = 0x00021100L;
	pd_tree[0].ob_width = 52;
	pd_tree[0].ob_height = 18;
	nobj = 1;
	last_child = 0;
	add(G_STRING, 0, (long)"Contacts (from EContact's file)", 2, 1, 31, 1);
	add(G_STRING, 0, (long)"Find:", 2, 2, 5, 1);
	o_find = add(G_IBOX, 0, 0L, 8, 2, 42, 1);
	o_list = add(G_BOX, 0, 0x00FF1100L, 2, 3, 48, 11);
	add(G_STRING, 0, (long)"Click marks; double-click takes one", 2, 14, 35, 1);
	o_ok = add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, (long)"OK", 30, 16, 9, 1);
	o_cancel = add(G_BUTTON, SELECTABLE | EXIT, (long)"Cancel", 41, 16, 9, 1);
	pd_tree[last_child].ob_next = 0;
	pd_tree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(pd_tree, nobj);
	pd_tree[o_list].ob_y += 3;	/* clear of the Find line's underline */
}

static void box(short obj, GRECT *r, short in)
{
	short x, y;
	objc_offset(pd_tree, obj, &x, &y);
	r->x = x + in;
	r->y = y + in;
	r->w = pd_tree[obj].ob_width - 2 * in;
	r->h = pd_tree[obj].ob_height - 2 * in;
}

static char low(char c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int has(const char *s, const char *f)
{
	for (; *s; s++) {
		const char *a = s, *b = f;
		while (*b && low(*a) == low(*b))
			a++, b++;
		if (!*b)
			return 1;
	}
	return !*f;
}

static void refilter(void)
{
	short i;
	nshown = 0;
	for (i = 0; i < nlines; i++)
		if (has(ln[i], find) || has(la[i], find))
			shown[nshown++] = i;
	top = cur = 0;
}

static void keep_cur(void)
{
	if (cur >= nshown)
		cur = nshown - 1;
	if (cur < 0)
		cur = 0;
	if (cur < top)
		top = cur;
	if (cur >= top + rows)
		top = cur - rows + 1;
	if (top > nshown - rows)
		top = nshown - rows;
	if (top < 0)
		top = 0;
}

static short sb_w(void)
{
	return 2 * gl_wchar;
}

static void edge(short x, short y, short w, short h)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	frame(&r, 1);
}

static void arrow(short x, short y, short w, short h, int up)
{
	short cx = x + w / 2, cy = y + h / 2, k = h >= 14 ? 3 : 2;
	short y1 = up ? cy + k / 2 + 1 : cy - k / 2 - 1, y2 = up ? cy - k / 2 - 1 : cy + k / 2 + 1;
	edge(x, y, w, h);
	line_col(cx - k, y1, cx, y2, 1);
	line_col(cx, y2, cx + k, y1, 1);
}

/* the VDI's clip on r, cut to what may be drawn now (in a dialog window,
   the piece the AES asked for: dialog.c's dlg_cut); 0 when none of r */
static int pf_clip(GRECT *r)
{
	GRECT c = *r;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return 0;
	clip_on(&c);
	return 1;
}

static void draw_list(void)
{
	GRECT r = lr, t;
	short i, sw = sb_w(), cols = (r.w - sw - 4) / gl_wchar;
	char row[PEOPLE_ADDR + PEOPLE_NAME + 8];
	graf_mouse(M_OFF, 0);
	if (!pf_clip(&r)) {
		graf_mouse(M_ON, 0);
		return;
	}
	fill(&r, 0);
	for (i = 0; i < rows && top + i < nshown; i++) {
		short k = shown[top + i], y = r.y + i * gl_hchar, fl = TX_LTR;
		row[0] = mark[k] ? 8 : ' ';	/* the system font's tick */
		row[1] = ' ';
		strcpy(row + 2, ln[k][0] || group[k] ? ln[k] : la[k]);
		if (group[k])
			strcat(row, "  (group)");
		else if (ln[k][0]) {
			strcat(row, "  <");
			strcat(row, la[k]);
			strcat(row, ">");
		}
		if (top + i == cur) {
			t.x = r.x;
			t.y = y;
			t.w = r.w - sw;
			t.h = gl_hchar;
			fill(&t, 1);
			fl |= TX_INVERSE;
		}
		text_at(r.x + 2, y, row, (long)strlen(row), cols, fl);
	}
	if (!nshown) {
		const char *s = nlines ? "Nobody by that name" : "EContact's file has nobody in it";
		text_at(r.x + 2, r.y, s, (long)strlen(s), cols, TX_LTR | TX_DIM);
	}
	t.x = r.x + r.w - sw;
	t.y = r.y;
	t.w = sw;
	t.h = r.h;
	fill(&t, 0);
	arrow(t.x, t.y, sw, gl_hchar, 1);
	arrow(t.x, t.y + t.h - gl_hchar, sw, gl_hchar, 0);
	if (nshown > rows) {
		short ty = t.y + gl_hchar, th = t.h - 2 * gl_hchar, h = (short)((long)th * rows / nshown);
		if (h < 6)
			h = 6;
		t.y = ty + (short)((long)(th - h) * top / (nshown - rows));
		t.h = h;
		fill(&t, 1);
	}
	edge(r.x + r.w - sw, r.y, sw, r.h);
	clip_off();
	graf_mouse(M_ON, 0);
}

static void draw_find(void)
{
	char s[sizeof(find) + 1];
	short cols = fr.w / gl_wchar;
	strcpy(s, find);
	strcat(s, "_");
	graf_mouse(M_OFF, 0);
	if (pf_clip(&fr)) {
		fill(&fr, 0);
		text_at(fr.x, fr.y, s, (long)strlen(s), cols, TX_LTR);
		clip_off();
	}
	graf_mouse(M_ON, 0);
}

/* the list's and the search line's places, from the tree's */
static void place(void)
{
	box(o_list, &lr, 1);
	box(o_find, &fr, 0);
}

/* what the dialog's window shows over its tree, drawn again when the AES
   asks (in a window: dlg_more) */
static void pf_more(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	place();
	draw_find();
	draw_list();
}

static short left_down(void)
{
	short mx, my, mb, ks;
	graf_mkstate(&mx, &my, &mb, &ks);
	return mb & 1;
}

static void button(short obj)
{
	short x, y, w = pd_tree[obj].ob_width + 4, h = pd_tree[obj].ob_height + 4;
	pd_tree[obj].ob_state |= SELECTED;
	objc_offset(pd_tree, obj, &x, &y);
	x -= 2;
	y -= 2;
	graf_mouse(M_OFF, 0);
	if (dlg_cut(&x, &y, &w, &h))
		objc_draw(pd_tree, 0, 8, x, y, w, h);
	graf_mouse(M_ON, 0);
	while (left_down())
		evnt_timer_(10);
	pd_tree[obj].ob_state &= ~SELECTED;
}

/* 1 OK, -1 Cancel, 0 on */
static int on_key(short k)
{
	short sc = (k >> 8) & 0xff, as = k & 0xff;
	if (dlg_esc(k))
		return -1;
	if (as == 0x0d || sc == 0x72 || sc == 0x1c)
		return 1;
	if (sc == 0x48 || sc == 0x50) {	/* Up, Down */
		cur += sc == 0x48 ? -1 : 1;
		keep_cur();
		draw_list();
	} else if (sc == 0x52 && nshown) {	/* Insert: marks */
		mark[shown[cur]] ^= 1;
		draw_list();
	} else if (as == 8) {		/* Backspace */
		short l = (short)strlen(find);
		if (l) {
			find[l - 1] = 0;
			refilter();
			draw_find();
			draw_list();
		}
	} else if (as >= 32 && strlen(find) < sizeof(find) - 1) {
		short l = (short)strlen(find);
		find[l] = (char)as;
		find[l + 1] = 0;
		refilter();
		draw_find();
		draw_list();
	}
	return 0;
}

static int in(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static int on_click(short mx, short my, short clicks)
{
	short obj = objc_find(pd_tree, 0, 8, mx, my), sw = sb_w();
	if (in(&lr, mx, my)) {
		if (mx >= lr.x + lr.w - sw) {
			if (my < lr.y + gl_hchar || my >= lr.y + lr.h - gl_hchar) {
				short d = my < lr.y + gl_hchar ? -1 : 1, first = 1;
				do {
					short old = top;
					top += d;
					if (top > nshown - rows)
						top = nshown - rows;
					if (top < 0)
						top = 0;
					if (top != old)
						draw_list();
					evnt_timer_(first ? 250 : 50);
					first = 0;
				} while (left_down());
			} else {
				top += my < lr.y + lr.h / 2 ? -rows : rows;
				if (top > nshown - rows)
					top = nshown - rows;
				if (top < 0)
					top = 0;
				draw_list();
			}
			return 0;
		}
		{
			short k = top + (my - lr.y) / gl_hchar;
			if (k < 0 || k >= nshown)
				return 0;
			cur = k;
			if (clicks >= 2) {	/* that one alone */
				memset(mark, 0, (size_t)nlines);
				mark[shown[k]] = 1;
				return 1;
			}
			mark[shown[k]] ^= 1;
			draw_list();
		}
		return 0;
	}
	if (obj == o_ok || obj == o_cancel) {
		button(obj);
		return obj == o_ok ? 1 : -1;
	}
	return 0;
}

/* a group's members, "Name <email>, Name <email>": each to out (when
   given); how many */
static short members(const char *s, PERSON *out)
{
	short n = 0;
	while (*s) {
		const char *lt = strchr(s, '<'), *gt = lt ? strchr(lt, '>') : 0, *a = s;
		long nl, el;
		if (!gt)
			break;
		while (*a == ' ' || *a == ',')
			a++;
		nl = lt - a;
		while (nl > 0 && a[nl - 1] == ' ')
			nl--;
		el = gt - lt - 1;
		if (el > 0 && el < PEOPLE_ADDR) {
			if (out) {
				if (nl > PEOPLE_NAME - 1)
					nl = PEOPLE_NAME - 1;
				memcpy(out[n].addr, lt + 1, (size_t)el);
				out[n].addr[el] = 0;
				memcpy(out[n].name, a, (size_t)nl);
				out[n].name[nl] = 0;
			}
			n++;
		}
		s = gt + 1;
	}
	return n;
}

/* the file, its P and G lines cut in place; 0 when there is none, -1
   when it is there and doesn't fit in memory */
static int load(void)
{
	long len, size;
	char *p, *name, *email;
	short n = 0, i = 0;
	text = pf_load(path, &len);
	if (!text)
		return pf_info(path, &size) || size >= 0 ? -1 : 0;
	for (p = text; *p; p++)
		if (*p == '\n')
			n++;
	n++;
	la = malloc((size_t)n * sizeof(char *));
	ln = malloc((size_t)n * sizeof(char *));
	mark = calloc((size_t)n, 1);
	group = calloc((size_t)n, 1);
	shown = malloc((size_t)n * sizeof(short));
	if (!la || !ln || !mark || !group || !shown)
		return -1;
	for (p = text; *p && i < n;) {
		char *e = strchr(p, '\n'), *t;
		if (e)
			*e = 0;
		no_cr(p);
		if (p_line(p, &name, &email)) {
			ln[i] = name;
			la[i++] = email;
		} else if (p[0] == 'G' && p[1] == '\t' && (t = strchr(p + 2, '\t')) != 0) {
			*t = 0;			/* a group: its name, its members */
			ln[i] = p + 2;
			if (strlen(ln[i]) > PEOPLE_NAME - 1)
				ln[i][PEOPLE_NAME - 1] = 0;
			la[i] = t + 1;
			if (members(la[i], 0))
				group[i++] = 1;
		}
		if (!e)
			break;
		p = e + 1;
	}
	nlines = i;
	return 1;
}

static void unload(void)
{
	free(text);
	free(la);
	free(ln);
	free(mark);
	free(group);
	free(shown);
	text = mark = group = 0;
	la = ln = 0;
	shown = 0;
	nlines = nshown = 0;
}

static int file_dialog(const char *partial, void (*picked)(const PERSON *list, int n))
{
	short x, y, w, h, m[8], up = 0, i, n = 0;
	int done = 0, r;
	AEVENT e;
	PERSON *list = 0;

	graf_mouse(BUSYBEE, 0);
	r = load();
	graf_mouse(ARROW, 0);
	if (r <= 0) {
		unload();
		return r;		/* 0 no file: as if EContact weren't there;
					   -1 no memory for it */
	}
	font_sys_push();
	find[0] = 0;
	if (partial) {
		strncpy(find, partial, sizeof(find) - 1);
		find[sizeof(find) - 1] = 0;
	}
	build();
	/* in a window of the AES's on MiNT and MagiC (3.5), else over the
	   screen */
	dlg_name("Contacts");
	dlg_open(pd_tree, &x, &y, &w, &h);
	place();
	rows = lr.h / gl_hchar;
	refilter();
	draw_find();
	draw_list();
	dlg_more(pf_more);
	while (!done) {
		short flags = MU_KEYBD | MU_BUTTON | (on_message ? MU_MESAG | MU_TIMER : 0);
		dlg_evnt(flags, up ? 2 : 1, 1, up ? 1 : 0, 250, m, &e);
		place();		/* the window may have moved */
		if ((e.which & MU_MESAG) && on_message)
			on_message(m);
		else if ((e.which & MU_TIMER) && on_message)
			on_message(0);
		if (e.which & MU_KEYBD)
			done = on_key(e.kreturn);
		if ((e.which & MU_BUTTON) && !done) {
			if (up) {
				done = on_click(e.mx, e.my, e.breturn);
				up = 0;
			} else
				up = 1;
		}
	}
	dlg_close();
	clip_off();
	font_sys_pop();
	if (done > 0) {
		short k = 0;
		for (i = 0; i < nlines; i++)
			k += mark[i];
		if (!k && nshown)	/* none marked: the one the cursor is on */
			mark[shown[cur]] = 1;
		for (i = 0; i < nlines; i++)
			if (mark[i])
				n += group[i] ? members(la[i], 0) : 1;
		list = n ? malloc((size_t)n * sizeof(PERSON)) : 0;
		if (!list)
			n = 0;
		for (i = 0, r = 0; list && i < nlines; i++)
			if (mark[i] && group[i])
				r += members(la[i], list + r);	/* a group: its people */
			else if (mark[i]) {
				strcpy(list[r].addr, la[i]);
				strcpy(list[r].name, ln[i]);
				r++;
			}
	}
	unload();
	if (picked)
		picked(list, done > 0 ? n : 0);
	free(list);
	return 1;
}

#endif
