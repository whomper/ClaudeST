/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * textsel.h - choosing text with the mouse in a read-only view (EMail's
 * reader, any app's text pane), as a model: no AES or VDI call, no copy of
 * the text, nothing allocated except by ts_text. The app keeps its lines and
 * its drawing; this keeps the selection as (line, column) pairs in LOGICAL
 * order, tells which lines changed (so only those are redrawn), gives the
 * rectangles to invert for a line, and the chosen text.
 *
 * The app describes its view with a TSVIEW: the text of a line, and how a
 * pixel maps to a column and back (so a fixed-pitch grid, a proportional
 * font, NVDI's vector fonts and Hebrew drawn right to left all fit). A
 * mixed Hebrew/Latin line selected from the middle is several runs on the
 * screen: the app's `runs` callback gives them; without it a selection on a
 * line is one run.
 *
 * Clicks: ts_press with clicks 1 puts the anchor (nothing is selected until
 * the pointer moves), 2 takes a word, 3 the line; a drag after a double or
 * triple click goes on by words or lines. Shift+click (ts_extend) moves the
 * end of what is chosen. A click that selects nothing (ts_has() 0 on release)
 * is the app's: it may mean "open this link".
 *
 * The app's lines are rebuilt on a resize or a new message: it bumps a
 * generation number (ts_sync) or calls ts_clear, and a selection of the old
 * lines is never drawn.
 */
#ifndef TEXTSEL_H
#define TEXTSEL_H

typedef struct {
	long x0, x1;			/* a run of the line on the screen, x0 < x1 */
} TSRUN;

typedef struct {
	void *ud;			/* the app's, handed to every callback */
	long nlines;
	/* the text of line n as it is stored (logical order, whatever the
	   app draws); *len < 0 or NULL: this line can't be chosen (a rule, an
	   attachment) and has no text in a copy */
	const char *(*line)(void *ud, long n, long *len);
	/* the column (0..len) whose left edge is nearest x, in logical order */
	long (*col_at)(void *ud, long n, long x);
	/* the left edge on the screen of column col; of len, the end of the text */
	long (*x_of)(void *ud, long n, long col);
	/* optional: the screen runs of logical columns [c0, c1) of line n, in any
	   order, at most max; the number of them. NULL: one run from x_of(c0) to
	   x_of(c1) */
	int  (*runs)(void *ud, long n, long c0, long c1, TSRUN *out, int max);
	/* optional: 1 if line n goes on from line n-1 where a soft wrap broke it
	   at a space (a copy joins them with a space, not a line end). NULL: no */
	int  (*wrapped)(void *ud, long n);
	/* optional: 1 for a character that belongs to a word. NULL: letters,
	   digits, '_' and every byte from 0x80 (Atari's accents and Hebrew
	   0xC2-0xDC, UTF-8) */
	int  (*is_word)(unsigned char c);
} TSVIEW;

typedef struct {
	long gen;			/* the generation of the app's lines this belongs to */
	int on;				/* something was pressed (an anchor exists) */
	int unit;			/* 1 characters, 2 words, 3 lines */
	long a0l, a0c, a1l, a1c;	/* the anchor's extent: where the press was (a0 == a1 for characters) */
	long hl, hc;			/* where the pointer is now */
	long d0, d1;			/* the lines that changed in the last call, d0 > d1: none */
} TSSEL;

/* forget the selection (a new message, a resize you don't map through) */
void ts_clear(TSSEL *s);
/* the app's lines are generation gen: a selection made for another one is cleared */
void ts_sync(TSSEL *s, long gen);

/* the left button went down on line `line` (the pointer's line, clamped to the
   view) at pixel x. Returns 0 if that line can't be chosen. clicks: 1, 2, 3. */
int  ts_press(TSSEL *s, const TSVIEW *v, long line, long x, int clicks);
/* Shift+click: the end that isn't the anchor moves to the pointer */
int  ts_extend(TSSEL *s, const TSVIEW *v, long line, long x);
/* the pointer moved with the button down; 1 if the selection changed */
int  ts_drag(TSSEL *s, const TSVIEW *v, long line, long x);
/* the button went up: nothing to do for the model (the selection stays); kept
   so the app has the pair */
void ts_release(TSSEL *s);

/* is anything chosen (not just an anchor)? */
int  ts_has(const TSSEL *s, const TSVIEW *v);
/* the lines to redraw after the last press, extend, drag or clear: [*first, *last];
   0 if none. A selection that moves one line redraws two lines, not the pane */
int  ts_dirty(const TSSEL *s, long *first, long *last);
/* the logical columns of line n that are chosen: [*c0, *c1); 0 if none of it */
int  ts_cols(const TSSEL *s, const TSVIEW *v, long n, long *c0, long *c1);
/* the screen runs of line n to draw inverse (ts_cols, then v->runs or one run);
   the number of runs, 0 if none. max >= 1 */
int  ts_rects(const TSSEL *s, const TSVIEW *v, long n, TSRUN *out, int max);
/* is the point (line, x) inside the chosen text? (the right-click that should
   offer "Translate..." and "Copy" on a selection) */
int  ts_inside(const TSSEL *s, const TSVIEW *v, long line, long x);
/* the chosen text, malloc'd and NUL-terminated, one line end per line (a
   wrapped line joined with a space); *len its length; NULL if nothing is chosen
   or no memory */
char *ts_text(const TSSEL *s, const TSVIEW *v, long *len);

/* how many lines to scroll while the button is down and the pointer is at y,
   outside [y0, y1) (the view's area): negative up, positive down, 0 inside; one
   line at the edge, up to 4 a pointer's ch*4 away. Call it on a timer tick. */
int  ts_autoscroll(long y, long y0, long y1, long ch);

#endif
