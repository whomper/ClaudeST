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

/* the box p (x1, y1, x2, y2) filled solid in pen (vr_recfl in MD_REPLACE,
   solid, that pen): pens 0 and 1 (white, black) by the CPU on TOS's screen
   (where text_plain goes into it), cut to the clip text_clip set, the
   time of a row's fill on an 8 MHz ST some 2.4 ms by the VDI (EFTP, 31
   Oct 2026); other pens, and elsewhere, the VDI. Either way the VDI's
   writing mode and fill are left as they were. The caller hides the
   mouse. Call it where scr_cpu_wins() (a 68000): on a 68030 TOS 4's
   fill is as fast or faster, and an app's own vr_recfl with its fill
   set once beats this one's VDI way (tests/text) */
void scr_fill(short h, const short *p, short pen);

/* the box p filled with a 16 by 16 pattern in pens 1 (its set pixels) and
   0, as vr_recfl in MD_REPLACE with a fill pattern in pen 1 puts it: row
   y's sixteen pixels pat[y & 15], from the left of each 16-pixel group of
   the screen (the VDI's patterns lie on the screen's own grid). By the CPU
   on TOS's screen, where text_plain goes into it, cut to the clip
   text_clip set; 0, and nothing drawn, elsewhere (the caller uses the
   VDI). panebar.c's track (a whole bar 76 ms of an 8 MHz ST by the AES).
   The caller hides the mouse */
int scr_pattern(short h, const short *p, const unsigned short *pat);

/* the clip the CPU's drawing here (scr_fill, scr_text, scr_xor,
   scr_pattern) is cut to for a while, the VDI's own left as it is: r (x1,
   y1, x2, y2), and 0 for the one text_clip or text_noclip set again;
   dialog.c's, for objc_draw's clip */
void scr_clip(const short *r);

/* n characters s of the system font of this cell height (8 or 16: the
   AES's), at any x, as the AES puts an object's text: in pen 1 or 0, with
   MD_TRANS (ground 0) or with the cells' ground in pen 0 (MD_REPLACE,
   ground 1). By the CPU on TOS's screen, where text_plain goes into it,
   cut to the clip; 0, and nothing drawn, elsewhere (the caller lets the
   AES or the VDI draw it). An 8 MHz ST's VDI takes some 9 ms for a dozen
   characters off a byte (EFTP's Settings tabs, 31 Oct 2026). The caller
   hides the mouse */
int scr_text(short x, short y, const char *s, long n, short pen, short height, int ground);

/* a band of rows y1..y2 painted by the CPU: n spans (x1, x2, pen) side by
   side, pens 0 and 1, each row written once (the edge words by their
   masks, the words between whole); cut to the clip. 0, and nothing
   drawn, where text_plain doesn't go into the screen or the band has
   more pieces than it keeps (the caller uses another way). dialog.c's
   frames, outlines and shadows: a narrow fill of its own cost an 8 MHz
   ST some 47 us a row */
int scr_spans(short y1, short y2, const short *spans, short n);

/* a bitmap (rows of wb bytes, its set bits in pen, MD_TRANS) at any x, as
   vrt_cpyfm in MD_TRANS puts a G_IMAGE's; cut to the clip. 0 elsewhere */
int scr_bits(short x, short y, const unsigned char *bits, short wb, short rows, short pen);

/* the box p's pixels cleared where x + y is even, as TOS 1.04's AES greys
   a DISABLED object (MD_TRANS, pen 0, the 50% pattern); 0 elsewhere */
int scr_grey_off(const short *p);

/* 1 where the CPU's drawing into TOS's screen beats the AES and the VDI
   at whole boxes (dialog.c's): text_plain goes into the screen and the
   CPU is a 68000. On a 68030 TOS 4's AES and VDI are as fast or faster
   (a stock Falcon: Settings 90 ms by objc_draw, 117 the CPU's way; the
   screen under it 42 ms by the VDI, 45 by the CPU) */
int scr_cpu_wins(void);

/* the tests' and the logs': 1 text_plain goes into the screen, 0 not
   checked yet; below 0 it is text_n: -1 not the system font or NVDI, or
   the first draw's check failed, -2 a cell neither 8x8 nor 8x16, -3 no
   such font in Line-A, -4 its header not as TOS's, -5 not 8 pixels a
   glyph, -6 a screen not of interleaved planes */
int text_plain_state(void);

/* the clip text_clip or text_noclip set last, given to the VDI again
   (after something drew with a clip of its own: panebar.c) */
void text_reclip(short h);

/* TOS's screen as text_plain writes into it, while text_plain_state() is
   1: its address now (Logbase), bytes a line, planes, width and height;
   0 otherwise */
unsigned char *scr_at(long *bpl, short *planes, short *w, short *h);

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
