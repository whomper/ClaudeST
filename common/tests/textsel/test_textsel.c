/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_textsel.c - ui/textsel.c on a text grid of 8-pixel cells starting at x 16:
 * press, drag, double and triple click, Shift+click, the lines to redraw, the
 * rectangles, the text, lines that can't be chosen, a mixed-direction line's runs,
 * and a selection of old lines never drawn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textsel.h"

static int fails;
static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

#define CW 8
#define X0 16
static const char *L[] = {
	"Hello brave world",		/* 0 */
	"second line here",		/* 1: continues? no */
	"and a wrapped",		/* 2 */
	"continuation of it",		/* 3: a soft wrap of line 2 */
	"--------",			/* 4: a rule, not selectable */
	"last \xC2\xC3\xC4 word",	/* 5: Hebrew letters in Atari's set */
	"",				/* 6: an empty line */
	"end",				/* 7 */
};
#define N (long)(sizeof(L) / sizeof(L[0]))

static const char *vline(void *ud, long n, long *len)
{
	if (n == 4) {
		*len = -1;
		return NULL;
	}
	*len = (long)strlen(L[n]);
	return L[n];
}
static long vcol(void *ud, long n, long x)
{
	long c = (x - X0 + CW / 2) / CW;
	long len = (long)strlen(L[n]);
	if (x < X0)
		c = 0;
	return c > len ? len : c;
}
static long vx(void *ud, long n, long col) { return X0 + col * CW; }
static int vwrapped(void *ud, long n) { return n == 3; }
static int two_runs(void *ud, long n, long c0, long c1, TSRUN *out, int max)
{
	/* an RTL line drawn reversed: columns [c0,c1) land at the mirrored place, in two pieces */
	out[0].x0 = 100;
	out[0].x1 = 140;
	out[1].x0 = 300;
	out[1].x1 = 330;
	return 2;
}

static TSVIEW view = { 0, N, vline, vcol, vx, 0, vwrapped, 0 };

static long px(long col) { return X0 + col * CW; }

int main(void)
{
	TSSEL s;
	long f, l, c0, c1, len;
	char *t;
	TSRUN r[4];

	memset(&s, 0, sizeof(s));
	ts_clear(&s);
	check(!ts_has(&s, &view), "nothing chosen at the start");

	/* a click is only an anchor */
	check(ts_press(&s, &view, 0, px(6), 1) && !ts_has(&s, &view), "a click: an anchor, nothing chosen (the app's click, e.g. a link)");
	check(ts_dirty(&s, &f, &l) && f == 0 && l == 0, "...line 0 to redraw");

	/* a drag in the line */
	check(ts_drag(&s, &view, 0, px(11)) && ts_has(&s, &view), "drag to column 11: chosen");
	check(ts_cols(&s, &view, 0, &c0, &c1) && c0 == 6 && c1 == 11, "...columns 6..11");
	check(ts_rects(&s, &view, 0, r, 4) == 1 && r[0].x0 == px(6) && r[0].x1 == px(11), "...one rectangle");
	check(ts_dirty(&s, &f, &l) && f == 0 && l == 0, "...only line 0 redrawn");
	check(!ts_drag(&s, &view, 0, px(11) + 1), "a move inside the same cell: no change");
	t = ts_text(&s, &view, &len);
	check(t && len == 5 && !strcmp(t, "brave"), "text: \"brave\"");
	free(t);
	check(ts_inside(&s, &view, 0, px(8)) && !ts_inside(&s, &view, 0, px(3)) && !ts_inside(&s, &view, 1, px(8)), "inside / outside the chosen text");

	/* a drag that grows by one cell redraws one line */
	ts_drag(&s, &view, 0, px(12));
	check(ts_dirty(&s, &f, &l) && f == 0 && l == 0, "one cell more: line 0 only");

	/* across lines, a soft wrap joins with a space, the rule is skipped */
	ts_press(&s, &view, 1, px(7), 1);
	ts_drag(&s, &view, 3, px(12));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "line here\nand a wrapped continuation"), "text over lines 1..3: line end, then the wrap joined with a space");
	free(t);
	check(ts_dirty(&s, &f, &l) && f <= 3 && l >= 3, "...the new end line to redraw");
	ts_drag(&s, &view, 5, px(4));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "line here\nand a wrapped continuation of it\nlast"), "...past the rule: it has no text (the line before it is whole)");
	free(t);
	check(ts_rects(&s, &view, 4, r, 4) == 0, "...and no rectangle");
	check(ts_rects(&s, &view, 2, r, 4) == 1 && r[0].x0 == px(0) && r[0].x1 == px(13), "a middle line is whole (to its end)");

	/* upward from the anchor */
	ts_press(&s, &view, 3, px(5), 1);
	ts_drag(&s, &view, 1, px(7));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "line here\nand a wrapped conti"), "upward: from the head to the anchor");
	free(t);

	/* a press on a rule can't start; a drag below the view ends at the last text */
	check(!ts_press(&s, &view, 4, px(2), 1), "a press on a line that can't be chosen: not taken");
	ts_press(&s, &view, 5, px(0), 1);
	ts_drag(&s, &view, 99, 0);
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "last \xC2\xC3\xC4 word\n\nend"), "below the view: to the end of the last line; the empty line is a line");
	free(t);
	ts_drag(&s, &view, -3, 0);
	check(ts_has(&s, &view), "above the view: to the start");

	/* words and lines */
	ts_press(&s, &view, 0, px(7), 2);
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "brave"), "double click: the word");
	free(t);
	ts_press(&s, &view, 5, px(7), 2);
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "\xC2\xC3\xC4"), "double click on Hebrew letters (0xC2-0xC4): the word");
	free(t);
	ts_press(&s, &view, 0, px(5), 2);
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, " "), "double click on a blank: the blank");
	free(t);
	ts_press(&s, &view, 1, px(3), 3);
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "second line here"), "triple click: the line");
	free(t);
	ts_press(&s, &view, 0, px(7), 2);
	ts_drag(&s, &view, 1, px(10));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "brave world\nsecond line"), "a drag after a double click goes on by words");
	free(t);
	ts_drag(&s, &view, 0, px(1));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "Hello brave"), "...and back before the first word: the word's start, the anchor's end");
	free(t);

	/* Shift+click */
	ts_press(&s, &view, 0, px(6), 1);
	ts_drag(&s, &view, 0, px(11));
	ts_extend(&s, &view, 0, px(17));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "brave world"), "Shift+click after the end: the end moves");
	free(t);
	ts_extend(&s, &view, 0, px(0));
	t = ts_text(&s, &view, &len);
	check(t && !strcmp(t, "Hello brave world"), "Shift+click before the start: the old end becomes the anchor");
	free(t);

	/* a new generation clears it */
	ts_press(&s, &view, 0, px(0), 1);
	ts_drag(&s, &view, 0, px(4));
	ts_sync(&s, 7);
	check(!ts_has(&s, &view) && ts_text(&s, &view, &len) == NULL, "a new generation of lines: nothing is drawn or copied");
	ts_press(&s, &view, 0, px(0), 1);
	ts_drag(&s, &view, 0, px(4));
	ts_sync(&s, 7);
	check(ts_has(&s, &view), "...the same generation keeps it");
	ts_clear(&s);
	check(!ts_has(&s, &view), "ts_clear");

	/* a mixed-direction line: the app's runs */
	{
		TSVIEW v2 = view;
		v2.runs = two_runs;
		ts_press(&s, &v2, 5, px(0), 1);
		ts_drag(&s, &v2, 5, px(6));
		check(ts_rects(&s, &v2, 5, r, 4) == 2 && r[0].x0 == 100 && r[1].x1 == 330, "a selection on a mixed Hebrew/Latin line: the app's two runs");
		check(ts_inside(&s, &v2, 5, 310) && !ts_inside(&s, &v2, 5, 200), "...inside is by the runs");
	}

	/* nothing chosen: no text */
	ts_clear(&s);
	ts_press(&s, &view, 0, px(3), 1);
	check(ts_text(&s, &view, &len) == NULL, "an anchor only: no text");

	/* autoscroll */
	check(ts_autoscroll(50, 100, 300, 16) == -4 || ts_autoscroll(50, 100, 300, 16) == -4, "pointer far above: up 4");
	check(ts_autoscroll(95, 100, 300, 16) == -1 && ts_autoscroll(300, 100, 300, 16) == 1 && ts_autoscroll(150, 100, 300, 16) == 0 &&
	      ts_autoscroll(330, 100, 300, 16) == 2, "autoscroll: 1 at the edge, more further out, 0 inside");

	printf("%s\n", fails ? "FAILED" : "all textsel checks passed");
	return fails != 0;
}
