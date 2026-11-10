/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * popup.c's two contracts on the host (popup.h): popup_m puts every text 2 cells in and the tick in the
 * margin whether the item is ticked or not; popup keeps the old, padded look (text 4 cells in) and the
 * old misalignment of an unpadded tick. The stand-in records what is drawn (stub.c).
 */
#include <stdio.h>
#include <string.h>
#include "stub.h"
#include "popup.h"

static int failed, total;
static void check(int ok, const char *what)
{
	total++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

/* the first text drawn whose string is s: its x, or -1 */
static int text_x(const char *s)
{
	int i;
	for (i = 0; i < ntexts && i < MAXT; i++)
		if (!strcmp(texts[i].s, s))
			return texts[i].x;
	return -1;
}
static int flags_of(const char *s)
{
	int i;
	for (i = 0; i < ntexts && i < MAXT; i++)
		if (!strcmp(texts[i].s, s))
			return texts[i].flags;
	return -1;
}
static int ticks(void)
{
	int i, n = 0;
	for (i = 0; i < ntexts && i < MAXT; i++)
		n += !strcmp(texts[i].s, "\x08");
	return n;
}

int main(void)
{
	/* the new contract: the words, the tick in front of the ticked one only */
	static const char *const nu[] = { "Open", "\x08Hidden", "~Grey", "-", "Quit" };
	/* the old contract, padded */
	static const char *const old[] = { "  Open", "\x08 Hidden", "  Grey", "-", "  Quit" };
	/* the old contract, unpadded: the audit's MG14 */
	static const char *const bare[] = { "Open", "\x08Hidden" };
	short px, w_new, w_old;
	short r;

	stub_reset();
	r = popup_m(100, 50, nu, 5);
	px = popup_area.x;
	check(r == -1, "Esc gives -1");
	check(px == 96, "the box is on a byte (x 100 -> 96)");
	check(text_x("Open") == px + 16, "popup_m: a plain item's text is 2 cells in");
	check(text_x("Hidden") == px + 16, "popup_m: the ticked item's text is 2 cells in too");
	check(text_x("Grey") == px + 16 && (flags_of("Grey") & TX_DIM), "popup_m: a grey item is 2 cells in and dim");
	check(text_x("\x08") == px + 4 && ticks() == 1, "popup_m: the tick is half a cell in, once");
	check(text_x("Quit") == px + 16, "popup_m: the last item too");
	w_new = popup_area.w;
	check(w_new == (6 + 4) * 8 + 3, "popup_m: the width is the longest words (the tick not counted) + 4 cells");

	/* ticked with the longest label: the tick is not part of the width */
	{
		static const char *const t[] = { "\x08" "Hello", "Hi" };
		stub_reset();
		popup_m(100, 50, t, 2);
		check(popup_area.w == (5 + 4) * 8 + 3, "popup_m: a ticked longest label is as wide as its words + 4");
	}

	stub_reset();
	popup(100, 50, old, 5);
	px = popup_area.x;
	w_old = popup_area.w;
	check(text_x("  Open") == px + 16 && text_x("  Quit") == px + 16, "popup: a padded item is drawn from 2 cells (its text 4 in)");
	check(text_x(" Hidden") == px + 24 && text_x("\x08") == px + 4,
	      "popup: a padded tick keeps the text where the unticked is (its text 4 in) and the tick half a cell in");
	check(w_old == (8 + 4) * 8 + 3, "popup: the width is the padded label + 4 cells (unchanged)");
	check(w_old - w_new == 2 * 8, "the padded look is 2 cells wider than popup_m's for the same words");

	stub_reset();
	popup(100, 50, bare, 2);
	px = popup_area.x;
	check(text_x("Open") == px + 16 && text_x("Hidden") == px + 24,
	      "popup: an unpadded tick still moves the text a cell right (MG14: the example popup.h had)");

	/* above the item it is for, and kept on the desktop: both entry points */
	stub_reset();
	popup_m_alt(630, 390, 300, nu, 5);
	check(popup_area.x + popup_area.w - 3 <= 640 && popup_area.y + popup_area.h <= 400, "popup_m_alt: on the desktop");
	check(popup_area.y < 300, "popup_m_alt: above alt_y when there is no room below");

	/* over its button (D27): the ticked row begins a pixel above the button, rows being ch + 2 high from 2 down */
	{
		static const char *const c[] = { "Alpha", "Beta", "\x08Gamma", "Delta" };
		static const char *const lines[] = { "One", "-", "\x08Two", "Three" };
		short ih;
		stub_reset();
		popup_over(200, 150, c, 4, 2);
		ih = (short)(popup_area.h - 3 - 4) / 4;	/* the frame's 2 + 2, four rows */
		check(popup_area.y == 150 - 2 - 2 * ih - 1 && popup_area.y + 2 + 2 * ih == 150 - 1,
		      "popup_over: the ticked row (2 of 4) begins a pixel above the button");
		check(popup_area.x == 200, "popup_over: x is the button's (on a byte)");
		stub_reset();
		popup_over(200, 150, c, 4, 0);
		check(popup_area.y == 150 - 3, "popup_over: the first row over the button: 3 pixels above it");
		stub_reset();
		popup_over(200, 150, lines, 4, 2);
		check(popup_area.y == 150 - 1 - (2 + 2 * ih),
		      "popup_over: a separator before the row counts as a row (the place comes from the layout)");
		stub_reset();
		popup_over(200, 395, c, 4, 0);
		check(popup_area.y + popup_area.h <= 401 && popup_area.y > 395 - 4 * 20,
		      "popup_over: near the bottom it is moved up onto the desktop, not flipped above the button");
		stub_reset();
		popup_over(200, 4, c, 4, 3);
		check(popup_area.y >= 0, "popup_over: near the top it is kept on the desktop");
		stub_reset();
		popup_over(200, 150, c, 4, 9);
		check(popup_area.y == 150 - 3, "popup_over: a selection out of range is the first row");
	}

	printf("%d checks, %d failed\n", total, failed);
	return failed != 0;
}
