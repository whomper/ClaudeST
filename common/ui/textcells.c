/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * textcells.c - a GDOS font's runs in their cells, and its Hebrew as the
 * font has it (textcells.h). EMail's ui/draw.c runs_at and cells_at and
 * ui/font.c cells_mode, shared; fontsel_hebrew (fontsel.c) says where a
 * font's Hebrew is.
 */
#include "gem.h"
#include "text.h"
#include "textcells.h"

static short heb_how;			/* FS_HEB_* (fontsel.h): 0, the codes as they are */
static unsigned short heb_ix[27];	/* FS_HEB_UNICODE: the font's index of each letter, 0xC2-0xDC */
static short heb_id = 1, heb_pt;	/* the font, set again after the system font's letters */
static short cells_how;			/* TEXT_* */
static short cw = 8, ch = 16;

void text_hebrew(short how, const unsigned short *idx, short id, short pt)
{
	short k;

	heb_how = how;
	heb_id = id;
	heb_pt = pt;
	for (k = 0; k < 27; k++)
		heb_ix[k] = how == 1 && idx ? idx[k] : 0;
}

void text_cells(short how, short w, short h)
{
	cells_how = how;
	cw = w > 0 ? w : 8;
	ch = h > 0 ? h : 16;
}

short text_cells_probe(short h, short id, short w)
{
	static const char probe[] = "MiWl.0_|@#mwIj1;:,'AZaz09([{x})]";
	short n = (short)(sizeof(probe) - 1), run, k, wm;

	if (id == 1)
		return TEXT_RUNS;
	run = vqt_extent_n(h, probe, n);
	wm = vqt_width(h, 'M');
	for (k = 0; k < n && vqt_width(h, (unsigned char)probe[k]) == wm; k++)
		;
	if (run == n * w)
		return TEXT_RUNS;
	/* monospaced, within a pixel of a cell a character (a vector font's
	   fractional advance): made to fit */
	if (k == n && run > n * (w - 1) && run <= n * (w + 1))
		return TEXT_JUSTIFY;
	return TEXT_CELLS;
}

static int is_heb(unsigned char c)
{
	return c >= 0xC2 && c <= 0xDC;
}

/* a run with no Hebrew in it, in its cells */
static void cells_at(short h, short x, short y, const char *s, short n)
{
	short k;

	if (cells_how == TEXT_RUNS || n == 1) {
		text_n(h, x, y, s, n);
		return;
	}
	if (cells_how == TEXT_JUSTIFY) {
		v_justified_n(h, x, y, s, n, (short)(n * cw), 0, 1);
		return;
	}
	for (k = 0; k < n; k++)
		if (s[k] != ' ')
			v_gtext_n(h, (short)(x + k * cw), y, s + k, 1);
}

void text_cells_n(short h, short x, short y, const char *s, long n)
{
	short i = 0, m = (short)(n > 32000 ? 32000 : n);

	while (i < m) {
		short j = i, heb = is_heb((unsigned char)s[i]);

		while (j < m && is_heb((unsigned char)s[j]) == heb && j - i < 120)
			j++;
		if (!heb || heb_how == 0) {
			cells_at(h, (short)(x + i * cw), y, s + i, (short)(j - i));
		} else if (heb_how == 1) {
			/* the font's own letters, by its index (NVDI's map mode 0) */
			short k;

			vst_map_mode(h, 0);
			for (k = i; k < j; k++)
				v_gtext_w(h, (short)(x + k * cw), y, &heb_ix[(unsigned char)s[k] - 0xC2], 1);
			vst_map_mode(h, 1);
		} else {
			/* the system font's, each letter centred in its cell */
			short dw, dh, w2, h2, k;

			vst_font(h, 1);
			vst_height(h, ch >= 16 ? 13 : 6, &dw, &dh);
			for (k = i; k < j; k++)
				v_gtext_n(h, (short)(x + k * cw + (cw - dw) / 2), (short)(y + (ch - dh) / 2), s + k, 1);
			vst_font(h, heb_id);
			vst_point(h, heb_pt, &w2, &h2);
		}
		i = j;
	}
}
