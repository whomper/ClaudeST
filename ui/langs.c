/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * langs.c - the family's Languages dialog (langs.h; standards/
 * TRANSLATION-SERVICE.md), on the Gateway's client (src/translate.c).
 * Laid out as the Font dialog is (fontsel.c): 48 by 17 cells of the system
 * font, one list with a pane bar of AES objects at its right (panebar.c),
 * the total line, a line for what the user is told, Cancel and OK (10 wide, OK
 * at the right) under a black title band " Languages". The
 * rows are drawn by the app's gemui.h calls and kept to the clip the
 * dialog's window may draw (dlg_cut), so the list is right in a window of
 * the AES's (MiNT, MagiC) and over the screen (TOS).
 *
 * The cursor row (the black bar) is where the keys are; the tick is the
 * user's: Space ticks it, and so does a click, on the release over the row
 * (5.8: the press picks the row, the release does the rest). A tick draws
 * the row and the total line, a key the old and the new row, a scroll the
 * rows and the slider's part of the bar (pb_moved).
 *
 * Built with -DLANGS_PURE_ONLY, only the part with no GEM, for tests/langs
 * on the build machine.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "langs.h"
#include "charset.h"
#include "bidi.h"
#include "conn.h"
#include "progbar.h"

/* ---------------- the words and the rules ---------------- */

static char low(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/* v in decimal at p (no snprintf: 2.9 ms a call on an 8 MHz ST); returns the digits' end */
static char *put_num(char *p, long v)
{
	char t[12];
	int n = 0;
	if (v < 0)
		v = 0;
	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v && n < 11);
	while (n)
		*p++ = t[--n];
	return p;
}

static void put(char *dst, const char *s, int max)
{
	int n = (int)strlen(s);
	if (n > max - 1)
		n = max - 1;
	memcpy(dst, s, n);
	dst[n] = 0;
}

void langs_name(const TR_LANGINFO *i, char *out)
{
	const unsigned char *p;
	int ascii = 1;
	for (p = (const unsigned char *)i->name; *p; p++)
		if (*p >= 0x80)
			ascii = 0;
	if (i->name[0] && ascii) {
		put(out, i->name, LANGS_NAME);
		return;
	}
	if (i->name[0]) {
		long n = 0;
		char *a = cs_to_atari(i->name, (long)strlen(i->name), CS_UTF8, &n);
		if (a) {
			int bad = 0, k;
			for (k = 0; k < n; k++)
				if (a[k] == '?' && !strchr(i->name, '?'))
					bad = 1;	/* a letter the Atari set hasn't: shown by its code */
			if (!bad && n > 0 && n < LANGS_NAME && n < BIDI_MAX) {
				if (bidi_has_rtl(a, (short)n)) {
					char vis[BIDI_MAX];
					bidi_visual(a, (short)n, bidi_is_rtl(a, (short)n), vis);
					memcpy(out, vis, n);
				} else {
					memcpy(out, a, n);
				}
				out[n] = 0;
				free(a);
				return;
			}
			free(a);
		}
	}
	put(out, i->code, LANGS_NAME);
}

int langs_row(const TR_LANGINFO *i, const char *name, int cols, char *out)
{
	char sz[16];
	int szl, nl, room, k;
	if (cols < 1)
		return 0;
	memset(out, ' ', cols);
	if (!strcmp(i->code, "en")) {
		strcpy(sz, "always");
		szl = 6;
	} else {
		char *e = put_num(sz, i->mb);
		*e++ = ' ';
		*e++ = 'M';
		*e++ = 'B';
		*e = 0;
		szl = (int)(e - sz);
	}
	/* the tick box: [ ] or [x], the x being the system font's tick */
	if (cols >= 1)
		out[0] = '[';
	if (cols >= 2)
		out[1] = (!strcmp(i->code, "en") || i->want) ? (char)LANGS_TICK : ' ';
	if (cols >= 3)
		out[2] = ']';
	/* the name from column 4 to a blank before the size, the size at the right edge */
	nl = (int)strlen(name);
	room = cols - 4 - szl - 1;
	if (room < 1)
		room = cols > 4 ? cols - 4 : 0;
	if (nl > room)
		nl = room;
	for (k = 0; k < nl; k++)
		out[4 + k] = name[k];
	if (cols - szl >= 4 + nl + 1)
		memcpy(out + cols - szl, sz, szl);
	return cols;
}

int langs_total_line(const TR_LANGS *l, char *out, int max)
{
	char t[48], *p = t;
	memcpy(p, "Installed ", 10);
	p = put_num(p + 10, translate_langs_total(l));
	memcpy(p, " MB of ", 7);
	p = put_num(p + 7, l->cap_mb);
	memcpy(p, " MB", 4);
	p += 3;
	put(out, t, max);
	return (int)(p - t);
}

int langs_over_line(const TR_LANGS *l, int i, const char *name, char *out, int max)
{
	if (i < 0 || i >= l->n)
		return snprintf(out, max, "%s", "");
	return snprintf(out, max, "%s does not fit: it needs %d MB more.", name && name[0] ? name : l->l[i].code,
			translate_langs_over(l, i));
}

int langs_toggle(TR_LANGS *l, int i)
{
	if (i < 0 || i >= l->n)
		return LT_NOTHING;
	if (!strcmp(l->l[i].code, "en"))
		return LT_FIXED;
	if (l->l[i].want) {
		l->l[i].want = 0;
		return LT_UNTICKED;
	}
	if (!translate_langs_fits(l, i))
		return LT_REFUSED;
	l->l[i].want = 1;
	return LT_TICKED;
}

static int cmp_name(const char *a, const char *b)
{
	for (;; a++, b++) {
		unsigned char x = (unsigned char)*a, y = (unsigned char)*b;	/* (folded in line: a call a letter was most of the sort's time) */
		if (x >= 'A' && x <= 'Z')
			x = (unsigned char)(x + 32);
		if (y >= 'A' && y <= 'Z')
			y = (unsigned char)(y + 32);
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
	}
}

/* a sorts strictly before b: English (index en) first, then by name, then by code */
static int before(const TR_LANGS *l, const langs_nm *names, int en, short a, short b)
{
	int c;
	if (a == en || b == en)
		return a == en && b != en;
	c = cmp_name(names[a], names[b]);
	if (c)
		return c < 0;
	return strcmp(l->l[a].code, l->l[b].code) < 0;
}

void langs_sort(const TR_LANGS *l, const langs_nm *names, short *ord)
{
	int i, j, en = -1;
	for (i = 0; i < l->n; i++)
		if (!strcmp(l->l[i].code, "en"))
			en = i;
	/* binary insertion: some 250 comparisons for 50 languages, where the plain way made 600 */
	for (i = 0; i < l->n; i++) {
		short v = (short)i;
		int lo = 0, hi = i;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			if (before(l, names, en, v, ord[mid]))
				hi = mid;
			else
				lo = mid + 1;
		}
		for (j = i; j > lo; j--)
			ord[j] = ord[j - 1];
		ord[lo] = v;
	}
}

int langs_clamp_top(int top, int rows, int n)
{
	if (top > n - rows)
		top = n - rows;
	if (top < 0)
		top = 0;
	return top;
}

int langs_show(int top, int cur, int rows, int n)
{
	if (cur < top)
		top = cur;
	if (cur >= top + rows)
		top = cur - rows + 1;
	return langs_clamp_top(top, rows, n);
}

int langs_move(int cur, int n, int rows, int scan, int shift)
{
	int step = shift ? (rows > 1 ? rows - 1 : 1) : 1;
	if (n <= 0)
		return -1;
	if (cur < 0)
		cur = 0;
	switch (scan) {
	case 0x48:
		cur -= step;
		break;
	case 0x50:
		cur += step;
		break;
	case 0x47:
		cur = shift ? n - 1 : 0;
		break;
	}
	if (cur < 0)
		cur = 0;
	if (cur > n - 1)
		cur = n - 1;
	return cur;
}

int langs_find_letter(const short *ord, const langs_nm *names, int n, int cur, int ch)
{
	int i;
	for (i = 1; i <= n; i++) {
		int j = (cur + i) % n;		/* (cur -1: from the first) */
		if (low(names[ord[j]][0]) == low((char)ch))
			return j;
	}
	return -1;
}

int langs_wrap(const char *s, int cols, char *l1, char *l2)
{
	int n = (int)strlen(s), cut, rest, i;
	if (cols < 2) {
		l1[0] = l2[0] = 0;
		return 0;
	}
	if (n <= cols) {
		put(l1, s, cols + 1);
		l2[0] = 0;
		return 1;
	}
	cut = cols;			/* the last blank within cols, else cols */
	for (i = cols; i > 0; i--)
		if (s[i] == ' ') {
			cut = i;
			break;
		}
	rest = cut;			/* where the second line begins: past the blanks */
	while (s[rest] == ' ')
		rest++;
	while (cut > 0 && s[cut - 1] == ' ')
		cut--;			/* no blanks at the end of the first */
	memcpy(l1, s, cut);
	l1[cut] = 0;
	put(l2, s + rest, cols + 1);
	return 2;
}

long langs_mb(long bytes)
{
	if (bytes <= 0)
		return 0;
	return (bytes + 524288L) / 1048576L;
}

/* ---------------- the job ---------------- */

void langs_job_begin(langs_job *j, const char *host, unsigned short port, int id, int adding_mb,
		     unsigned long now_ms)
{
	memset(j, 0, sizeof(*j));
	put(j->host, host ? host : "", sizeof(j->host));
	j->port = port;
	j->id = id;
	j->adding_mb = adding_mb;
	j->state = LJ_RUNNING;
	j->next_ms = now_ms;		/* the first look at once */
	put(j->text, adding_mb > 0 ? LANGS_W_WAIT : LANGS_W_REMOVING, sizeof(j->text));
}

int langs_job_apply(langs_job *j, const TR_JOB *t)
{
	char was[sizeof(j->text)];
	long dd = j->done, dt = j->total;
	int ds = j->state, adding = j->adding_mb > 0;
	memcpy(was, j->text, sizeof(was));
	if (t->id > j->id) {
		/* a later job than ours: ours has ended (another Atari's came after it) */
		j->state = LJ_DONE;
	} else if (t->id < j->id) {
		return 0;		/* an older job's record: nothing about ours yet */
	} else {
		switch (t->state) {
		case TR_JOB_RUNNING:
			j->state = LJ_RUNNING;
			j->done = t->done;
			j->total = t->total;
			break;
		case TR_JOB_DONE:
			j->state = LJ_DONE;
			break;
		case TR_JOB_FAILED:
			j->state = LJ_FAILED;
			break;
		default:
			j->state = LJ_LOST;		/* "none": the Gateway has forgotten it */
			break;
		}
	}
	switch (j->state) {
	case LJ_RUNNING:
		if (adding && j->total > 0) {
			long tot = langs_mb(j->total);
			snprintf(j->text, sizeof(j->text), LANGS_W_RUN, langs_mb(j->done), tot < 1 ? 1L : tot);
		} else {
			put(j->text, adding ? LANGS_W_WAIT : LANGS_W_REMOVING, sizeof(j->text));
		}
		break;
	case LJ_DONE:
		put(j->text, adding ? LANGS_W_DONE : LANGS_W_REMOVED, sizeof(j->text));
		if (t->id == j->id && t->state == TR_JOB_DONE && j->total > 0)
			j->done = j->total;
		break;
	case LJ_FAILED:
		snprintf(j->text, sizeof(j->text), adding ? LANGS_W_FAIL : LANGS_W_FAIL_RM,
			 t->why[0] ? t->why : "no reason given");
		break;
	default:
		put(j->text, LANGS_W_UNKNOWN, sizeof(j->text));
		break;
	}
	return strcmp(was, j->text) != 0 || dd != j->done || dt != j->total || ds != j->state;
}

/* now is at or past at, the clock a 32-bit one that wraps (tos_ms) */
static int later(unsigned long now, unsigned long at)
{
	return (int)(unsigned int)(now - at) >= 0;
}

int langs_job_step(langs_job *j, unsigned long now_ms)
{
	TR_JOB t;
	char err[96];
	long saved;
	int ok, changed;
	if (j->state != LJ_RUNNING)
		return 0;
	if (!later(now_ms, j->next_ms))
		return 0;
	conn_stop = 0;			/* a stop is for one operation */
	saved = translate_timeout_ms;
	translate_timeout_ms = LANGS_POLL_TIMEOUT_MS;
	ok = translate_job_get(j->host, j->port, &t, err, sizeof(err));
	translate_timeout_ms = saved;
	conn_stop = 0;
	if (!ok) {
		/* no answer: the Gateway restarts itself when the job ends, so wait a while before
		   saying it is gone */
		if (!j->lost_since)
			j->lost_since = now_ms | 1;
		j->next_ms = now_ms + LANGS_POLL_SLOW_MS;
		if (later(now_ms, j->lost_since + LANGS_LOST_MS)) {
			j->state = LJ_LOST;
			put(j->text, LANGS_W_LOST, sizeof(j->text));
			return 2;
		}
		return 0;
	}
	j->lost_since = 0;
	j->next_ms = now_ms + LANGS_POLL_MS;
	changed = langs_job_apply(j, &t);
	if (j->state != LJ_RUNNING)
		return 2;
	return changed;
}

short langs_job_permille(const langs_job *j)
{
	long d = j->done, t = j->total;
	if (j->state == LJ_DONE)
		return PB_FULL;
	if (t <= 0 || d <= 0)
		return 0;
	while (t > 0xFFFFFL) {		/* (d * 1024 must fit a long) */
		t >>= 1;
		d >>= 1;
	}
	if (t < 1)
		return 0;
	d = d * PB_FULL / t;
	return (short)(d > PB_FULL ? PB_FULL : d);
}

#ifndef LANGS_PURE_ONLY

/* ---------------- the dialog ---------------- */

#include "plat.h"
#include "tos.h"
#include "dialog.h"
#include "panebar.h"
#include "click.h"
#include "alert.h"

#ifdef LANGS_TRACE
#define TRACE(...) do { char tb[160]; snprintf(tb, sizeof(tb), __VA_ARGS__); nf_debug(tb); } while (0)
/* where a stretch's time goes: "LANGS ms WHAT N" (ms since the last mark) */
static unsigned long mark_t;
#define MARK(what) do { unsigned long n_ = tos_ms(); TRACE("LANGS ms %s %lu\n", what, n_ - mark_t); mark_t = tos_ms(); } while (0)
#else
#define TRACE(...) do { } while (0)
#define MARK(what) do { } while (0)
#endif

#define DMAX 12
static OBJECT ltree[DMAX];
static short nobj, last_child;

static void tr_begin(short w, short h)
{
	memset(ltree, 0, sizeof(ltree));
	nobj = 1;
	last_child = 0;
	ltree[0].ob_next = -1;
	ltree[0].ob_head = ltree[0].ob_tail = -1;
	ltree[0].ob_type = G_BOX;
	ltree[0].ob_state = OUTLINED;
	ltree[0].ob_spec = 0x00021100L;
	ltree[0].ob_width = w;
	ltree[0].ob_height = h;
}

static short tr_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &ltree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		ltree[last_child].ob_next = i;
	else
		ltree[0].ob_head = i;
	ltree[0].ob_tail = i;
	last_child = i;
	return i;
}

static void tr_end(void)
{
	ltree[last_child].ob_next = 0;
	ltree[nobj - 1].ob_flags |= LASTOB;
	rsrc_obfix_all(ltree, nobj);
	dlg_3d(ltree);
}

static langs *lg;
static short o_list, o_tot, o_msg, o_msg2, o_ok, o_cancel, o_band;
/* the title band (5.10, D22): the first row, the dialog's width, white on black, the text left with a space before it as
   Settings has it; in a window of the AES's the window's own title replaces it (dlg_band) */
static TEDINFO band_ted = { (char *)" Languages", (char *)"", (char *)"", 3, 0, 0, 0x1071, 0, 0, 11, 1 };
static short ord[TR_LANGS_MAX];
static langs_nm names[TR_LANGS_MAX];
static int cur, top, rows, cols;
static GRECT rowsr;		/* where the rows are */
static PANEBAR bar;
#define LD_COLS 44		/* the lines of text: the list's width in cells */
static char tot_text[LD_COLS + 4], msg_text[2 * LD_COLS + 1], msg_l1[LD_COLS + 1], msg_l2[LD_COLS + 1];

/* the mouse hidden once for a whole action (an AES call costs an 8 MHz ST some 5 ms, and a tick draws the
   row, the total and the message): the calls nest */
static int hidden;

static void hide(void)
{
	if (!hidden++)
		graf_mouse(M_OFF, 0);
}

static void show(void)
{
	if (hidden > 0 && !--hidden)
		graf_mouse(M_ON, 0);
}

/* where object o is: every object is a child of the root, whose place dlg_open and a moved window keep
   (an objc_offset is an AES call) */
static void origin(short o, short *x, short *y)
{
	*x = ltree[0].ob_x + ltree[o].ob_x;
	*y = ltree[0].ob_y + ltree[o].ob_y;
}

static int clip(GRECT *r)
{
	GRECT c = *r;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return 0;
	clip_on(&c);
	return 1;
}

/* where the list is, from the tree (the window may have moved) */
static void place(void)
{
	short x, y, w, h;
	origin(o_list, &x, &y);
	w = ltree[o_list].ob_width;
	h = ltree[o_list].ob_height;
	/* the bar is the list's right edge, its frame the list's line (5.7: one line between panes) */
	bar.r.w = pb_width();
	bar.r.x = x + w + 1 - bar.r.w;
	bar.r.y = y - 1;
	bar.r.h = h + 2;
	rowsr.x = x;
	rowsr.y = y;
	rowsr.w = bar.r.x - x;
	rowsr.h = h;
	rows = h / gl_hchar;
	cols = (rowsr.w - 4) / gl_wchar;
	if (cols > 100)
		cols = 100;
	bar.total = lg->list.n;
	bar.shown = rows;
	bar.top = top;
}

/* row k in its place, in the clip and with the mouse hidden by the caller; white_done: the list's
   white is already there (a whole redraw fills it once), only the cursor's row has a ground to fill */
static void draw_one(int k, int white_done)
{
	GRECT r;
	char buf[104];
	short fl = TX_LTR;
	if (k < top || k >= top + rows || k >= lg->list.n)
		return;
	r.x = rowsr.x;
	r.y = rowsr.y + (short)(k - top) * gl_hchar;
	r.w = rowsr.w;
	r.h = gl_hchar;
	if (k == cur) {
		fill(&r, 1);
		fl |= TX_INVERSE;
	} else if (!white_done) {
		fill(&r, 0);
	}
	langs_row(&lg->list.l[ord[k]], names[ord[k]], cols, buf);
	text_at(rowsr.x + 2, r.y, buf, cols, cols, fl);
}

/* rows from..to (inclusive) drawn in one go: the mouse hidden once, the clip the whole list's (a row's own
   clip would end where its text does, which TOS's VDI draws the slow way). b < 0 for one row */
static void rows_go(int from, int to, int second)
{
	GRECT t;
	int k;
	t.x = rowsr.x;
	t.y = rowsr.y;
	t.w = rowsr.w;
	t.h = rowsr.h;
	hide();
	if (clip(&t)) {
		for (k = from; k <= to; k++)
			draw_one(k, 0);
		if (second >= 0)
			draw_one(second, 0);
		clip_off();
	}
	show();
}

static void rows_between(int from, int to)
{
	if (from < top)
		from = top;
	if (to >= top + rows)
		to = top + rows - 1;
	if (to >= lg->list.n)
		to = lg->list.n - 1;
	if (from <= to)
		rows_go(from, to, -1);
}

static void rows_draw_two(int a, int b)
{
	rows_go(a, a, b);
}

static void row_draw(int k)
{
	rows_between(k, k);
}

static void bar_draw(const GRECT *part)
{
	GRECT c = *part;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return;
	hide();
	pb_draw(&bar, &c);
	show();
}

/* the whole list's rows: the white filled once, then each row's letters (and the cursor's black) */
static void rows_draw(void)
{
	GRECT t = rowsr;
	int k;
	hide();
	if (clip(&t)) {
		fill(&rowsr, 0);
		for (k = top; k < top + rows && k < lg->list.n; k++)
			draw_one(k, 1);
		clip_off();
	}
	show();
}

/* objects first..last of the dialog drawn again (the strings under them cleared by the root's own
   ground: 3D or flat), within what the dialog's window may draw. The clip reaches two pixels past
   the last line: TOS's VDI draws a string the slow way (some 40 ms of an 8 MHz ST for the total line)
   when the clip's edge is nearer than that to the letters; the layout leaves the pixels free */
static void box_draw(short first, short last)
{
	GRECT c;
	short x, y;
	origin(first, &x, &y);
	c.x = x;
	c.y = y;
	c.w = ltree[first].ob_width;
	c.h = ltree[last].ob_y + ltree[last].ob_height - ltree[first].ob_y + 2;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return;
	hide();
	if (!dlg_in_window() && !(dlg_3d_ok() && dlg_3d_on)) {
		/* over TOS's screen in the flat look the ground is white: the lines by our own drawing
		   (some 8 ms of an 8 MHz ST, where the AES took 25 for the total line) */
		short o;
		if (clip(&c)) {
			for (o = first; o <= last; o++) {
				GRECT r;
				const char *t = (const char *)ltree[o].ob_spec;
				origin(o, &r.x, &r.y);
				r.w = ltree[o].ob_width;
				r.h = ltree[o].ob_height;
				fill(&r, 0);
				if (t[0])
					text_at(r.x, r.y, t, (long)strlen(t), r.w / gl_wchar, TX_LTR);
			}
			clip_off();
		}
	} else {
		objc_draw(ltree, 0, 8, c.x, c.y, c.w, c.h);
	}
	show();
}

static void total_draw(void)
{
	langs_total_line(&lg->list, tot_text, sizeof(tot_text));
	box_draw(o_tot, o_tot);
}

static void msg_set(const char *s)
{
	if (!strcmp(msg_text, s))
		return;
	put(msg_text, s, sizeof(msg_text));
	langs_wrap(msg_text, LD_COLS, msg_l1, msg_l2);
	box_draw(o_msg, o_msg2);
}

/* the whole list again: when the AES asks for a piece, and after a scroll */
static void list_all(void)
{
	GRECT all;
	MARK("(list_all)");
	rows_draw();
	MARK("list: rows");
	all = bar.r;
	bar_draw(&all);
	MARK("list: bar");
}

static void more(short x, short y, short w, short h)
{
	(void)x, (void)y, (void)w, (void)h;
	place();
	list_all();
}

/* the rows moved d rows up the screen (d > 0: top went down by d) or down (d < 0) by the VDI's screen copy,
   the rows that come into view drawn after: some 50 ms of an 8 MHz ST for a row, where drawing them all
   took 70 (the guidelines' Speed: a row scrolled, 50 ms). Only over TOS's own screen, where the whole
   list is ours to move: in a window another program's window may cover a part of it. 1 when it did */
extern void scr_rows(short h, const short *p) __attribute__((weak));

static int move_rows(int d)
{
	short p[8], dy;
	GRECT t;
	MFDB scr;
	if (dlg_in_window() || d == 0 || d >= rows || d <= -rows)
		return 0;
	dy = (short)(d * gl_hchar);
	t.x = rowsr.x;
	t.y = rowsr.y;
	t.w = rowsr.w;
	t.h = rows * gl_hchar;
	p[0] = p[4] = t.x;
	p[2] = p[6] = (short)(t.x + t.w - 1);
	if (d > 0) {
		p[1] = (short)(t.y + dy);
		p[3] = (short)(t.y + t.h - 1);
		p[5] = t.y;
		p[7] = (short)(t.y + t.h - 1 - dy);
	} else {
		p[1] = t.y;
		p[3] = (short)(t.y + t.h - 1 + dy);
		p[5] = (short)(t.y - dy);
		p[7] = (short)(t.y + t.h - 1);
	}
	hide();
	clip_on(&t);
	if (scr_rows) {
		scr_rows(vdi_h, p);
	} else {
		memset(&scr, 0, sizeof(scr));
		vro_cpyfm(vdi_h, 3, p, &scr, &scr);
	}
	clip_off();
	show();
	return 1;
}

/* top becomes nt (and the cursor row cur from old_cur): the list shows it, the slider moves */
static void top_to(int nt, int old_cur)
{
	int ot = top, d = nt - ot;
	GRECT m;
	top = nt;
	bar.top = top;
	if (move_rows(d)) {
		/* the rows that came into view, and the cursor's old and new rows (the old one went with
		   the picture, black) */
		GRECT t;
		int k, from = d > 0 ? top + rows - d : top, to = d > 0 ? top + rows - 1 : top - d - 1;
		t.x = rowsr.x;
		t.y = rowsr.y;
		t.w = rowsr.w;
		t.h = rowsr.h;
		hide();
		if (clip(&t)) {
			for (k = from; k <= to; k++)
				draw_one(k, 0);
			if (old_cur != cur) {
				if (old_cur >= top && old_cur < top + rows && (old_cur < from || old_cur > to))
					draw_one(old_cur, 0);
				if (cur >= top && cur < top + rows && (cur < from || cur > to))
					draw_one(cur, 0);
			}
			clip_off();
		}
		show();
	} else {
		rows_draw();
	}
	m = pb_moved(&bar, ot);
	bar_draw(&m);
}

/* the cursor to row k, kept in sight: the old and the new row drawn, or the list scrolled */
static void cursor_to(int k)
{
	int old = cur, nt;
	if (k < 0 || k >= lg->list.n || k == cur)
		return;
	cur = k;
	nt = langs_show(top, cur, rows, lg->list.n);
	if (nt != top)
		top_to(nt, old);
	else
		rows_draw_two(old, cur);
}

static int scroll_by(long n)
{
	int nt = langs_clamp_top((int)(top + n), rows, lg->list.n);
	if (nt == top)
		return 0;
	top_to(nt, cur);
	return 1;
}

static int scroll_to(long t)
{
	return scroll_by(t - top);
}

/* a tick (Space, or the release of a click) on row k */
static void tick(int k)
{
	int r;
	if (k < 0 || k >= lg->list.n)
		return;
	r = langs_toggle(&lg->list, ord[k]);
	TRACE("LANGS tick %s %d\n", lg->list.l[ord[k]].code, r);
	switch (r) {
	case LT_TICKED:
	case LT_UNTICKED:
		hide();
		row_draw(k);
		total_draw();
		msg_set("");
		show();
		break;
	case LT_REFUSED: {
		char m[sizeof(msg_text)];
		langs_over_line(&lg->list, ord[k], names[ord[k]], m, sizeof(m));
		msg_set(m);
		break;
	}
	case LT_FIXED:
		msg_set("English is always installed.");
		break;
	}
}

/* ---- the waits: the list asked for, the request sent ---- */

static int own_idle;
#ifdef LANGS_TRACE
static unsigned long idle_end;		/* the tos_ms at the end of the last idle turn: the last stretch's start */
#endif

static void stop_idle(void)
{
	short msg[8], which;
	AEVENT ev;
	which = evnt_multi_((short)(MU_KEYBD | MU_TIMER | (lg->message ? MU_MESAG : 0)), 0, 0, 0, 5, msg, &ev);
	if ((which & MU_MESAG) && lg->message)
		lg->message(msg);
	if (which & MU_KEYBD) {
		short sc = (short)((ev.kreturn >> 8) & 0xff);
		if (sc == 0x01 || sc == 0x61)	/* Esc, Undo */
			conn_stop = 1;
	}
#ifdef LANGS_TRACE
	idle_end = tos_ms();
#endif
}

static void wait_on(langs *l)
{
	conn_stop = 0;
	if (l->wait)
		l->wait(1);
	else
		graf_mouse(BUSYBEE, 0);
	if (!pf_idle) {
		pf_idle = stop_idle;
		own_idle = 1;
	}
}

static void wait_off(langs *l)
{
	if (own_idle) {
		pf_idle = 0;
		own_idle = 0;
	}
	if (l->wait)
		l->wait(0);
	else
		graf_mouse(ARROW, 0);
}

/* an event call, no wait: the names and the order (above) end a stretch, the dialog's drawing begins another
   (the AES's messages, if the app takes them, given as in any wait) */
static void yield_turn(langs *l)
{
	short msg[8], which;
	AEVENT ev;
	which = evnt_multi_((short)(MU_TIMER | (l->message ? MU_MESAG : 0)), 0, 0, 0, 0, msg, &ev);
	if ((which & MU_MESAG) && l->message)
		l->message(msg);
}

/* 1 the list came, 0 stopped by the user, -1 failed (the user has been told) */
static int ask_list(langs *l)
{
	int ok;
	wait_on(l);
	MARK("(get)");
	ok = translate_langs_get(l->host ? l->host : "", l->port, &l->list, l->err, sizeof(l->err));
	TRACE("LANGS ms after the last idle turn %lu\n", (unsigned long)(tos_ms() - idle_end));
	MARK("get: the request, with its waits");
	wait_off(l);
	MARK("get: wait_off");
	if (!ok && conn_stop) {
		conn_stop = 0;
		return 0;
	}
	conn_stop = 0;
	if (!ok) {
		alert(1, "[3][The Gateway's languages|can't be had: %s][ OK ]", l->err);
		return -1;
	}
	return 1;
}

/* OK with something changed: the one request. 1 sent (job, add_mb), 0 refused or stopped (said) */
static int send_set(langs *l)
{
	char codes[512], why[sizeof(l->err)];
	int ok, job = 0, i, add = 0;
	if (!translate_langs_codes(&l->list, codes, sizeof(codes))) {
		alert(1, "[3][Too many languages|to send to the Gateway.][ OK ]");
		return 0;
	}
	for (i = 0; i < l->list.n; i++)
		if (l->list.l[i].want && !l->list.l[i].installed && strcmp(l->list.l[i].code, "en"))
			add += l->list.l[i].mb;
	msg_set("Asking the Gateway...");
	wait_on(l);
	TRACE("LANGS post %s\n", codes);
	ok = translate_langs_set(l->host ? l->host : "", l->port, codes, &job, why, sizeof(why));
	wait_off(l);
	msg_set("");
	if (!ok && conn_stop) {
		conn_stop = 0;
		return 0;
	}
	conn_stop = 0;
	if (!ok) {
		alert(1, "[3][The languages can't be|changed: %s.|Change the ticks or cancel.][ OK ]", why);
		return 0;
	}
	l->job = job;
	l->add_mb = add;
	return 1;
}

/* ---- the user ---- */

/* a key; 1 OK, -1 Cancel, 0 on */
static int on_key(short k, short kstate)
{
	short sc = (k >> 8) & 0xff, as = k & 0xff, shift = kstate & (K_LSHIFT | K_RSHIFT);
	/* the shifted Up, Down and ClrHome give a character ('8', '2', '7'): a key typed
	   while busy comes with kstate 0 (as fontsel.c) */
	if ((sc == 0x48 || sc == 0x50 || sc == 0x47) && as)
		shift = 1;
	if (dlg_esc(k))
		return -1;
	if (as == 0x0d || sc == 0x72 || sc == 0x1c)
		return 1;
	msg_set("");
	switch (sc) {
	case 0x48:
	case 0x50:
	case 0x47:
		cursor_to(langs_move(cur, lg->list.n, rows, sc, shift));
		return 0;
	}
	if (as == ' ') {
		tick(cur);
		return 0;
	}
	if ((as >= 'a' && as <= 'z') || (as >= 'A' && as <= 'Z')) {
		int j = langs_find_letter(ord, (const langs_nm *)names, lg->list.n, cur, as);
		if (j >= 0)
			cursor_to(j);
	}
	return 0;
}

static int rect_in(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* a button's press as GEM's: drawn pressed, its action on the release over it */
static int button(short o)
{
	short x, y, b, k;
	graf_mkstate(&x, &y, &b, &k);
	if (!(b & 1))
		return 1;		/* (let go already) */
	return graf_watchbox(ltree, o, SELECTED, 0);
}

/* a press of the left button; 1 OK, -1 Cancel, 0 on */
static int on_click(short mx, short my)
{
	short o;
	if (rect_in(&bar.r, mx, my)) {
		msg_set("");
		pb_click(&bar, mx, my, scroll_by, scroll_to);
		return 0;
	}
	if (rect_in(&rowsr, mx, my)) {
		int k = top + (my - rowsr.y) / gl_hchar;
		GRECT box;
		short dx, dy;
		msg_set("");
		if (k >= lg->list.n || k >= top + rows)
			return 0;
		cursor_to(k);		/* the press: the row is the chosen one */
		box.x = rowsr.x;
		box.y = rowsr.y + (short)(k - top) * gl_hchar;
		box.w = rowsr.w;
		box.h = gl_hchar;
		if (press_track(&box, mx, my, 0, 0, &dx, &dy) == PRESS_IN)
			tick(k);	/* the release over the row: the tick */
		return 0;
	}
	o = objc_find(ltree, 0, 8, mx, my);
	if (o == o_ok || o == o_cancel) {
		int hit = button(o);
		ltree[o].ob_state &= ~SELECTED;
		dlg_draw(ltree, o);
		if (hit)
			return o == o_ok ? 1 : -1;
	}
	return 0;
}

int langs_run(langs *l)
{
	short x, y, w, h, m[8], i, k;
	int done = 0, r;
	AEVENT e;

	lg = l;
	l->job = 0;
	l->add_mb = 0;
	l->err[0] = 0;
	r = ask_list(l);
	if (r <= 0)
		return r;
	MARK("(list asked)");
	/* the names and the order (not drawing) in their own stretch, apart from the dialog's drawing:
	   an event call between (the guidelines' Speed: work that isn't drawing is not part of a draw's stretch) */

	/* the names as drawn, and the order (English, then by name) */
	for (i = 0; i < l->list.n; i++)
		langs_name(&l->list.l[i], names[i]);
	langs_sort(&l->list, (const langs_nm *)names, ord);
	cur = l->list.n ? 0 : -1;
	top = 0;
	tot_text[0] = 0;
	msg_text[0] = msg_l1[0] = msg_l2[0] = 0;
	langs_total_line(&l->list, tot_text, sizeof(tot_text));
	MARK("names, order");
	yield_turn(l);

	font_sys_push();
	/* 48 x 18 cells (the Font dialog's 17 and a line for the message): the lists a few pixels down, so no label loses its
	   descenders and the list loses no row */
	tr_begin(48, 18);
	o_band = tr_add(G_BOXTEXT, 0, 0, (long)&band_ted, 0, 0, 48, 1);
	tr_add(G_STRING, 0, 0, (long)"Languages on the Gateway", 2, 1, 24, 1);
	o_list = tr_add(G_BOX, 0, 0, 0x00FF1100L, 2, 2, 44, 10);
	/* the total and the message are G_STRINGs the AES draws on the dialog's own ground (3D or
	   flat, 5.6), whatever the app's text_at does with the ground behind a letter */
	o_tot = tr_add(G_STRING, 0, 0, (long)tot_text, 2, 12, 44, 1);
	o_msg = tr_add(G_STRING, 0, 0, (long)msg_l1, 2, 13, 44, 1);
	o_msg2 = tr_add(G_STRING, 0, 0, (long)msg_l2, 2, 14, 44, 1);
	/* Cancel then OK, OK at the right, 10 wide each (5.10, D26: Settings' W - 24 and W - 12) */
	o_cancel = tr_add(G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 24, 16, 10, 1);
	o_ok = tr_add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 36, 16, 10, 1);
	tr_end();
	ltree[o_list].ob_y += 3;
	ltree[o_list].ob_height -= 3;
	ltree[o_tot].ob_y += 5;
	ltree[o_msg].ob_y += 9;		/* (4 pixels between the lines: box_draw's clip) */
	ltree[o_msg2].ob_y += 9;
	ltree[o_ok].ob_y += 12;
	ltree[o_cancel].ob_y += 12;
	ltree[0].ob_height += 14;
	bar.across = 0;
	MARK("tree built");

	dlg_name("Languages");
	dlg_band(o_band);		/* in a window its title says it (3.5) */
	dlg_open(ltree, &x, &y, &w, &h);
	MARK("dlg_open");
	place();
#ifdef LANGS_TRACE
	{
		/* by hand: snprintf took 30 ms of an 8 MHz ST here, in the stretch the speed rig times */
		char tb[200], *p = tb;
		short v[17], bx, by, cx, cy, q;
		objc_offset(ltree, o_ok, &bx, &by);
		objc_offset(ltree, o_cancel, &cx, &cy);
		v[0] = rowsr.x; v[1] = rowsr.y; v[2] = rowsr.w; v[3] = rowsr.h;
		v[4] = bar.r.x; v[5] = bar.r.y; v[6] = bar.r.w; v[7] = bar.r.h;
		v[8] = bx; v[9] = by; v[10] = ltree[o_ok].ob_width; v[11] = ltree[o_ok].ob_height;
		v[12] = cx; v[13] = cy; v[14] = ltree[o_cancel].ob_width; v[15] = ltree[o_cancel].ob_height;
		v[16] = gl_hchar;
		memcpy(p, "LANGS open n=", 13);
		p = put_num(p + 13, l->list.n);
		memcpy(p, " cols=", 6);
		p = put_num(p + 6, cols);
		*p++ = '\n';
		*p = 0;
		nf_debug(tb);
		p = tb;
		memcpy(p, "LANGS geom rows", 15);
		p += 15;
		for (q = 0; q < 17; q++) {
			const char *w = q == 4 ? " bar" : q == 8 ? " ok" : q == 12 ? " cancel" : q == 16 ? " row" : "";
			size_t wl = strlen(w);
			memcpy(p, w, wl);
			p += wl;
			*p++ = ' ';
			if (v[q] < 0) {
				*p++ = '-';
				p = put_num(p, -v[q]);
			} else {
				p = put_num(p, v[q]);
			}
		}
		*p++ = '\n';
		*p = 0;
		nf_debug(tb);
	}
#endif
	if (!dlg_in_window()) {
		/* (over the screen: the tree is drawn; the list and the lines with it. In a window the
		   AES's redraw asks for them: more) */
		list_all();
	}
	MARK("list drawn");
	dlg_more(more);

	while (!done) {
		short flags = MU_KEYBD | MU_BUTTON;
		unsigned long t = 0;
		if (l->message) {
			flags |= MU_MESAG | MU_TIMER;
			t = 250;
		}
		/* a press when the button is up (5.8: one click asked for) */
		dlg_evnt(flags, 1, 1, 1, t, m, &e);
		place();			/* the window may have moved */
		if ((e.which & MU_MESAG) && l->message)
			l->message(m);
		if (e.which & MU_KEYBD)
			done = on_key(e.kreturn, e.kstate);
		if ((e.which & MU_BUTTON) && !done)
			done = on_click(e.mx, e.my);
		if ((e.which & MU_TIMER) && !(e.which & (MU_KEYBD | MU_BUTTON)) && l->message)
			l->message(0);
		if (done > 0 && translate_langs_changed(&l->list) && !send_set(l))
			done = 0;		/* refused or stopped: the dialog stays, the user was told */
	}
	dlg_close();
	clip_off();			/* no clip left on vdi_h: NVDI clips raster copies with it */
	font_sys_pop();
	k = done > 0;
	TRACE("LANGS close %d job %d\n", k, l->job);
	return k;
}

#endif
