/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * badge.h - a small boxed label in the system font's small size (6 x 6):
 * "320 KBPS", "44.1 KHZ", "DSP" in EPlay's bar, the Hebrew keyboard's
 * badge, so every app's badges look alike (the hub, 5 Nov 2026).
 *
 * The text a pixel clear of a one-pixel box all round (3 pixels left, 2
 * right: the glyphs' blank column is their right one), the box centred
 * in a row of height rh at y, all in one pen, the ground left as it is.
 *
 * TOS's VDI draws a 6-pixel font a glyph at a time, shifted, and setting
 * its size each time costs more: a badge's picture was 5 to 10 ms slower
 * that way on a stock Falcon (EPlay 0.27, the hub's speed rig). So a
 * badge is made once, as its text changes, into a one-plane form from the
 * font's own glyphs (Line-A's), box and all, and drawn with one raster
 * copy. Where the font isn't as TOS's (another size, no Line-A font) the
 * box is in the form still, and the text is the VDI's.
 *
 *     static BADGE b;
 *     badge_init(vdi_h, the window's text height);   once, and after a
 *                                                    change of screen
 *     w = badge_draw(&b, x, y, rh, "44.1 KHZ", pen, clip);
 *
 * A badge made is one raster copy, transparent, and sets nothing. On the
 * VDI's way (badge_vdi after the call) it sets the writing mode to
 * transparent (2), the text's colour and effects (0), the height back to
 * badge_init's, and for a box too big for the form the fill's interior,
 * perimeter and colour: an app that keeps those in step forgets them.
 */
#ifndef AERA_BADGE_H
#define AERA_BADGE_H

#define BADGE_MAX 21			/* characters */
#define BADGE_WPL 9			/* 144 pixels: 21 x 6 and the box */
#define BADGE_ROWS 12			/* a cell of 8 at most and the box */

typedef struct {
	char s[BADGE_MAX + 3];
	short w, h;			/* the box; 0: not made */
	short glyphs;			/* the text is in the form */
	short gen;			/* badge_init's it was made for */
	unsigned short bits[BADGE_WPL * BADGE_ROWS];
} BADGE;

void badge_init(short vdi_h, short text_h);
short badge_width(const char *s);		/* the box's */
short badge_height(void);
/* the badge's width; s empty: nothing drawn, 0. clip: x1, y1, x2, y2,
   or 0 for none */
short badge_draw(BADGE *b, short x, short y, short rh, const char *s, short pen, const short *clip);
/* the last badge_draw used the VDI's way (its attributes set) */
extern int badge_vdi;

/* the form, from a font's glyphs (first..last, off, dat, fw, fh: the
   font header's) at cw a character: 1 when the text is in it. With no
   font (dat 0) the box alone. For badge_draw, and the host test */
int badge_form(BADGE *b, const char *s, short cw, short ch, short first, short last, const short *off,
	       const unsigned char *dat, short fw, short fh);

#endif
