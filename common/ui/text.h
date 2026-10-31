/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * text.h - text drawn the fast way TOS allows (the guidelines' Speed).
 *
 * TOS's VDI draws text the slow way when the clip's right or bottom edge
 * is less than 2 pixels past it, even text wholly inside the clip: ten
 * lines of 78 characters took an ST (TOS 1.04) 100 ms unclipped and 638
 * ms with the clip tight round them, a stock Falcon (TOS 4.04) 63 against
 * 171 (EHypView, EFTP and the hub, 26 Oct 2026). A redraw's rectangle or
 * a row's often ends right at the text, so text_n() draws the characters
 * wholly inside the clip with clipping off where the clip leaves no such
 * room, and only the one cut at each side with it on.
 *
 * Bold and underline by vst_effects cost TOS three times plain text;
 * text_fx() draws them on plain text, the same bytes as TOS's (TOS 1.04
 * in ST high and medium, TOS 4.04 in 16 colours, EmuTOS: tests/text).
 *
 * Both only for the system font (id 1) with TOS's or EmuTOS's own VDI
 * (no NVDI, whose text is clipped fast and whose fonts are its own), text
 * aligned to the cell's top (vst_alignment 0, 5, as every app has it),
 * and, for text_fx, plain text drawn in MD_REPLACE on a white ground;
 * elsewhere text_n is v_gtext_n and text_fx_fast says no.
 */
#ifndef TEXT_H
#define TEXT_H

/* the font in use: its id (vst_font) and cell (vst_height's); call it
   when the font changes */
void text_font(short h, short id, short cw, short ch);

/* the clip set and kept (vs_clip on), or none; use these in place of
   vs_clip wherever text_n or text_fx draw */
void text_clip(short h, const short *r);	/* x1, y1, x2, y2 */
void text_noclip(short h);

/* n characters at (x, y), the cell's top left, as v_gtext_n */
void text_n(short h, short x, short y, const char *s, long n);

/* text_n for plain text: the caller has the writing mode MD_REPLACE, the
   text in pen 1 (black) and no effects (vst_effects 0), as it would for
   text_n. On TOS's own screen the characters go straight into it, the
   system font's glyphs from Line-A (8x8 or 8x16, all 256), the same bytes
   as the VDI's (tests/text) and some three times the speed on an ST; a
   cell cut by the clip by its visible pixels, so a row cut at a window's
   foot costs nothing more (EPDF, 31 Oct 2026). Where that can't be (a
   cell not on a byte, NVDI, a GDOS font, a screen that isn't interleaved
   planes, or the first draw's check finding the VDI's pixels elsewhere)
   it is text_n. The caller hides the mouse, as for the VDI */
void text_plain(short h, short x, short y, const char *s, long n);
/* a box of the screen moved to another place in the same columns (a
   scroll), as vro_cpyfm(h, 3, p, screen, screen) with p its eight
   corners: on TOS's screen (where text_plain goes into it) the rows moved
   by the CPU, some twice the speed of TOS's copy on an ST (EPDF, 31 Oct
   2026), a 16-pixel group cut at either side moved by its pixels;
   elsewhere, or when the box isn't wholly inside the clip text_clip set,
   vro_cpyfm as before. The caller hides the mouse and holds
   wind_update, as for the VDI */
void scr_rows(short h, const short *p);

/* the box p (x1, y1, x2, y2) inverted, as vr_recfl in MD_XOR with a solid
   fill in pen 1 (every plane of each pixel turned over): a selection
   shown, or taken off again. On TOS's screen (where text_plain goes into
   it) by the CPU, cut to the clip text_clip set; elsewhere vswr_mode
   MD_XOR, vsf_interior solid, vsf_color 1, vr_recfl. It leaves the
   writing mode MD_REPLACE. The caller hides the mouse, as for the VDI */
void scr_xor(short h, const short *p);

/* the tests' and the logs': 1 text_plain goes into the screen, 0 not
   checked yet; below 0 it is text_n: -1 not the system font or NVDI, or
   the first draw's check failed, -2 a cell neither 8x8 nor 8x16, -3 no
   such font in Line-A, -4 its header not as TOS's, -5 not 8 pixels a
   glyph, -6 a screen not of interleaved planes */
int text_plain_state(void);

/* bold (fx 1) and underline (8) on the n characters s of plain text just
   drawn at (x, y) by text_n in pen colour, when text_fx_fast(fx, mode)
   says it can; else draw with vst_effects as before */
int text_fx_fast(short fx, short mode);
void text_fx(short h, short x, short y, const char *s, long n, short fx, short colour);

/* bold where text_fx_fast says no (NVDI, a GDOS font, another writing
   mode): the n characters s just drawn plain at (x, y) struck again a
   pixel to the right, transparent. Never vst_effects' bold for text laid
   out in fixed cells: NVDI widens each letter by a pixel or more, and the
   next run drawn in MD_REPLACE covers the last letters (EHypView 1.30,
   Erez's accessory under NVDI: "On the Falcon itsel"). It leaves the
   writing mode MD_REPLACE */
void text_bold_strike(short h, short x, short y, const char *s, long n);

#endif
