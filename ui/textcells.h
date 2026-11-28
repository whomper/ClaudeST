/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * textcells.h - a GDOS font's text: each run in its cells, and its Hebrew
 * letters as the font has them (EMail's draw.c, shared: the hub, 17 Nov
 * 2026; FreeMono in EPDF drew TOS's Hebrew codes 0xC2-0xDC, which
 * a vector font through NVDI has other glyphs at, on cells its advance
 * doesn't keep).
 *
 * After the font is set (and text_font told), the app says once:
 *   text_hebrew(how, idx, id, pt)    fontsel_hebrew's FS_HEB_* for it, the
 *                                    index of each letter (FS_HEB_UNICODE),
 *                                    and its id and points (FS_HEB_SYSTEM's
 *                                    letters come from the system font, the
 *                                    font set again after them)
 *   text_cells(text_cells_probe(h, id, cw), cw, ch)
 * then draws a row already in display order (common's bidi) with
 * text_cells_n. With the system font: FS_HEB_MAP and TEXT_RUNS, and
 * text_cells_n is text_n.
 */
#ifndef TEXTCELLS_H
#define TEXTCELLS_H

#define TEXT_RUNS    0	/* a run drawn in one call */
#define TEXT_JUSTIFY 1	/* monospaced within a pixel a cell: made to fit them */
#define TEXT_CELLS   2	/* a character a cell */

void text_hebrew(short how, const unsigned short *idx, short id, short pt);
short text_cells_probe(short h, short id, short cw);
void text_cells(short how, short cw, short ch);
void text_cells_n(short h, short x, short y, const char *s, long n);

#endif
