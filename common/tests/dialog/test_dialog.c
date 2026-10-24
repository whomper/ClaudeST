/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The shared dialog loop (ui/dialog.c) on the build machine, against a
 * stand-in AES that does what TOS's does: form_keybd takes Return, Enter,
 * Tab and the arrows, form_button selects and exits, objc_edit types into
 * the field and, as TOS's and EmuTOS's do, clears it on Esc. Each case
 * is a script of keys and clicks.
 *   make test
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gem.h"
#include "dialog.h"
#include "clip.h"
#include "field.h"

#define KEY_RETURN 0x1c0d
#define KEY_ESC    0x011b
#define KEY_UNDO   0x6100
#define KEY_TAB    0x0f09
#define KEY_CTRLBR 0x1a1b	/* ^[ : ASCII's Esc */
#define K(c)       ((short)(0x1e00 | (c)))	/* a letter (its scan code doesn't matter here) */
#define CLICK(o)   { MU_BUTTON, 0, (o), 1 }
#define DCLICK(o)  { MU_BUTTON, 0, (o), 2 }
#define KEY(k)     { MU_KEYBD, (k), 0, 0 }
#define END        { 0, 0, 0, 0 }

typedef struct {
	short which, key, obj, clicks;
} STEP;

/* the dialog: a title, two fields, OK (default), Cancel, a list row
   that answers a double click (TOUCHEXIT) */
enum { ROOT, TITLE, F1, F2, OK, CANCEL, ROW, NOBJ };
static OBJECT tree[NOBJ];
static TEDINFO ted[2];
static char text[2][32];

static const STEP *script;
static short pre_keys[4], npre;		/* typed before the dialog came */
static jmp_buf out;
static int failed, checks;
static int updates, update_low, mouse_off, cursor, ed_char_esc, bells, draws;
static int dial_start, dial_finish, blits, opened, closed, vwk_fails;
static short planes = 1, blit_dir[2];
static long blit_size;
static void *blit_buf;

static void setup(void)
{
	static const short at[NOBJ][4] = {
		{ 0, 0, 320, 160 }, { 16, 8, 200, 16 }, { 16, 32, 200, 16 }, { 16, 56, 200, 16 },
		{ 16, 120, 80, 16 }, { 120, 120, 80, 16 }, { 16, 88, 200, 16 },
	};
	static const unsigned short type[NOBJ] = { G_BOX, G_STRING, G_FTEXT, G_FTEXT, G_BUTTON, G_BUTTON, G_STRING };
	static const unsigned short flags[NOBJ] = {
		0, 0, EDITABLE, EDITABLE, SELECTABLE | DEFAULT | EXIT, SELECTABLE | EXIT, TOUCHEXIT | LASTOB,
	};
	short i;
	memset(tree, 0, sizeof(tree));
	for (i = 0; i < NOBJ; i++) {
		tree[i].ob_next = i + 1 < NOBJ ? i + 1 : 0;
		tree[i].ob_head = tree[i].ob_tail = -1;
		tree[i].ob_type = type[i];
		tree[i].ob_flags = flags[i];
		tree[i].ob_x = at[i][0];
		tree[i].ob_y = at[i][1];
		tree[i].ob_width = at[i][2];
		tree[i].ob_height = at[i][3];
	}
	tree[ROOT].ob_next = -1;
	tree[ROOT].ob_head = 1;
	tree[ROOT].ob_tail = NOBJ - 1;
	tree[OK].ob_spec = (long)"OK";
	tree[CANCEL].ob_spec = (long)"Cancel";
	strcpy(text[0], "hello");
	strcpy(text[1], "world");
	for (i = 0; i < 2; i++) {
		ted[i].te_ptext = text[i];
		ted[i].te_txtlen = 20;
		tree[F1 + i].ob_spec = (long)&ted[i];
	}
	updates = update_low = mouse_off = cursor = ed_char_esc = bells = draws = 0;
	dial_start = dial_finish = blits = opened = closed = vwk_fails = 0;
	npre = 0;
	planes = 1;
}

/* ---------------- the stand-in AES ---------------- */

void fake_bconout(short dev, short c)
{
	if (dev == 2 && c == 7)
		bells++;
}

void nf_debug(const char *s)
{
	(void)s;
}

/* an AES with 3D objects (AES 4.1, appl_getinfo(13) says so) */
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

long get_cookie(long id)
{
	(void)id;
	return 0;
}

short wind_update(short mode)
{
	updates += mode == BEG_UPDATE || mode == 3 ? 1 : -1;
	if (updates < update_low)
		update_low = updates;
	return 1;
}

short evnt_multi_(short flags, short clicks, short mask, short state,
		  unsigned long timer, short *msg, AEVENT *ev)
{
	(void)clicks, (void)mask, (void)state, (void)timer, (void)msg;
	memset(ev, 0, sizeof(*ev));
	if (flags & MU_TIMER) {
		if (npre) {
			ev->which = MU_KEYBD;
			ev->kreturn = pre_keys[--npre];
		} else {
			ev->which = MU_TIMER;
		}
		return ev->which;
	}
	if (!script->which)
		longjmp(out, 1);		/* still waiting when the script ended */
	ev->which = script->which;
	ev->kreturn = script->key;
	ev->mx = script->obj;
	ev->breturn = script->clicks;
	script++;
	return ev->which;
}

/* the clipboard (clip.c) */
static char clipboard[256];
static int clip_puts;

int clip_key(short key)
{
	switch (key & 0xff) {
	case 3: return CLIP_COPY;
	case 24: return CLIP_CUT;
	case 22: return CLIP_PASTE;
	}
	return CLIP_NONE;
}

int clip_put(const char *text, long n)
{
	clip_puts++;
	memcpy(clipboard, text, (size_t)n);
	clipboard[n] = 0;
	return 1;
}

long clip_get(char *buf, long size)
{
	strncpy(buf, clipboard, (size_t)size - 1);
	buf[size - 1] = 0;
	return (long)strlen(buf);
}

char *clip_line(char *buf)
{
	char *e = strchr(buf, '\n');
	if (e)
		*e = 0;
	return buf;
}

static short find(short from, short inc, unsigned short flag)
{
	short i = from + inc;
	while (i >= 0 && i < NOBJ) {
		if (tree[i].ob_flags & flag)
			return i;
		i += inc;
	}
	return from;
}

short form_keybd(OBJECT *t, short obj, short next, short ch, short *onext, short *och)
{
	short scan = (ch >> 8) & 0xff;
	(void)t;
	*och = 0;
	*onext = next;
	if (scan == 0x1c || scan == 0x72) {
		*onext = find(-1, 1, DEFAULT);
		if (*onext > 0) {
			tree[*onext].ob_state |= SELECTED;
			return 0;
		}
		*onext = 0;
	} else if (scan == 0x0f || scan == 0x50) {
		*onext = find(obj, 1, EDITABLE);
	} else if (scan == 0x48) {
		*onext = find(obj, -1, EDITABLE);
	} else {
		*och = ch;
	}
	return 1;
}

short form_button(OBJECT *t, short obj, short clicks, short *onext)
{
	short cont = 1, high = 0;
	(void)t;
	if (tree[obj].ob_flags & TOUCHEXIT) {
		if (clicks == 2)
			high = (short)0x8000;
		cont = 0;
	}
	if (tree[obj].ob_flags & SELECTABLE)
		tree[obj].ob_state ^= SELECTED;
	if ((tree[obj].ob_state & SELECTED) && (tree[obj].ob_flags & EXIT))
		cont = 0;
	if (cont && !(tree[obj].ob_flags & EDITABLE))
		obj = 0;
	*onext = obj | high;
	return cont;
}

short objc_edit(OBJECT *t, short obj, short ch, short *idx, short kind)
{
	char *s;
	if (obj != F1 && obj != F2) {
		checks++, failed++;
		printf("FAIL objc_edit on object %d, not a field\n", obj);
		return 0;
	}
	s = ((TEDINFO *)t[obj].ob_spec)->te_ptext;
	if (kind == ED_INIT) {
		*idx = (short)strlen(s);
		cursor++;
	} else if (kind == ED_END) {
		cursor--;
	} else if (kind == ED_CHAR) {
		if (ch == KEY_ESC || (ch & 0xff) == 0x1b) {
			ed_char_esc++;
			s[0] = 0;		/* what TOS does with Esc in a field */
			*idx = 0;
		} else if (ch == 0x4b00) {	/* the cursor left */
			if (*idx > 0)
				(*idx)--;
		} else if ((ch & 0xff) >= ' ' && *idx < 30) {
			s[(*idx)++] = (char)ch;
			s[*idx] = 0;
		}
	}
	return 1;
}

short objc_find(OBJECT *t, short start, short depth, short x, short y)
{
	(void)t, (void)start, (void)depth, (void)y;
	return x;			/* the script's object, -1 for outside */
}

short objc_offset(OBJECT *t, short obj, short *x, short *y)
{
	*x = t[0].ob_x + (obj ? t[obj].ob_x : 0);
	*y = t[0].ob_y + (obj ? t[obj].ob_y : 0);
	return 1;
}

short objc_draw(OBJECT *t, short start, short depth, short x, short y, short w, short h)
{
	(void)t, (void)start, (void)depth, (void)x, (void)y, (void)w, (void)h;
	draws++;
	return 1;
}

short form_center(OBJECT *t, short *x, short *y, short *w, short *h)
{
	t[0].ob_x = 160;
	t[0].ob_y = 120;
	*x = 160;
	*y = 120;
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

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
	*wchar = 8;
	*hchar = 16;
	*wbox = 10;
	*hbox = 19;
	return 1;		/* the physical workstation */
}

short graf_mouse(short num, void *form)
{
	(void)form;
	mouse_off += num == M_OFF ? 1 : num == M_ON ? -1 : 0;
	return 1;
}

short v_opnvwk_(short phys, short *work_out)
{
	(void)phys;
	work_out[0] = 639;
	work_out[1] = 399;
	if (vwk_fails)
		return 0;
	opened++;
	return 7;
}

void v_clsvwk(short h)
{
	(void)h;
	closed++;
}

void vq_extnd(short h, short owflag, short *work_out)
{
	(void)h, (void)owflag;
	work_out[4] = planes;
}

void vro_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst)
{
	MFDB *m = src->fd_addr ? src : dst;
	long need;
	(void)h, (void)mode;
	blit_dir[blits++ & 1] = src->fd_addr == 0;	/* 1: from the screen */
	need = (long)m->fd_wdwidth * 2 * m->fd_h * m->fd_nplanes;
	blit_size = need;
	blit_buf = m->fd_addr;
	if (m->fd_nplanes != planes || pxy[6] - pxy[4] != pxy[2] - pxy[0] ||
	    pxy[7] - pxy[5] != pxy[3] - pxy[1])
		blit_size = -1;
	memset(m->fd_addr, 0x55, need);	/* AddressSanitizer: the whole of it is ours */
}

/* ---------------- the cases ---------------- */

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failed++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

/* runs the loop on a script; -999 when the script ended first */
static short run(short start, short cancel, const STEP *s)
{
	script = s;
	if (setjmp(out))
		return -999;
	return dlg_do(tree, start, cancel);
}

static void tidy(const char *what)
{
	char b[160];
	snprintf(b, sizeof(b), "%s: the cursor gone, the screen and mouse given back", what);
	check(cursor == 0 && updates == 0 && update_low >= 0 && mouse_off == 0, b);
}

/* ---------------- fields that hold more than they show (field.c) ---------------- */

#define TOKEN "github_pat_11ABCDEFG0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789a"

static void test_long_fields(void)
{
	static char big[100], tmpl[] = "____________________";	/* shows 20 */
	static STEP s[120];
	LFIELD f, g;
	short r, i, n = 0;

	/* the editing alone */
	memset(&g, 0, sizeof(g));
	strcpy(big, "abc");
	g.text = big;
	g.cap = 5;
	g.show = 3;
	lf_end(&g);
	check(g.cur == 3 && !strcmp(g.view, "abc"), "lf: the cursor at the end, the text shown");
	lf_edit(&g, K('d'), 'd', 0);
	check(!strcmp(big, "abcd") && !strcmp(g.view, "bcd") && g.off == 1, "lf: typing past the width scrolls");
	lf_edit(&g, K('e'), 'e', 0);
	lf_edit(&g, K('f'), 'f', 0);
	check(!strcmp(big, "abcde"), "lf: no more than it holds");
	lf_edit(&g, 0x4b00, 0, 1);
	check(g.cur == 0 && g.off == 0 && !strcmp(g.view, "abc"), "lf: Shift+Left to the start, shown from there");
	lf_edit(&g, 0x5300, 0, 0);
	check(!strcmp(big, "bcde") && g.cur == 0, "lf: Delete takes the letter at the cursor");
	lf_edit(&g, 0x4d00, 0, 1);
	lf_edit(&g, 0x0e08, 8, 0);
	check(!strcmp(big, "bcd") && g.cur == 3 && !strcmp(g.view, "bcd"), "lf: Backspace at the end");
	lf_edit(&g, 0x4700, 0, 0);
	lf_edit(&g, K('Z'), 'Z', 0);
	check(!strcmp(big, "Zbcd") && g.cur == 1 && g.off == 0, "lf: ClrHome, then typing in front");
	check(!lf_edit(&g, KEY_RETURN, 13, 0) && !lf_edit(&g, KEY_TAB, 9, 0) && !lf_edit(&g, 0x3b00, 0, 0),
	      "lf: Return, Tab and a function key are the dialog's");
	g.mask = '*';
	lf_view(&g);
	check(!strcmp(g.view, "***"), "lf: a password shows stars");
	lf_edit(&g, 0x4b00, 0, 0);
	lf_edit(&g, 0x4b00, 0, 0);
	check(g.cur == 0, "lf: Left stops at the start");
	g.mask = 0;
	lf_edit(&g, 0x4d36, '6', 0);
	check(g.cur == 4 && !strcmp(big, "Zbcd"), "lf: Shift+Right by its '6' alone (TOS 1.04: kstate 0), to the end");
	lf_edit(&g, 0x4b34, '4', 0);
	check(g.cur == 0, "lf: Shift+Left by its '4' alone, to the start");
	lf_edit(&g, 0x4737, '7', 0);
	check(g.cur == 4 && !strcmp(big, "Zbcd"), "lf: Shift+ClrHome by its '7' alone, to the end");

	/* a 93-character token typed into a field 20 wide, in dlg_do */
	setup();
	ted[0].te_ptmplt = tmpl;
	big[0] = 0;
	lf_init(&f, tree, F1, big, 99, 0);
	check(lf_of(tree, F1) == &f && !lf_of(tree, F2) && ted[0].te_ptext == f.view && ted[0].te_txtlen == 21,
	      "lf_init: the field shows its view, 20 wide");
	for (i = 0; TOKEN[i]; i++)
		s[n++] = (STEP)KEY(K(TOKEN[i]));
	s[n++] = (STEP)KEY(KEY_RETURN);
	s[n] = (STEP)END;
	r = run(F1, CANCEL, s);
	check(r == OK && !strcmp(big, TOKEN) && strlen(TOKEN) == 93,
	      "a 93-character token typed into a field showing 20: all of it kept");
	check(!strcmp(f.view, TOKEN + 73) && f.cur == 93, "... the field shows its last 20, the cursor after them");
	tidy("a long field");

	/* the cursor moved back, a letter put in there, Esc keeps it all */
	setup();
	ted[0].te_ptmplt = tmpl;
	strcpy(big, TOKEN);
	lf_init(&f, tree, F1, big, 99, 0);
	n = 0;
	for (i = 0; i < 30; i++)
		s[n++] = (STEP)KEY(0x4b00);
	s[n++] = (STEP)KEY(K('#'));
	s[n++] = (STEP)KEY(KEY_ESC);
	s[n] = (STEP)END;
	r = run(F1, CANCEL, s);
	check(r == CANCEL && big[63] == '#' && !strcmp(big + 64, TOKEN + 63) && strlen(big) == 94,
	      "Left 30 times, then a letter: put in there; Esc cancels and the text stays");
	check(ed_char_esc == 0 && cursor == 0, "... Esc never reached objc_edit");

	/* the clipboard: a paste takes the whole token; a password is never copied out */
	setup();
	ted[0].te_ptmplt = tmpl;
	big[0] = 0;
	strcpy(clipboard, TOKEN "\nsecond line");
	clip_puts = 0;
	lf_init(&f, tree, F1, big, 99, '*');
	{
		static const STEP p[] = { KEY(0x2f16), KEY(0x2e03), KEY(0x2d18), KEY(KEY_RETURN), END };
		r = run(F1, CANCEL, p);
	}
	check(r == OK && !strcmp(big, TOKEN), "^V into a long password field: the whole first line");
	check(clip_puts == 0 && !strcmp(f.view, "********************"), "... ^C and ^X take nothing out of it, stars shown");
	lf_forget(tree);
	check(!lf_of(tree, F1), "lf_forget: the dialog's fields let go");
}

/* dlg_3d: the AES's 3D flags (0x0200 indicator, 0x0400 background, 0x0600
   activator) on each kind of object, none in a list (a white framed box)
   and none when the setting says flat */
static void test_3d(void)
{
	enum { T_BOX, T_LABEL, T_FIELD, T_OK, T_RADIO, T_TOGGLE, T_ARROW, T_CHAR, T_LINE, T_POPUP, T_PAGE, T_LIST,
		T_ROW, T_UP, N3 };
	static const unsigned short type[N3] = { G_BOX, G_STRING, G_FTEXT, G_BUTTON, G_BUTTON, G_BUTTON, G_BOXCHAR,
		G_BOXCHAR, G_BOX, G_BOXTEXT, G_BOXTEXT, G_BOX, G_STRING, G_BOXCHAR };
	static const unsigned short flags[N3] = { 0, 0, EDITABLE, SELECTABLE | DEFAULT | EXIT, SELECTABLE | RBUTTON,
		SELECTABLE, TOUCHEXIT, 0, 0, TOUCHEXIT, TOUCHEXIT, 0, TOUCHEXIT, TOUCHEXIT };
	/* the root, a separator line (black), a list (white, framed) */
	static const long spec[N3] = { 0x00021100L, 0, 0, 0, 0, 0, 0, 0, 0x00001171L, 0, 0, 0x00FF1100L, 0, 0 };
	/* a pop-up (framed) raised, a sidebar's page (no frame) on the ground */
	static const unsigned short want[N3] = { 0x0400, 0x0400, 0, 0x0600, 0x0200, 0x0200, 0x0600, 0x0400, 0x0400,
		0x0600, 0x0400, 0, 0, 0x0600 };
	static TEDINFO popup, page;
	OBJECT t[N3];
	int i, ok = 1;
	memset(t, 0, sizeof t);
	popup.te_thickness = -1;
	page.te_thickness = 0;
	for (i = 0; i < N3; i++) {
		t[i].ob_type = type[i];
		t[i].ob_flags = flags[i] | (i == N3 - 1 ? LASTOB : 0);
		t[i].ob_spec = i == T_POPUP ? (long)&popup : i == T_PAGE ? (long)&page : spec[i];
		t[i].ob_head = t[i].ob_tail = -1;
		/* the root's children: all but the list's row, in the list */
		t[i].ob_next = i == 0 ? -1 : i == T_LIST - 1 ? T_LIST : i == T_LIST ? T_UP : i == N3 - 1 ? 0 : i + 1;
	}
	t[T_ROW].ob_next = T_LIST;
	t[T_LIST].ob_head = t[T_LIST].ob_tail = T_ROW;
	t[0].ob_head = 1;
	t[0].ob_tail = N3 - 1;
	check(dlg_3d_ok(), "dlg_3d_ok: an AES 4.1 that has 3D objects");
	dlg_3d(t);
	for (i = 0; i < N3; i++)
		if ((t[i].ob_flags & 0x0600) != want[i] || (t[i].ob_flags & ~0x0600) != (flags[i] | (i == N3 - 1 ? LASTOB : 0)))
			ok = 0;
	check(ok, "dlg_3d: box, labels and a sidebar's page background, the field none, OK, a touch-exit box and a "
	      "pop-up activators, radio and toggle indicators");
	check(!(t[T_LIST].ob_flags & 0x0600) && !(t[T_ROW].ob_flags & 0x0600),
	      "... a list (a white framed box) and its rows none: they stay white");
	dlg_3d(t);
	for (i = 0; i < N3; i++)
		if ((t[i].ob_flags & 0x0600) != want[i])
			ok = 0;
	check(ok, "... the same again when a tree is shown again");
	dlg_3d_on = 0;
	dlg_3d(t);
	for (i = 0; i < N3; i++)
		if (t[i].ob_flags != (flags[i] | (i == N3 - 1 ? LASTOB : 0)))
			ok = 0;
	check(ok, "DIALOGS3D=0: every 3D flag taken off, the rest kept");
	dlg_3d_on = 1;
}

int main(void)
{
	short r;
	setvbuf(stdout, 0, _IOLBF, 0);

	test_3d();

	setup();
	{
		static const STEP s[] = { KEY(K('a')), KEY(K('b')), KEY(KEY_RETURN), END };
		r = run(0, CANCEL, s);
		check(r == OK, "typing, then Return: OK, the default");
		check(!strcmp(text[0], "helloab"), "typed into the first field");
		check(tree[OK].ob_state & SELECTED, "OK left selected, as form_do leaves it");
		tidy("Return");
	}

	setup();
	{
		static const STEP s[] = { KEY(K('x')), KEY(KEY_ESC), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL, "Esc in a text field: Cancel");
		check(!strcmp(text[0], "hellox"), "... and the field keeps its text (form_do would clear it)");
		check(ed_char_esc == 0, "... Esc never reached objc_edit");
		check(!(tree[CANCEL].ob_state & SELECTED), "... Cancel not selected");
		tidy("Esc");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_UNDO), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL && !strcmp(text[0], "hello") && !ed_char_esc, "Undo: Cancel, the field as it was");
		tidy("Undo");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_CTRLBR), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL && !strcmp(text[0], "hello"), "^[ (ASCII's Esc): Cancel too");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_TAB), KEY(K('z')), KEY(KEY_ESC), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL && !strcmp(text[1], "worldz") && !strcmp(text[0], "hello"),
		      "Tab to the second field, typing, Esc: Cancel, both fields kept");
		tidy("Tab");
	}

	setup();
	{
		static const STEP s[] = { KEY(K('q')), END };
		script = s;
		if (!setjmp(out))
			dlg_do(tree, F2, CANCEL);
		check(!strcmp(text[1], "worldq") && !strcmp(text[0], "hello"), "start = F2: the cursor starts there");
	}

	setup();
	tree[CANCEL].ob_state |= SELECTED;
	{
		static const STEP s[] = { KEY(KEY_ESC), END };
		draws = 0;
		r = run(0, CANCEL, s);
		check(r == CANCEL && !(tree[CANCEL].ob_state & SELECTED) && draws == 1,
		      "Esc with Cancel shown selected: deselected and drawn again");
	}

	setup();
	{
		static const STEP s[] = { KEY(K('k')), KEY(KEY_ESC), END };
		draws = 0;
		r = run(0, -1, s);
		check(r == -1, "cancel -1 (no do-nothing button): Esc gives -1, told apart from every button");
		check(!strcmp(text[0], "hellok") && !ed_char_esc, "... the field kept");
		check(draws == 0 && !(tree[OK].ob_state & SELECTED) && !(tree[CANCEL].ob_state & SELECTED),
		      "... nothing drawn or selected");
		tidy("cancel -1");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_UNDO), END };
		r = run(0, -1, s);
		check(r == -1, "cancel -1: Undo gives -1 too");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_ESC), END };
		script = s;
		r = dlg_modal(tree, 0, -1);
		check(r == -1 && dial_start == 1 && dial_finish == 1, "dlg_modal, cancel -1: Esc gives -1, the dialog put away");
		check(!(tree[OK].ob_state & SELECTED) && !(tree[CANCEL].ob_state & SELECTED), "... no object changed");
		tidy("dlg_modal cancel -1");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_UNDO), END };
		script = s;
		r = dlg_modal_save(tree, 0, -1);
		check(r == -1 && blits == 2 && closed == 1, "dlg_modal_save, cancel -1: -1, the screen put back");
		tidy("dlg_modal_save cancel -1");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_RETURN), END };
		script = s;
		r = dlg_modal(tree, 0, -1);
		check(r == OK, "dlg_modal, cancel -1: Return still gives the default");
	}

	setup();
	{
		static const STEP s[] = { CLICK(CANCEL), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL && (tree[CANCEL].ob_state & SELECTED), "a click on Cancel: Cancel, selected");
		tidy("click");
	}

	setup();
	{
		static const STEP s[] = { CLICK(F2), KEY(K('m')), CLICK(TITLE), KEY(K('n')), KEY(KEY_RETURN), END };
		r = run(0, CANCEL, s);
		check(r == OK && !strcmp(text[1], "worldmn") && !strcmp(text[0], "hello"),
		      "a click on the second field moves the cursor there; on the title it stays");
		tidy("field click");
	}

	setup();
	{
		static const STEP s[] = { CLICK(-1), KEY(KEY_RETURN), END };
		r = run(0, CANCEL, s);
		check(r == OK && bells == 1, "a click outside: the bell, and the dialog stays");
	}

	setup();
	{
		static const STEP s[] = { DCLICK(ROW), END };
		r = run(0, CANCEL, s);
		check(r == (short)(ROW | 0x8000), "a double click on a TOUCHEXIT row: its number with the high bit");
	}

	setup();
	{
		static const STEP s[] = { CLICK(ROW), END };
		r = run(0, CANCEL, s);
		check(r == ROW, "a single click on it: its number");
	}

	setup();
	pre_keys[0] = KEY_RETURN;
	npre = 1;
	{
		static const STEP s[] = { KEY(KEY_ESC), END };
		r = run(0, CANCEL, s);
		check(r == CANCEL && npre == 0, "a Return typed before the dialog came doesn't answer it");
	}

	/* the whole of it */
	setup();
	{
		static const STEP s[] = { DCLICK(ROW), END };
		script = s;
		r = dlg_modal(tree, 0, CANCEL);
		check(r == ROW, "dlg_modal: the object without the double-click bit");
		check(dial_start == 1 && dial_finish == 1 && blits == 0, "dlg_modal: form_dial START and FINISH");
		tidy("dlg_modal");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_RETURN), END };
		script = s;
		r = dlg_modal(tree, 0, CANCEL);
		check(r == OK && !(tree[OK].ob_state & SELECTED), "dlg_modal: OK, deselected after");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_ESC), END };
		script = s;
		r = dlg_modal(tree, 0, CANCEL);
		check(r == CANCEL && !strcmp(text[0], "hello"), "dlg_modal: Esc gives Cancel, the field kept");
	}

	setup();
	{
		static const STEP s[] = { KEY(KEY_ESC), END };
		script = s;
		r = dlg_modal_save(tree, 0, CANCEL);
		check(r == CANCEL, "dlg_modal_save: Esc gives Cancel");
		check(blits == 2 && blit_dir[0] == 1 && blit_dir[1] == 0 && dial_finish == 0,
		      "dlg_modal_save: the screen saved and put back, no redraw");
		check(blit_size == (long)((320 + 8 + 15) / 16) * 2 * (160 + 8), "... the box and its outline, one plane");
		check(opened == 1 && closed == 1, "... its own workstation, closed after");
		tidy("dlg_modal_save");
	}

	setup();
	planes = 16;
	{
		static const STEP s[] = { KEY(KEY_RETURN), END };
		script = s;
		r = dlg_modal_save(tree, 0, CANCEL);
		check(r == OK && blits == 2 && blit_size == (long)((320 + 8 + 15) / 16) * 2 * (160 + 8) * 16,
		      "dlg_modal_save in 16 planes (Falcon true colour): sixteen times the memory");
	}

	setup();
	vwk_fails = 1;
	{
		static const STEP s[] = { KEY(KEY_RETURN), END };
		script = s;
		r = dlg_modal_save(tree, 0, CANCEL);
		check(r == OK && blits == 0 && dial_finish == 1,
		      "dlg_modal_save without a workstation: form_dial's redraw instead");
		tidy("no workstation");
	}

	test_long_fields();

	check(dlg_esc(KEY_ESC) && dlg_esc(KEY_UNDO) && !dlg_esc(KEY_RETURN) && !dlg_esc(K('a')) &&
	      !dlg_esc(0x0e08), "dlg_esc: Esc and Undo only");

	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
