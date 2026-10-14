/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * aboutbox.h - the About box of the AERA programs (the guidelines, 1.3),
 * ECal's: centred on the screen, in the system font, the icon large (in
 * colour with 16 colours or more), the name bold and underlined, the
 * version, a one-line tagline, who made it, the licence, and up to three
 * lines of the app's own (how it is connected, its memory). Return, Esc,
 * Undo, Help, Space or a click closes it. Drawn with the app's own
 * primitives (gemui.h).
 *
 *   static const ABOUT about = { "ECal", cal_version,
 *       "A calendar for the Atari ST, TT and Falcon", { 0 },
 *       icon_bits, icon16_px, icon16_rgb };
 */
#ifndef AERA_ABOUTBOX_H
#define AERA_ABOUTBOX_H

typedef struct {
	const char *name;		/* "ECal" */
	const char *version;		/* "1.32" */
	const char *tagline;		/* at most 44 characters */
	const char *status[3];		/* the app's own lines, 0 for none */
	const unsigned short *icon;	/* 32x32, one plane: icon.h's icon_bits */
	const unsigned char *icon16;	/* 32x32, 4 bits a pixel: icon16.h's icon16_px */
	const short (*icon16_rgb)[3];	/* its colours for pens 8-15: icon16_rgb */
} ABOUT;

void about_show(const ABOUT *a);

/* where TOS put the program and its memory: "TT RAM", "TT RAM in part"
   or "", for a status line */
const char *about_ram(void);

/* the 16-colour icon at x, y, each pixel sx by sy, in pens[] (0: the
   icon's own pens 8-15), for an iconified window too */
void about_icon16(const unsigned char *px, short x, short y, short sx, short sy, const short *pens);

#endif
