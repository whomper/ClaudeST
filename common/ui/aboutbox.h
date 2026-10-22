/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aboutbox.h - the About box of the AERA programs (the guidelines, 1.3),
 * ECal's with Claude ST's fine print: centred on the screen, in the
 * system font, the icon large (in colour with 16 colours or more), the
 * name bold and underlined, the version, a one-line tagline, who made
 * it, up to three lines of the app's own (how it is connected, its
 * memory), then the fine print in the small system font: the licence and
 * up to three lines of the app's (trademarks it names). Return, Esc,
 * Undo, Help, Space or a click closes it. Drawn with the app's own
 * primitives (gemui.h).
 *
 *   ABOUT a = { "ECal", cal_version, "A calendar for the Atari ST, TT and Falcon",
 *               { status, 0, 0 }, { 0, 0, 0 }, icon_bits, icon16_px, icon16_rgb,
 *               icon256_px, icon256_rgb };
 */
#ifndef AERA_ABOUTBOX_H
#define AERA_ABOUTBOX_H

typedef struct {
	const char *name;		/* "ECal" */
	const char *version;		/* "1.32" */
	const char *tagline;		/* at most 44 characters */
	const char *status[3];		/* the app's own lines, 0 for none */
	const char *fine[3];		/* fine print after the licence, 0 for none */
	const unsigned short *icon;	/* 32x32, one plane: icon.h's icon_bits */
	const unsigned char *icon16;	/* 32x32, 4 bits a pixel: icon16.h's icon16_px */
	const short (*icon16_rgb)[3];	/* its colours for pens 8-15: icon16_rgb */
	const unsigned char *icon256;	/* 32x32, a byte a pixel (VDI pens, 0 not drawn):
					   icon256.h's icon256_px, used with 256 colours
					   or more; 0 for none */
	const short (*icon256_rgb)[4];	/* its pens' colours in TOS's 256, { pen, r, g,
					   b }, ending with pen 0: icon256_rgb (a pen
					   that has another colour now is replaced by
					   the nearest) */
} ABOUT;

void about_show(const ABOUT *a);

/* where TOS put the program and its memory: "TT RAM", "TT RAM in part"
   or "", for a status line */
const char *about_ram(void);

/* the 16-colour icon at x, y, each pixel sx by sy, in pens[] (0: the
   icon's own pens 8-15), for an iconified window too */
void about_icon16(const unsigned char *px, short x, short y, short sx, short sy, const short *pens);

/* plain text in the same frame, under a title, with OK: lines broken to
   fit the screen, as many as fit */
void text_box(const char *title, const char *text);

/* at start-up, after a crash TOS recorded in the program (crash.h's
   crash_previous()): an alert saying so, "What happened is in CRASH.LOG,
   <where>." ("next to ECAL.PRG"), which offers to show the report */
void crash_offer(const char *app, const char *where);

#endif
