/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * text.h - text drawn the fast way TOS allows (the guidelines' Speed).
 *
 * TOS's VDI draws text the slow way whenever a clip rectangle smaller
 * than the screen is set, even text wholly inside it: ten lines of 78
 * characters took an ST (TOS 1.04) 100 ms unclipped and 638 ms with the
 * clip round them, a stock Falcon (TOS 4.04) 63 against 171 (EHypView and
 * the hub, 26 Oct 2026). Every redraw through the AES's rectangle list is
 * clipped, so text_n() draws the characters wholly inside the clip with
 * clipping off and only the one cut at each side with it on.
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

/* bold (fx 1) and underline (8) on the n characters s of plain text just
   drawn at (x, y) by text_n in pen colour, when text_fx_fast(fx, mode)
   says it can; else draw with vst_effects as before */
int text_fx_fast(short fx, short mode);
void text_fx(short h, short x, short y, const char *s, long n, short fx, short colour);

#endif
