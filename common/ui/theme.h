/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * theme.h - the family's UI palette (the guidelines' 5.2, UI standards WINDOWS.md "Roles of colour"; D42,
 * 9 Nov 2026): what EMail, EFTP and EUpdate each carried as their own theme.c, made once. A few colours, for
 * meaning, as modern programs use them: the six BASE roles below are the family's and are the same in every
 * app (one selection colour: P_SEL); an app adds its own meaning colours after them (EFTP's folder, up and
 * down arrows and failure; EUpdate's new release and current; EMail's unread count, flag and quoted text) as
 * a table it hands to theme_init.
 *
 *	enum { P_FOLDER = P_BASE, P_UP, P_DOWN, P_ERR, NPEN };
 *	static const THEME_ROLE mine[] = {			in the order of the enum
 *		{ { 922, 690, 220 }, { 667, 667,   0 }, 0, 0 },		P_FOLDER: amber
 *		{ { 851, 467, 341 }, { 667,   0,   0 }, 1, 0 },		P_UP
 *		...
 *	};
 *	theme_init(opt.colour, mine, NPEN - P_BASE);		after the VDI is open and opt is read
 *	bold_forget();						the app's own: titles kept were in old colours
 *	... text_colour(pen[P_DIM]) ...				theme_exit() at the end
 *
 * 256 colours: the roles take pens 16 and up, set to the RGB below and put back at exit (the desktop's sixteen
 * are never touched: 5.2). 16 colours: each role is the pen nearest its `want16` among the screen's own
 * sixteen (pen_nearest: NVDI's are not TOS's, a TT's 9 to 14 are light where a Falcon's are dark), never a fixed
 * number. Monochrome and 4 colours: P_PANEL is pen 0, everything else pen 1; the selection is inverse.
 * `colour` is 0 there, and when the app's option turns colour off.
 */
#ifndef AERA_THEME_H
#define AERA_THEME_H

enum {
	P_TEXT,		/* ordinary text: near black, a little blue */
	P_DIM,		/* sizes, dates, column titles, notes, counts: slate grey */
	P_LINE,		/* rules and frames */
	P_PANEL,	/* light background: dividers, idle title bars, bar tracks */
	P_HEAD,		/* the title bar of the pane that has the keyboard: dark slate */
	P_SEL,		/* the selection bar and progress: blue, (0, 0, 667) with 16 colours */
	P_BASE		/* the first role of the app's own */
};

#define THEME_MAX 16	/* roles in all: pens 16 up to 31 */

typedef struct {
	short rgb[3];	/* with 256 colours: VDI 0-1000 */
	short want16[3];	/* with 16: the colour wanted among the screen's own sixteen (TOS's darker shades) */
	char text16;	/* 1: it is for text or has white text on it: dark enough to read on white */
	char not_dim;	/* 1: with 16 colours never the same pen as P_DIM (a state's colour reads as disabled
			   otherwise): then P_TEXT */
} THEME_ROLE;

extern short pen[THEME_MAX];
extern int colour;	/* colours are on (else black and white) */

/* after the VDI is open: colour_on is the app's option; extra the app's roles (P_BASE, P_BASE + 1, ...), n of
   them (n + P_BASE <= THEME_MAX); 0 and 0 for the six alone. Calls theme_exit first, so it may be repeated
   (a changed option, a changed screen mode). The app puts its own caches right after (bold_forget) */
void theme_init(int colour_on, const THEME_ROLE *extra, short n);

/* 4 colours (ST medium, planes == 2) with colour on: pen 2 (TOS's red) turned to rgb (VDI 0-1000) while the
   app runs and the role given that pen, for the one accent an app keeps there (EMail's links and unread counts:
   0, 0, 930); everything else stays black and white and `colour` stays 0. Call it after theme_init, with a
   role of the app's own; it does nothing on any other screen or with colour off. theme_exit puts pen 2 back */
void theme_accent4(short role, const short rgb[3]);

/* the palette back as it was (256 colours; pen 2 with theme_accent4); harmless when nothing was set */
void theme_exit(void);

#endif
