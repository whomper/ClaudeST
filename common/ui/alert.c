/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alert.c - the alert, with formatting, wrapping and bidi (see alert.h).
 *
 * TOS's and EmuTOS's form_alert take Return but not Esc (EmuTOS's
 * fm_keybd knows Return, Enter, Tab and the arrows), so the alert is
 * built here, as EmuTOS's fm_alert builds it (gemfmalt.c), and run by
 * the shared dialog loop, where Esc and Undo pick the last button (the
 * guidelines, 3.5 and 6.2). Anything it can't show goes to form_alert, and
 * so does everything where the AES runs several programs at once (MiNT,
 * MagiC): there the alert is the AES's, in its look, as its other
 * programs' are.
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "gem.h"
#include "dialog.h"
#include "bidi.h"
#include "alert.h"

void (*alert_hebrew)(char *line, long n);

/* the icons, 32 by 32: our own drawings of the usual three (1 !, 2 ?,
   3 stop) */
static const unsigned short ic_note[64] = {
	0x0000, 0x0000,	/* ................................ */
	0x0001, 0x0000,	/* ...............#................ */
	0x0003, 0x8000,	/* ..............###............... */
	0x0003, 0x8000,	/* ..............###............... */
	0x0007, 0xc000,	/* .............#####.............. */
	0x0007, 0xc000,	/* .............#####.............. */
	0x000e, 0xe000,	/* ............###.###............. */
	0x000e, 0xe000,	/* ............###.###............. */
	0x001c, 0x7000,	/* ...........###...###............ */
	0x001c, 0x7000,	/* ...........###...###............ */
	0x0038, 0x3800,	/* ..........###.....###........... */
	0x0038, 0x3800,	/* ..........###.....###........... */
	0x0070, 0x1c00,	/* .........###.......###.......... */
	0x0073, 0xdc00,	/* .........###..####.###.......... */
	0x00e3, 0xce00,	/* ........###...####..###......... */
	0x00e3, 0xce00,	/* ........###...####..###......... */
	0x01c3, 0xc700,	/* .......###....####...###........ */
	0x0383, 0xc380,	/* ......###.....####....###....... */
	0x0383, 0xc380,	/* ......###.....####....###....... */
	0x0701, 0x81c0,	/* .....###.......##......###...... */
	0x0701, 0x81c0,	/* .....###.......##......###...... */
	0x0e00, 0x00e0,	/* ....###.................###..... */
	0x0e00, 0x00e0,	/* ....###.................###..... */
	0x1c03, 0xc070,	/* ...###........####.......###.... */
	0x1c03, 0xc070,	/* ...###........####.......###.... */
	0x3803, 0xc038,	/* ..###.........####........###... */
	0x3800, 0x0038,	/* ..###.....................###... */
	0x7fff, 0xfffc,	/* .#############################.. */
	0x7fff, 0xfffc,	/* .#############################.. */
	0xffff, 0xfffe,	/* ###############################. */
	0x0000, 0x0000,	/* ................................ */
	0x0000, 0x0000,	/* ................................ */
};
static const unsigned short ic_quest[64] = {
	0x000f, 0xe000,	/* ............#######............. */
	0x007f, 0xfc00,	/* .........#############.......... */
	0x01ff, 0xff00,	/* .......#################........ */
	0x03e0, 0x0f80,	/* ......#####.........#####....... */
	0x0780, 0x03c0,	/* .....####.............####...... */
	0x0f00, 0x01e0,	/* ....####...............####..... */
	0x1c00, 0x0070,	/* ...###...................###.... */
	0x3c00, 0x0078,	/* ..####...................####... */
	0x3807, 0xe038,	/* ..###........######.......###... */
	0x700f, 0xf01c,	/* .###........########.......###.. */
	0x701e, 0x781c,	/* .###.......####..####......###.. */
	0x601c, 0x380c,	/* .##........###....###.......##.. */
	0xe000, 0x380e,	/* ###...............###.......###. */
	0xe000, 0x780e,	/* ###..............####.......###. */
	0xe000, 0xf00e,	/* ###.............####........###. */
	0xe001, 0xe00e,	/* ###............####.........###. */
	0xe001, 0xc00e,	/* ###............###..........###. */
	0xe001, 0xc00e,	/* ###............###..........###. */
	0xe000, 0x000e,	/* ###.........................###. */
	0x6001, 0xc00c,	/* .##............###..........##.. */
	0x7001, 0xc01c,	/* .###...........###.........###.. */
	0x7001, 0xc01c,	/* .###...........###.........###.. */
	0x3800, 0x0038,	/* ..###.....................###... */
	0x3c00, 0x0078,	/* ..####...................####... */
	0x1c00, 0x0070,	/* ...###...................###.... */
	0x0f00, 0x01e0,	/* ....####...............####..... */
	0x0780, 0x03c0,	/* .....####.............####...... */
	0x03e0, 0x0f80,	/* ......#####.........#####....... */
	0x01ff, 0xff00,	/* .......#################........ */
	0x007f, 0xfc00,	/* .........#############.......... */
	0x000f, 0xe000,	/* ............#######............. */
	0x0000, 0x0000,	/* ................................ */
};
static const unsigned short ic_stop[64] = {
	0x03ff, 0xff80,	/* ......###################....... */
	0x07ff, 0xffc0,	/* .....#####################...... */
	0x0e00, 0x00e0,	/* ....###.................###..... */
	0x1dff, 0xff70,	/* ...###.#################.###.... */
	0x3bff, 0xffb8,	/* ..###.###################.###... */
	0x77ff, 0xffdc,	/* .###.#####################.###.. */
	0xefff, 0xffee,	/* ###.#######################.###. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xd841, 0x8c36,	/* ##.##....#.....##...##....##.##. */
	0xd7f7, 0x75d6,	/* ##.#.#######.###.###.#.###.#.##. */
	0xd7f7, 0x75d6,	/* ##.#.#######.###.###.#.###.#.##. */
	0xd8f7, 0x7436,	/* ##.##...####.###.###.#....##.##. */
	0xdf77, 0x75f6,	/* ##.#####.###.###.###.#.#####.##. */
	0xdf77, 0x75f6,	/* ##.#####.###.###.###.#.#####.##. */
	0xd0f7, 0x8df6,	/* ##.#....####.####...##.#####.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xdfff, 0xfff6,	/* ##.#########################.##. */
	0xefff, 0xffee,	/* ###.#######################.###. */
	0x77ff, 0xffdc,	/* .###.#####################.###.. */
	0x3bff, 0xffb8,	/* ..###.###################.###... */
	0x1dff, 0xff70,	/* ...###.#################.###.... */
	0x0e00, 0x00e0,	/* ....###.................###..... */
	0x07ff, 0xffc0,	/* .....#####################...... */
	0x03ff, 0xff80,	/* ......###################....... */
	0x0000, 0x0000,	/* ................................ */
};

#define AL_LINES 5
#define AL_BUTS  3
#define AL_LINE  40
#define AL_BUT   20

static OBJECT al_tree[2 + AL_LINES + AL_BUTS];
static BITBLK al_icon;
static char al_text[AL_LINES][AL_LINE + 1], al_btn[AL_BUTS][AL_BUT + 1];

static void al_obj(short i, short type, long spec, short x, short y, short w, short h)
{
	OBJECT *o = &al_tree[i];
	o->ob_next = i + 1;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = o->ob_state = 0;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
}

/* the alert as a tree, laid out as EmuTOS's fm_build does, in the system
   font's cells: the icon at the top left, the lines beside it, the
   buttons below both, centred, as wide as the widest and 2 cells apart.
   Gives the number of buttons (the first is object first), 0 when it
   can't be shown that way */
static short al_build(short icon, short nlines, const char *btns, short def, short *first)
{
	short cw, ch, bw, bh, dx, dy, dw, dh, i, n = 0, blen = 0, mlen = 0;
	short iw = 0, ih = 0, mx = 1, bx, by, aw, ah, all, o;
	const char *p = btns;
	/* the buttons: "A|B|C]" */
	while (*p && *p != ']') {
		short k = 0;
		if (n == AL_BUTS)
			return 0;
		while (*p && *p != '|' && *p != ']') {
			if (k == AL_BUT)
				return 0;
			al_btn[n][k++] = *p++;
		}
		al_btn[n][k] = 0;
		if (k > blen)
			blen = k;
		n++;
		if (*p == '|')
			p++;
	}
	if (!n || nlines < 1)
		return 0;
	graf_handle(&cw, &ch, &bw, &bh);
	if (cw <= 0 || ch <= 0)
		return 0;
	for (i = 0; i < nlines; i++) {
		short k = (short)strlen(al_text[i]);
		if (k > mlen)
			mlen = k;
	}
	blen++;			/* half a cell inside each end of a button */
	/* in cells; aw is the box's width so far */
	aw = 1;
	if (icon) {
		iw = (32 + cw - 1) / cw;
		ih = (32 + ch - 1) / ch;
		aw += iw + 1;
		mx = iw + 2;
	}
	all = n * blen + 2 * (n - 1);
	if (mlen + aw > all + 1) {
		aw += mlen + 1;
		bx = (aw - all) / 2;
	} else {
		aw = all + 2;
		bx = 1;
	}
	by = (icon && 1 + ih > nlines + 1 ? 1 + ih : nlines + 1) + 1;
	ah = (by + 1 > 1 + ih ? by + 1 : 1 + ih) + 1;
	/* it must fit the desk, with the outline */
	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	if (aw * cw + 8 > dw || ah * ch + 8 > dh)
		return 0;
	al_obj(0, G_BOX, 0x00021100L, 0, 0, aw * cw, ah * ch);
	al_tree[0].ob_next = -1;
	al_tree[0].ob_state = OUTLINED;
	o = 1;
	if (icon) {
		al_icon.bi_pdata = (void *)(icon == 1 ? ic_note : icon == 2 ? ic_quest : ic_stop);
		al_icon.bi_wb = 4;
		al_icon.bi_hl = 32;
		al_icon.bi_x = al_icon.bi_y = 0;
		al_icon.bi_color = 1;
		al_obj(o++, G_IMAGE, (long)&al_icon, cw, ch, 32, 32);
	}
	for (i = 0; i < nlines; i++)
		al_obj(o++, G_STRING, (long)al_text[i], mx * cw, (1 + i) * ch, mlen * cw, ch);
	*first = o;
	for (i = 0; i < n; i++) {
		al_obj(o, G_BUTTON, (long)al_btn[i], (bx + i * (blen + 2)) * cw, by * ch, blen * cw, ch);
		al_tree[o].ob_flags = SELECTABLE | EXIT | (i + 1 == def ? DEFAULT : 0);
		o++;
	}
	al_tree[0].ob_head = 1;
	al_tree[0].ob_tail = o - 1;
	al_tree[o - 1].ob_next = 0;
	al_tree[o - 1].ob_flags |= LASTOB;
	return n;
}

/* format, then make the message fit: words wrapped into lines of at most
   30 characters, at most 5 lines; Hebrew put into display order */
#define ALW 30

short alert(short def, const char *fmt, ...)
{
	char raw[600], out[700];
	const char *msg, *btn = 0, *p;
	va_list ap;
	short o, lines = 0, icon = -1, first = 0, nb;
	long k;
	va_start(ap, fmt);
	vsnprintf(raw, sizeof(raw), fmt, ap);
	va_end(ap);
	msg = strstr(raw, "][");
	for (p = raw; (p = strstr(p, "][")); p++)
		btn = p;
	if (!msg || !btn || btn == msg)
		return form_alert(def, raw);
	if (msg == raw + 2 && raw[0] == '[' && raw[1] >= '0' && raw[1] <= '9')
		icon = raw[1] - '0';
	msg += 2;
	memcpy(out, raw, msg - raw);
	o = (short)(msg - raw);
	while (msg < btn && lines < 5) {
		const char *end = msg, *cut;
		char line[64], vis[64];
		short n = 0;
		while (end < btn && *end != '|')
			end++;
		cut = end;
		if (end - msg > ALW) {
			cut = msg + ALW;
			while (cut > msg && *cut != ' ')
				cut--;
			if (cut == msg)
				cut = msg + ALW;
		}
		while (msg < cut && n < (short)sizeof(line) - 1) {
			char c = *msg++;
			line[n++] = c == '[' ? '(' : c == ']' ? ')' : c;
		}
		if (!n)
			line[n++] = ' ';	/* an empty line: EmuTOS takes "||" for a '|' */
		line[n] = 0;
		if (bidi_has_rtl(line, n)) {
			bidi_visual(line, n, bidi_is_rtl(line, n), vis);
			memcpy(line, vis, n);
			if (alert_hebrew)
				alert_hebrew(line, n);
		}
		if (lines)
			out[o++] = '|';
		if (o + n < (short)sizeof(out) - 60) {
			memcpy(out + o, line, n);
			o += n;
		}
		if (n > AL_LINE)
			icon = -1;
		else {
			memcpy(al_text[lines], line, n);
			al_text[lines][n] = 0;
		}
		lines++;
		while (msg < btn && *msg == ' ')
			msg++;
		if (msg < btn && *msg == '|')
			msg++;
	}
	/* the buttons, as they were */
	for (k = 0; btn[k] && o < (short)sizeof(out) - 1; k++)
		out[o++] = btn[k];
	out[o] = 0;
	/* the AES's own where it runs several programs at once (MiNT, MagiC:
	   its look, in its window; Erez, 25 Oct 2026); else ours, where Esc
	   and Undo pick the last button, or form_alert when ours can't */
	if (dlg_windowed())
		return form_alert(def, out);
	if (icon >= 0 && (nb = al_build(icon, lines, btn + 2, def, &first))) {
		short r;
		graf_mouse(ARROW, 0);
		r = dlg_modal_save(al_tree, 0, first + nb - 1);
		return r >= first && r < first + nb ? r - first + 1 : nb;
	}
	return form_alert(def, out);
}

int screen_check(const char *app, short width)
{
	if (width >= 640)
		return 1;
	alert(1, "[3][%s needs a screen 640 pixels wide: ST medium or high, or a TT or Falcon mode.][ OK ]", app);
	return 0;
}
