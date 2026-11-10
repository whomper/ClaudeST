/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * filepick.c - the family's file dialog, several files at once
 * (filepick.h; EPlay's Add files, Erez, 4 Nov 2026). The tree is the AES's
 * own objects, given the AES's 3D look by dlg_3d (dlg_open); the list in
 * its white well is drawn here, as the Font dialog draws its lists
 * (fontsel.c), through dlg_more and dlg_cut so that it is right in a
 * dialog window too. The folder's entries, their sort, type, mask and
 * selection are src/dirlist.c's (tests/dirlist).
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "ui.h"
#include "gemui.h"
#include "dialog.h"
#include "popup.h"
#include "click.h"
#include "dirlist.h"
#include "filepick.h"
#include "fsel.h"

/* the tree: 42 by 24 cells */
enum { O_ROOT, O_BAND, O_DRIVE, O_PATH, O_PATHT, O_UP, O_FOLDERS, O_TYPE, O_SORT, O_NAMEL, O_MASK, O_LIST, O_INFO,
       O_ALL, O_NONE, O_CANCEL, O_ADD, O_N };
static OBJECT tree[O_N];
static TEDINFO ted_band, ted_path, ted_mask, ted_drive, ted_folders, ted_type, ted_sort;
static char s_path[FP_PATH], s_mask[13], t_mask[13], v_mask[13], s_drive[8], s_folders[16], s_type[20],
	s_sort[16], s_info[48];

/* the pop-up buttons' widths in cells (5.7: the words, the arrow in the last cell) */
enum { W_DRIVE = 5, W_FOLDERS = 12, W_TYPE = 15, W_SORT = 11 };

static void obj_in(short p, short i, short type, short flags, short state, long spec, short x, short y, short w,
		   short h)
{
	OBJECT *o = &tree[i];
	o->ob_next = -1;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (i > 0) {
		if (tree[p].ob_tail >= 0)
			tree[tree[p].ob_tail].ob_next = i;
		else
			tree[p].ob_head = i;
		tree[p].ob_tail = i;
		o->ob_next = p;
	}
}

static void obj(short i, short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	obj_in(0, i, type, flags, state, spec, x, y, w, h);
}

static void ted(TEDINFO *t, char *text, char *tmpl, char *valid, short just, short color, short thick)
{
	memset(t, 0, sizeof(*t));
	t->te_ptext = text;
	t->te_ptmplt = tmpl;
	t->te_pvalid = valid;
	t->te_font = 3;			/* the system font */
	t->te_just = just;
	t->te_color = color;
	t->te_thickness = thick;
	t->te_txtlen = (short)strlen(text) + 1;
	t->te_tmplen = (short)strlen(tmpl) + 1;
}

static const FP_SPEC *spec;

/* a pop-up button's text (5.7, as Settings' set_popup makes it): a space, the words (cut to leave a cell
   before the arrow), the system font's down arrow in the last of w cells */
static void pop_label(char *buf, short w, const char *words)
{
	short n = (short)strlen(words);
	if (n > w - 3)
		n = (short)(w - 3);
	memset(buf, ' ', (size_t)w);
	buf[w] = 0;
	memcpy(buf + 1, words, (size_t)n);
	buf[w - 1] = 2;
}

/* ... and the object: an outlined G_BOXTEXT, left-justified, TOUCHEXIT (not a G_BUTTON: C9) */
static void pop_button(short o, TEDINFO *t, char *buf, short w, short x, short y, const char *words)
{
	pop_label(buf, w, words);
	ted(t, buf, "", "", 0, 0x1180, -1);
	obj(o, G_BOXTEXT, TOUCHEXIT, 0, (long)t, x, y, w, 1);
}

static void build(void)
{
	memset(tree, 0, sizeof(tree));
	obj(O_ROOT, G_BOX, 0, OUTLINED, 0x00021100L, 0, 0, 42, (short)(8 << 8 | 22));
	tree[0].ob_head = tree[0].ob_tail = -1;
	/* the title band: hidden in a dialog window, whose title names it */
	ted(&ted_band, (char *)spec->title, "", "", 2, 0x1071, 0);	/* (white text, transparent, on black) */
	obj(O_BAND, G_BOXTEXT, 0, 0, (long)&ted_band, 0, 0, 42, 1);
	pop_button(O_DRIVE, &ted_drive, s_drive, W_DRIVE, 1, 2, "C:");
	/* the folder: its text in a white well (not typed into: the drive,
	   the folders, Up and a double click change it) */
	obj(O_PATH, G_BOX, 0, 0, 0x00FF1100L, 7, 2, 30, 1);
	ted(&ted_path, s_path, "", "", 0, 0x1100, 0);
	obj_in(O_PATH, O_PATHT, G_TEXT, 0, 0, (long)&ted_path, 1, 0, 28, 1);
	obj(O_UP, G_BOXCHAR, TOUCHEXIT, 0, 0x01FF1100L, 38, 2, 3, 1);
	pop_button(O_FOLDERS, &ted_folders, s_folders, W_FOLDERS, 1, 4, "Folders");
	pop_button(O_TYPE, &ted_type, s_type, W_TYPE, 14, 4, "");
	pop_button(O_SORT, &ted_sort, s_sort, W_SORT, 30, 4, "");
	obj(O_NAMEL, G_STRING, 0, 0, (long)"Name", 1, 6, 4, 1);
	ted(&ted_mask, s_mask, t_mask, v_mask, 0, 0x1180, -1);
	obj(O_MASK, G_FBOXTEXT, EDITABLE, 0, (long)&ted_mask, 7, 6, 13, 1);
	/* a white well with a frame: ten rows and its edges */
	obj(O_LIST, G_BOX, 0, 0, 0x00FF1100L, 1, 8, 40, (short)(2 << 8 | 10));
	obj(O_INFO, G_STRING, 0, 0, (long)s_info, 1, (short)(4 << 8 | 18), 40, 1);
	obj(O_ALL, G_BUTTON, SELECTABLE | EXIT, 0, (long)"All", 1, (short)(4 << 8 | 20), 5, 1);
	obj(O_NONE, G_BUTTON, SELECTABLE | EXIT, 0, (long)"None", 7, (short)(4 << 8 | 20), 6, 1);
	/* Cancel then OK, 10 wide, at W - 24 and W - 12 (5.10, D26) */
	obj(O_CANCEL, G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 18, (short)(4 << 8 | 20), 10, 1);
	obj(O_ADD, G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)spec->ok, 30, (short)(4 << 8 | 20), 10, 1);
	tree[O_N - 1].ob_flags |= LASTOB;
	tree[0].ob_next = -1;
	rsrc_obfix_all(tree, O_N);
	/* the list's well a few pixels taller, its rows whole */
}

/* ---- the folder ---- */

static DLIST dl;
static char dir_now[FP_PATH];
static int top, cur = -1, anchor = -1;
static GRECT lr;			/* the list's rows (inside the well) */
static short rows, rh, sw;

/* A folder's entries with their long names. TOS gives GEMDOS's 8+3, and on MiNT, in the TOS domain and in the MiNT
   domain alike, Fsfirst and Fsnext leave out every name that doesn't fit the DTA's 14 bytes: on an ext2 or VFAT
   drive a folder of long names looked empty or short (a tester, 9 Nov 2026; the hub's rig, FreeMiNT 1.17: of
   six files, the two that fit 8+3). Where MiNT is (its cookie) the folder is read with Dopendir in its own mode 0
   (the names as the file system has them, case kept) and Dxreaddir, which gives each entry's kind, size and
   time in one call; no Pdomain: Fopen takes a long name in either domain (the same rig) and Fsfirst stays what
   it was elsewhere (TOS, EmuTOS, MagiC). Hidden and system files and names that begin with a dot (hidden on
   ext2) are left out, as Fsfirst with attribute 0x10 leaves them. 1: read; 0: no MiNT or no Dxreaddir */
typedef struct {
	unsigned short mode;
	long index;
	short dev, rdev, nlink, uid, gid;
	long size, blksize, nblocks;
	unsigned short mtime, mdate, atime, adate, ctime, cdate;
	short attr, reserved2;
	long reserved3[2];
} XATTR;
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000

static int read_mint(void)
{
	static char buf[4 + DL_NAME + 4], full[FP_PATH + DL_NAME + 2];
	XATTR xa;
	long h, r, xr = 0;
	size_t n = strlen(dir_now);
	int first = 1;
	snprintf(full, sizeof(full), "%s", dir_now);
	h = Dopendir(dir_now, 0);
	if (h < 0)
		return 0;
	while ((r = Dxreaddir((short)sizeof(buf), h, buf, &xa, &xr)) == 0) {
		const char *name = buf + 4;
		unsigned type = xa.mode & S_IFMT;
		short at = xa.attr;
		unsigned short date = xa.mdate, time = xa.mtime;
		long size = xa.size;
		first = 0;
		if (xr < 0 || name[0] == '.' || strlen(name) + n + 1 >= sizeof(full))
			continue;
		if (type == S_IFLNK) {		/* a link: what it points to (Fxattr 0 follows it) */
			snprintf(full + n, sizeof(full) - n, "%s", name);
			if (Fxattr(0, full, &xa) != 0)
				continue;
			type = xa.mode & S_IFMT;
			at = xa.attr;
			date = xa.mdate;
			time = xa.mtime;
			size = xa.size;
		}
		if ((type != S_IFDIR && type != S_IFREG) || (at & 0x06))
			continue;
		if (!dl_add(&dl, name, size, date, time, type == S_IFDIR))
			break;
	}
	Dclosedir(h);
	return r == -49 || !first;	/* (ENMFILES: the end; the first call failing, -32: no Dxreaddir: Fsfirst) */
}

static int mint_here(void)
{
	static short k = -1;		/* worked out once: a cookie walk is a Supexec call */
	if (k < 0)
		k = get_cookie(0x4D694E54L) != 0;	/* MiNT */
	return k;
}

static void read_folder(void)
{
	char mask[FP_PATH + 4];
	DTA *old, dta;
	graf_mouse(BUSYBEE, 0);
	dl_clear(&dl);
	if (!mint_here() || !read_mint()) {
		dl_clear(&dl);
		old = Fgetdta();
		snprintf(mask, sizeof(mask), "%s*.*", dir_now);
		Fsetdta(&dta);
		if (Fsfirst(mask, 0x10) == 0)
			do {
				if (!dl_add(&dl, dta.d_fname, dta.d_length, dta.d_date, dta.d_time, (dta.d_attrib & 0x10) != 0))
					break;
			} while (Fsnext() == 0);
		Fsetdta(old);
	}
	dl_sort(&dl);
	top = 0;
	cur = anchor = dl.nview ? 0 : -1;
	graf_mouse(ARROW, 0);
}

/* the path shown: its end, where it is longer than the field */
static void show_path(void)
{
	size_t n = strlen(dir_now), room = 28;
	char d[3];
	d[0] = dir_now[0] ? dir_now[0] : 'C';
	d[1] = ':';
	d[2] = 0;
	pop_label(s_drive, W_DRIVE, d);
	if (n <= 2)
		snprintf(s_path, sizeof(s_path), "\\");
	else if (n - 2 <= room)
		snprintf(s_path, sizeof(s_path), "%s", dir_now + 2);
	else
		snprintf(s_path, sizeof(s_path), "...%s", dir_now + n - (room - 3));
}

static void info(void)
{
	long bytes;
	int k = dl_selected(&dl, &bytes), dirs = dl_selected_dirs(&dl), files = 0, r;
	char sz[12], fo[24];
	for (r = 0; r < dl.nview; r++)
		files += !dl_at(&dl, r)->dir;
	k -= dirs;			/* (the files selected; the folders said apart) */
	fo[0] = 0;
	if (dirs)
		snprintf(fo, sizeof(fo), " + %d folder%s", dirs, dirs == 1 ? "" : "s");
	dl_size_text(bytes, sz);
	if (bytes >= 1024) {		/* "61.1M": "61.1 MB" */
		size_t n = strlen(sz);
		char u = sz[n - 1];
		sz[n - 1] = 0;
		snprintf(s_info, sizeof(s_info), "%d of %d selected%s, %s %cB", k, files, fo, sz, u);
	} else if (k)
		snprintf(s_info, sizeof(s_info), "%d of %d selected%s, %s bytes", k, files, fo, sz);
	else if (dirs)
		snprintf(s_info, sizeof(s_info), "%d file%s%s selected", files, files == 1 ? "" : "s", fo);
	if (!k && !dirs)
		snprintf(s_info, sizeof(s_info), "%d file%s", files, files == 1 ? "" : "s");
}

/* ---- drawing ---- */

static void place(void)
{
	short x, y;
	objc_offset(tree, O_LIST, &x, &y);
	sw = 2 * gl_wchar;
	rh = gl_hchar;
	lr.x = x + 1;
	lr.y = y + 1;
	lr.w = tree[O_LIST].ob_width - 2;
	lr.h = tree[O_LIST].ob_height - 2;
	rows = lr.h / rh;
}

static int cut(GRECT *r)
{
	GRECT c = *r;
	if (!dlg_cut(&c.x, &c.y, &c.w, &c.h))
		return 0;
	clip_on(&c);
	return 1;
}

static void draw_obj(short o)
{
	dlg_draw(tree, o);
}

/* a name from x, in the room up to the column at `stop` (the size's or the date's, a cell between): an 8+3
   name (12 cells) is never cut; a long name (MiNT) that doesn't fit ends in "..." */
static void name_at(short x, short y, const char *name, short stop, short fl)
{
	long n = (long)strlen(name), room = (stop - x) / gl_wchar - 1;
	if (room < 12)
		room = 12;
	if (n <= room) {
		text_at(x, y, name, n, (short)n, fl);
		return;
	}
	text_at(x, y, name, room - 3, (short)room, fl);
	text_at(x + (short)(room - 3) * gl_wchar, y, "...", 3, 3, fl);
}

/* row r of the view, in its place (-1 rows past the end: blank) */
static void row_at(int r, GRECT *clip)
{
	GRECT t;
	DENT *e = dl_at(&dl, r);
	short y = lr.y + (short)(r - top) * rh, x = lr.x + 4, w = lr.w - sw;
	char sz[12], dt[12];
	short fl = TX_LTR;
	if (r < top || r >= top + rows)
		return;
	t.x = lr.x;
	t.y = y;
	t.w = w;
	t.h = rh;
	(void)clip;
	if (e && e->sel) {
		fill(&t, 1);
		fl |= TX_INVERSE;
	} else
		fill(&t, 0);
	if (!e)
		return;
	if (e->dir) {
		/* a folder: its shape, then its name. The shape, its tab two
		   pixels over it, within the row: 8 high on rows of 12 and more
		   (16: as drawn since 4 Nov 2026), 5 on the ST's rows of 8, where
		   8 reached a row into the next one's */
		GRECT f;
		short c = e->sel ? 0 : 1, fh = rh >= 12 ? 8 : rh - 3;
		f.x = x + 1;
		f.y = y + (rh - fh - 2) / 2 + 2;
		f.w = gl_wchar + 6;
		f.h = fh;
		frame(&f, c);
		line_col(f.x + 2, f.y - 2, f.x + 5, f.y - 2, c);
		name_at(x + 2 * gl_wchar + 4, y, e->name, lr.x + w - 4 - 8 * gl_wchar, fl);
	} else {
		dl_size_text(e->size, sz);
		name_at(x + 2 * gl_wchar + 4, y, e->name, lr.x + w - 4 - (short)(9 + strlen(sz)) * gl_wchar, fl);
		text_at(lr.x + w - 4 - (short)(9 + strlen(sz)) * gl_wchar, y, sz, (long)strlen(sz), 6, fl);
	}
	dl_date_text(e->date, dt);
	text_at(lr.x + w - 4 - 8 * gl_wchar, y, dt, 8, 8, fl);
	if (r == cur) {			/* the keys' place: a dotted frame (the VDI's dots) */
		vsl_type(vdi_h, 3);
		frame(&t, e->sel ? 0 : 1);
		vsl_type(vdi_h, 1);
	}
}

static void slider(void)
{
	GRECT t, s;
	short ah = rh;
	t.x = lr.x + lr.w - sw;
	t.y = lr.y;
	t.w = sw;
	t.h = lr.h;
	fill(&t, 0);
	frame(&t, 1);
	s = t;
	s.h = ah;
	frame(&s, 1);
	text_at(s.x + gl_wchar / 2, s.y, "\x01", 1, 1, TX_LTR);
	s.y = t.y + t.h - ah;
	frame(&s, 1);
	text_at(s.x + gl_wchar / 2, s.y, "\x02", 1, 1, TX_LTR);
	if (dl.nview > rows) {
		short ty = t.y + ah, th = t.h - 2 * ah, h = (short)((long)th * rows / dl.nview), y;
		if (h < 6)
			h = 6;
		y = ty + (short)((long)(th - h) * top / (dl.nview - rows));
		s.x = t.x + 2;
		s.w = t.w - 4;
		s.y = y;
		s.h = h;
		fill(&s, 1);
	}
}

/* the shown rows' selection and the keys' place, before a change: then
   only the rows that changed are drawn (a click: 50 ms drawn whole) */
static unsigned char snap_sel[64];
static int snap_cur, snap_top;

static void snap(void)
{
	int i;
	snap_cur = cur;
	snap_top = top;
	for (i = 0; i < rows && i < 64; i++) {
		DENT *e = dl_at(&dl, top + i);
		snap_sel[i] = e ? e->sel : 0;
	}
}

static void draw_list(void);

static void draw_changed(void)
{
	GRECT r;
	int i;
	if (top != snap_top) {
		draw_list();
		return;
	}
	graf_mouse(M_OFF, 0);
	r.x = lr.x;
	r.y = lr.y;
	r.w = lr.w - sw;
	r.h = rows * rh;
	if (cut(&r)) {
		for (i = 0; i < rows && i < 64; i++) {
			DENT *e = dl_at(&dl, top + i);
			int k = top + i;
			if ((e ? e->sel : 0) != snap_sel[i] || k == cur || k == snap_cur)
				row_at(k, &r);
		}
		clip_off();
	}
	graf_mouse(M_ON, 0);
}

static void draw_list(void)
{
	GRECT r = lr, rest;
	graf_mouse(M_OFF, 0);
	if (cut(&r)) {
		int i;
		for (i = top; i < top + rows; i++)
			row_at(i, &r);
		rest.x = lr.x;
		rest.y = lr.y + rows * rh;
		rest.w = lr.w - sw;
		rest.h = lr.h - rows * rh;
		fill(&rest, 0);
		slider();
		clip_off();
	}
	graf_mouse(M_ON, 0);
}

/* the AES asks for a piece again (a dialog window uncovered): the list */
static void pic_end(void);

static void more(short x, short y, short w, short h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	place();			/* (the list's place first: in a window the dialog's
					   first picture is this one, before the loop's place:
					   0.23 to 0.25 drew it nowhere, the list empty on
					   XaAES until a key moved it) */
	draw_list();
	pic_end();			/* (in a window: the dialog's first picture) */
}

static void show_info(void)
{
	info();
	draw_obj(O_INFO);
}

/* cur in sight, top kept within the list */
static int in_sight(void)
{
	int t = top;
	if (cur >= 0 && cur < top)
		top = cur;
	if (cur >= top + rows)
		top = cur - rows + 1;
	if (top > dl.nview - rows)
		top = dl.nview - rows;
	if (top < 0)
		top = 0;
	return t != top;
}


/* a picture's time, from when it is due to its last piece drawn, to the
   rigs' console ("EPLAY picture folder 45 ms", spec->log naming the app):
   the guidelines' Speed, "A rule never costs speed" */
static unsigned long pic_t0, pic_in, input_ms;	/* input_ms: the dialog's last key or click */
static const char *pic_name;
static void pic_begin(const char *n)
{
	if (!pic_name) {
		pic_name = n;
		pic_t0 = tos_ms();
		pic_in = input_ms && pic_t0 - input_ms < 2000 ? input_ms : pic_t0;
	}
}

static void pic_end(void)
{
	if (pic_name && spec->log) {
		char b[80];
		unsigned long now = tos_ms();
		snprintf(b, sizeof(b), "%s picture %s %lu ms, %lu from the input\n", spec->log, pic_name,
			 now - pic_t0, now - pic_in);
		nf_debug(b);
	}
	pic_name = 0;
}

static void folder_shown(void)
{
	pic_begin("folder");
	show_path();
	info();
	draw_obj(O_DRIVE);
	draw_obj(O_PATH);
	draw_obj(O_INFO);
	draw_list();			/* (whole, no event call between: Erez, 4 Nov 2026) */
	pic_end();
}

static void go(const char *d)
{
	snprintf(dir_now, sizeof(dir_now), "%s", d);
	read_folder();
	folder_shown();
}

static void go_up(int root)
{
	char *p;
	size_t n = strlen(dir_now);
	if (n <= 3)
		return;
	if (root) {
		dir_now[3] = 0;
	} else {
		dir_now[n - 1] = 0;
		p = strrchr(dir_now, '\\');
		if (p)
			p[1] = 0;
	}
	read_folder();
	folder_shown();
}

static void open_row(int r)
{
	DENT *e = dl_at(&dl, r);
	char d[FP_PATH];
	if (!e || !e->dir)
		return;
	if (strlen(dir_now) + strlen(e->name) + 2 > sizeof(d))
		return;
	snprintf(d, sizeof(d), "%s%s\\", dir_now, e->name);
	go(d);
}

/* ---- the pop-ups ---- */

/* The one place a pop-up is opened from (D27): the ticked row over its
   button, the AES's own pop-ups' way and Settings' (settings.c
   over_button): rows are a line and 2 pixels high from 2 pixels down, their
   text a pixel in, so the top is `sel` rows and 3 pixels above the button.
   popup_over() does it now (hub item 6). The button is drawn again after: the pop-up covered it. */
static short pop_over(short o, const char *const *lab, short n, short sel)
{
	short x, y, r;
	if (sel < 0 || sel >= n)
		sel = 0;
	objc_offset(tree, o, &x, &y);
	r = popup_over(x, y, lab, n, sel);
	tree[o].ob_state &= ~SELECTED;
	if (dlg_in_window() || popup_put_back) {
		draw_obj(o);		/* the screen under it is back, or the AES draws it */
	} else {
		draw_obj(0);		/* it wasn't kept (no memory): the dialog again, and its list */
		place();
		draw_list();
	}
	return r;
}

static void pop_drive(void)
{
	static char lab[26][8];
	const char *p[26];
	char drv[26];
	long map = Drvmap();
	short n = 0, k, sel = 0;
	int i;
	for (i = 0; i < 26; i++)
		if (map & (1L << i)) {
			if (dir_now[0] == 'A' + i)
				sel = n;
			snprintf(lab[n], sizeof(lab[n]), "%s%c:", dir_now[0] == 'A' + i ? "\x08" : "", 'A' + i);
			p[n] = lab[n];
			drv[n++] = (char)('A' + i);
		}
	k = pop_over(O_DRIVE, p, n, sel);
	if (k >= 0) {
		char d[4];
		d[0] = drv[k];
		d[1] = ':';
		d[2] = '\\';
		d[3] = 0;
		go(d);
	}
}

static void pop_folders(char recent[FP_RECENT][FP_PATH])
{
	static char lab[FP_RECENT][44];
	const char *p[FP_RECENT];
	short n = 0, k, sel = 0;
	int i;
	for (i = 0; i < FP_RECENT && recent[i][0]; i++) {
		size_t len = strlen(recent[i]);
		const char *tick = "";
		if (!strcmp(recent[i], dir_now)) {	/* the folder shown is the ticked one */
			tick = "\x08";
			sel = n;
		}
		if (len <= 38)
			snprintf(lab[n], sizeof(lab[n]), "%s%s", tick, recent[i]);
		else
			snprintf(lab[n], sizeof(lab[n]), "%s...%s", tick, recent[i] + len - 35);
		p[n] = lab[n];
		n++;
	}
	if (!n) {
		static char lab0[44];
		const char *none[1];
		snprintf(lab0, sizeof(lab0), "~%s", spec->none);
		none[0] = lab0;
		pop_over(O_FOLDERS, none, 1, 0);
		return;
	}
	k = pop_over(O_FOLDERS, p, n, sel);
	if (k >= 0)
		go(recent[k]);
}

static void set_labels(void)
{
	pop_label(s_type, W_TYPE, dl_type_name(&dl, dl.type));
	pop_label(s_sort, W_SORT, dl_sort_name(dl.sort));
}

static void pop_type(void)
{
	static char lab[DL_TYPES][20];
	const char *p[DL_TYPES];
	short k;
	int i;
	for (i = 0; i < dl.ntypes; i++) {
		snprintf(lab[i], sizeof(lab[i]), "%s%s", i == dl.type ? "\x08" : "", dl_type_name(&dl, i));
		p[i] = lab[i];
	}
	k = pop_over(O_TYPE, p, (short)dl.ntypes, (short)dl.type);
	if (k >= 0 && k != dl.type) {
		dl.type = k;
		dl_filter(&dl);
		top = 0;
		cur = anchor = dl.nview ? 0 : -1;
		set_labels();
		draw_obj(O_TYPE);
		show_info();
		draw_list();
	}
}

static void pop_sort(void)
{
	static char lab[DS_N][16];
	const char *p[DS_N];
	short k;
	int i;
	for (i = 0; i < DS_N; i++) {
		snprintf(lab[i], sizeof(lab[i]), "%s%s", i == dl.sort ? "\x08" : "", dl_sort_name(i));
		p[i] = lab[i];
	}
	k = pop_over(O_SORT, p, DS_N, (short)dl.sort);
	if (k >= 0 && k != dl.sort) {
		dl.sort = k;
		dl_sort(&dl);
		top = 0;
		cur = anchor = dl.nview ? 0 : -1;
		set_labels();
		draw_obj(O_SORT);
		draw_list();
	}
}

/* ---- the selection ---- */

static int row_y(short my)
{
	int r = (my - lr.y) / rh;
	if (my < lr.y || r >= rows)
		return -1;
	r += top;
	return r < dl.nview ? r : -1;
}

static void select_to(int r, int shift)
{
	if (r < 0 || r >= dl.nview)
		return;
	snap();
	cur = r;
	if (shift && anchor >= 0)
		dl_select_range(&dl, anchor, cur);
	else {
		dl_select_only(&dl, cur);
		anchor = cur;
	}
	in_sight();
	draw_changed();
	show_info();
}

static void (*idle_fn)(void);

static void idle_turn(void)
{
	if (idle_fn)
		idle_fn();
}

/* a press on the list: a click selects the file alone, Shift-click adds
   or takes out one, a drag selects the files it passes; a double click
   opens a folder, or adds the file. 1: add now (the double click) */
static int list_press(short mx, short my, short ks)
{
	int r = row_y(my), dbl;
	GRECT box;
	short dx, dy;
	if (r < 0)
		return 0;
	dbl = press_double((short)r, mx, my);
	snap();
	if (ks & 3) {
		dl_toggle(&dl, r);
		anchor = cur = r;
		draw_changed();
		show_info();
		return 0;
	}
	cur = anchor = r;
	dl_select_only(&dl, r);
	draw_changed();
	show_info();
	box.x = lr.x;
	box.y = lr.y + (short)(r - top) * rh;
	box.w = lr.w - sw;
	box.h = rh;
	switch (press_track(&box, mx, my, idle_turn, 0, &dx, &dy)) {
	case PRESS_IN:
		if (dbl) {
			DENT *e = dl_at(&dl, r);
			if (e && e->dir) {
				open_row(r);
				return 0;
			}
			return e != 0;
		}
		return 0;
	case PRESS_DRAG: {
		/* the rows it passes, from where it began; the list moves on
		   at its edges */
		short x, y, b, k, last = -1;
		do {
			int at;
			graf_mkstate(&x, &y, &b, &k);
			if (y < lr.y && top > 0) {
				top--;
				at = top;
			} else if (y >= lr.y + rows * rh && top + rows < dl.nview) {
				top++;
				at = top + rows - 1;
			} else
				at = row_y(y < lr.y ? lr.y : y >= lr.y + rows * rh ? lr.y + rows * rh - 1 : y);
			if (at < 0)
				at = dl.nview - 1;
			if (at != last) {
				snap();
				snap_top = top == snap_top ? top : -1;
				cur = at;
				dl_select_range(&dl, anchor, cur);
				draw_changed();
				show_info();
				last = (short)at;
			}
			idle_turn();
			evnt_timer_(20);
		} while (b & 1);
		return 0;
	}
	}
	return 0;
}

/* a press on the slider: an arrow a row, the track a page */
static void slider_press(short my)
{
	short ah = rh, t = top;
	if (my < lr.y + ah)
		top--;
	else if (my >= lr.y + lr.h - ah)
		top++;
	else if (dl.nview > rows) {
		short th = lr.h - 2 * ah, h = (short)((long)th * rows / dl.nview), y;
		if (h < 6)
			h = 6;
		y = lr.y + ah + (short)((long)(th - h) * top / (dl.nview - rows));
		top += my < y ? -rows : my >= y + h ? rows : 0;
	}
	if (top > dl.nview - rows)
		top = dl.nview - rows;
	if (top < 0)
		top = 0;
	if (top != t)
		draw_list();
}

/* a button's press as GEM's: drawn pressed, its action on the release
   over it */
static int button(short o)
{
	short x, y, b, k;
	graf_mkstate(&x, &y, &b, &k);
	if (!(b & 1))
		return 1;		/* (let go already) */
	return graf_watchbox(tree, o, SELECTED, 0);
}

/* the rigs' console: where the buttons are, "EPLAY geom type=x,y,w,h ..." (spec->log names the app) */
static void log_geom(void)
{
	static const struct {
		const char *name;
		short o;
	} k[] = { { "drive", O_DRIVE }, { "folders", O_FOLDERS }, { "type", O_TYPE }, { "sort", O_SORT },
		  { "mask", O_MASK }, { "cancel", O_CANCEL }, { "add", O_ADD }, { "list", O_LIST } };
	char b[240];
	int i, n;
	n = snprintf(b, sizeof(b), "%s geom", spec->log);
	for (i = 0; i < 8; i++) {
		short x, y;
		objc_offset(tree, k[i].o, &x, &y);
		n += snprintf(b + n, sizeof(b) - n, " %s=%d,%d,%d,%d", k[i].name, x, y, tree[k[i].o].ob_width,
			      tree[k[i].o].ob_height);
	}
	snprintf(b + n, sizeof(b) - n, "\n");
	nf_debug(b);
}

/* the filter field changed: the list narrowed as it is */
static void mask_changed(void)
{
	snprintf(dl.mask, sizeof(dl.mask), "%s", s_mask);
	dl_filter(&dl);
	top = 0;
	cur = anchor = dl.nview ? 0 : -1;
	draw_list();
	show_info();
}

short filepick_system;

/* "Use the system file selector" (Erez, 9 Nov 2026): the AES's own, through fsel_exinput (titled where the AES is 1.4
   or newer: gem.c), on the folder dir and its files whatever their type (the AES's mask is one, and the app's types
   are several). One file, which plain TOS gives and nothing more; it goes to add() once, the folder it was
   chosen in is left in dir with its '\\' and put first in recent[], as the dialog does. idle() isn't called while
   the selector is open: it is the AES's, and the app is waiting in it. The click that closed it is settled
   (fsel_settle: a double click's second press would reach the app's window as a click of its own). 1 or 0 on Cancel. */
static int run_system(const FP_SPEC *sp, char *dir, char recent[FP_RECENT][FP_PATH], void (*add)(const char *path))
{
	static char path[FP_PATH + 4], folder[FP_PATH], name[DL_NAME], full[FP_PATH + DL_NAME];
	short button = 0;
	if (dir[0])
		snprintf(path, sizeof(path), "%s*.*", dir);
	else
		snprintf(path, sizeof(path), "%c:\\*.*", (char)('A' + Dgetdrv()));
	name[0] = 0;
	fsel_exinput(path, name, &button, sp->title);
	fsel_settle();
	if (!button || !name[0] || !dl_folder_of(path, folder, sizeof(folder)) ||
	    !dl_join(folder, name, full, sizeof(full)))
		return 0;
	snprintf(dir, FP_PATH, "%s", folder);
	dl_recent_front(recent[0], FP_PATH, FP_RECENT, folder);
	add(full);
	return 1;
}

int filepick_run(const FP_SPEC *sp, char *dir, char recent[FP_RECENT][FP_PATH], void (*add)(const char *path),
		 void (*idle)(void))
{
	if (filepick_system)
		return run_system(sp, dir, recent, add);
	short x, y, w, h, idx = 0, m[8];
	int done = 0, added = 0, i;
	AEVENT e;
	spec = sp;
	idle_fn = idle;
	dl_init(&dl);
	dl_types(&dl, sp->types, sp->ntypes);
	build();
	memset(t_mask, '_', 12);
	t_mask[12] = 0;
	memset(v_mask, 'X', 12);	/* (any letter, wildcards too) */
	v_mask[12] = 0;
	s_mask[0] = 0;
	ted_path.te_txtlen = FP_PATH;
	ted_mask.te_txtlen = 13;
	ted_mask.te_tmplen = 13;
	if (dir[0])
		snprintf(dir_now, sizeof(dir_now), "%s", dir);
	else
		snprintf(dir_now, sizeof(dir_now), "%c:\\", (char)('A' + Dgetdrv()));
	read_folder();
	show_path();
	set_labels();
	info();
	dlg_name(spec->title);
	dlg_band(O_BAND);
	pic_begin("add");
	dlg_open(tree, &x, &y, &w, &h);
	dlg_more(more);
	if (spec->log)
		log_geom();
	if (!dlg_in_window()) {
		/* (TOS: the tree drawn; the list with it, before the first event
		   call. In a window the AES's redraw draws both: more) */
		place();
		draw_list();
		pic_end();
	}
	objc_edit(tree, O_MASK, 0, &idx, ED_INIT);
	while (!done) {
		dlg_evnt(MU_KEYBD | MU_BUTTON | MU_TIMER, 1, 1, 1, 20, m, &e);
		if (e.which & (MU_KEYBD | MU_BUTTON))
			input_ms = tos_ms();	/* (a picture's time from the input: the rigs') */
		place();			/* (the window may have moved) */
		if (e.which & MU_TIMER)
			idle_turn();
		if (e.which & MU_KEYBD) {
			short scan = (e.kreturn >> 8) & 0xff, a = e.kreturn & 0xff;
			int shift = (e.kstate & 3) != 0;
			if (scan == 0x48)
				select_to(cur - 1, shift);
			else if (scan == 0x50)
				select_to(cur + 1, shift);
			else if (scan == 0x1c || scan == 0x72) {	/* Return, Enter */
				/* (a folder at the keys' place, alone or nothing
				   selected: opened, as before; Add adds it) */
				DENT *c = dl_at(&dl, cur);
				int k = dl_selected(&dl, 0);
				if (c && c->dir && (!k || (k == 1 && c->sel)))
					open_row(cur);
				else
					done = 2;
			} else if (scan == 0x61)			/* Undo */
				done = 1;
			else if (key_ctrl(e.kstate, e.kreturn) && (a == 1 || (a | 0x20) == 'a')) {	/* ^A */
				dl_select_all(&dl, 1);
				draw_list();
				show_info();
			} else if (a == 27) {
				/* Esc empties the filter field and never cancels, empty or not: the
				   dialog closes by Cancel or Undo (3.5, D28) */
				if (s_mask[0]) {
					objc_edit(tree, O_MASK, 0, &idx, ED_END);
					s_mask[0] = 0;
					draw_obj(O_MASK);
					objc_edit(tree, O_MASK, 0, &idx, ED_INIT);
					mask_changed();
				}
			} else {
				/* the mask: typed into, the list narrowed as it is */
				char was[13];
				strcpy(was, s_mask);
				objc_edit(tree, O_MASK, e.kreturn, &idx, ED_CHAR);
				if (strcmp(was, s_mask))
					mask_changed();
			}
		}
		if ((e.which & MU_BUTTON) && !done) {
			short o = objc_find(tree, 0, 8, e.mx, e.my);
			switch (o) {
			case O_DRIVE:
				pop_drive();
				break;
			case O_FOLDERS:
				pop_folders(recent);
				break;
			case O_TYPE:
				pop_type();
				break;
			case O_SORT:
				pop_sort();
				break;
			case O_UP:
				go_up(press_double(-2, e.mx, e.my));
				break;
			case O_ALL:
			case O_NONE:
				if (button(o)) {
					dl_select_all(&dl, o == O_ALL);
					draw_list();
					show_info();
				}
				tree[o].ob_state &= ~SELECTED;
				draw_obj(o);
				break;
			case O_CANCEL:
			case O_ADD:
				if (button(o))
					done = o == O_ADD ? 2 : 1;
				tree[o].ob_state &= ~SELECTED;
				draw_obj(o);
				break;
			case O_LIST:
				if (e.mx >= lr.x + lr.w - sw)
					slider_press(e.my);
				else if (list_press(e.mx, e.my, e.kstate))
					done = 2;
				break;
			}
		}
	}
	objc_edit(tree, O_MASK, 0, &idx, ED_END);
	dlg_close();
	if (done == 2) {
		/* the selected files and folders in the list's order (a folder
		   with its subfolders: its name ending in '\'); with none, the
		   file at the keys' place */
		static char p[FP_PATH + DL_NAME + 2];	/* (a long name and the folder before it: never cut) */
		int any = dl_selected(&dl, 0);
		for (i = 0; i < dl.nview; i++) {
			DENT *f = dl_at(&dl, i);
			if (any ? !f->sel : f->dir || i != cur)
				continue;
			snprintf(p, sizeof(p), "%s%s%s", dir_now, f->name, f->dir ? "\\" : "");
			add(p);
			added++;
		}
		if (added)			/* the folder to the front of the Folders pop-up */
			dl_recent_front(recent[0], FP_PATH, FP_RECENT, dir_now);
	}
	snprintf(dir, FP_PATH, "%s", dir_now);
	dl_free(&dl);
	return added;
}
