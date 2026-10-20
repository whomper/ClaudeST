/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The Font dialog's names on the build machine (ui/fontsel.c built with
 * FONTSEL_NAMES_ONLY): a font's name split into its family and style,
 * the order of the styles in the Style list, the fonts of symbols, and
 * NAME.FNT's lines, old and new.
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "fontsel.h"

static int failed, checks;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static void split(const char *name, const char *family, const char *style)
{
	char f[FS_NAME], s[FS_NAME], m[200];
	memset(f, 'x', sizeof(f));
	memset(s, 'x', sizeof(s));
	fontsel_split(name, f, s);
	snprintf(m, sizeof(m), "\"%s\": \"%s\" \"%s\", not \"%s\" \"%s\"", name, f, s, family, style);
	check(!strcmp(f, family) && !strcmp(s, style), m);
}

/* the styles of a family in the Style list's order */
static void order(const char *const *styles)
{
	short i;
	char m[160];
	for (i = 1; styles[i]; i++) {
		snprintf(m, sizeof(m), "\"%s\" (%d) before \"%s\" (%d)", styles[i - 1],
			 fontsel_rank(styles[i - 1]), styles[i], fontsel_rank(styles[i]));
		check(fontsel_rank(styles[i - 1]) < fontsel_rank(styles[i]), m);
	}
}

int main(void)
{
	static const char *const usual[] = { "Regular", "Italic", "Bold", "Bold Italic", "Thin", "Light",
					     "Light Italic", "Medium", "Semibold", "Extra Bold", "Black",
					     "Condensed", "Bold Condensed", 0 };
	static const char *const same[][2] = {
		{ "Regular", "Roman" }, { "Regular", "Book" }, { "Italic", "Oblique" },
		{ "Bold Italic", "Bold Oblique" }, { "Bold Italic", "BoldItalic" }, { "Semibold", "Demi" },
		{ "Semibold", "Semi Bold" }, { "Bold", "Bd" }, { "Italic", "It" }, { 0, 0 }
	};
	fs_line l;
	short i;
	char m[120];

	/* the names Erez's fonts have, and the usual kinds */
	split("Courier 10 Pitch", "Courier 10 Pitch", "Regular");
	split("Courier 10 Pitch Bold Italic", "Courier 10 Pitch", "Bold Italic");
	split("Courier 10 Pitch Italic", "Courier 10 Pitch", "Italic");
	split("Liberation Mono Bold", "Liberation Mono", "Bold");
	split("Liberation Mono", "Liberation Mono", "Regular");
	split("Aldine 401 BT Bold Italic", "Aldine 401 BT", "Bold Italic");
	split("Letter Gothic 12 Pitch Italic", "Letter Gothic 12 Pitch", "Italic");
	split("System font", "System font", "Regular");
	split("Monospace 821 BT Roman", "Monospace 821 BT", "Roman");
	split("Swiss 721 Light Condensed Italic", "Swiss 721", "Light Condensed Italic");
	split("Futura-BoldItalic", "Futura", "BoldItalic");
	split("Humanist 521 Semi Bold", "Humanist 521", "Semi Bold");
	split("Dutch 801 Extra Bold", "Dutch 801", "Extra Bold");
	split("FreeMono", "FreeMono", "Regular");
	split("Bold", "Bold", "Regular");			/* a name that is only a style */
	split("Extra", "Extra", "Regular");
	split("Gothic Extra", "Gothic Extra", "Regular");	/* Extra on its own is the family's */
	split("Times New Roman", "Times New Roman", "Regular");
	split("Times New Roman Bold", "Times New Roman", "Bold");
	split("Courier   ", "Courier", "Regular");
	split("COURIER BOLD", "COURIER", "BOLD");
	split("A name of thirty-two characters!!", "A name of thirty-two characters!", "Regular");
	/* the VDI's 32 characters, the last word cut: still a style */
	split("Swiss 721 Bold Extended Bold Italic", "Swiss 721", "Bold Extended Bold Ita");
	split("Letter Gothic 12 Pitch Bold Ital", "Letter Gothic 12 Pitch", "Bold Ital");
	split("Letter Gothic 12 Pitch Bold It", "Letter Gothic 12 Pitch", "Bold It");
	split("Courier New Is A Long Name Of B", "Courier New Is A Long Name Of B", "Regular");

	order(usual);
	for (i = 0; same[i][0]; i++) {
		snprintf(m, sizeof(m), "\"%s\" and \"%s\" in the same place", same[i][0], same[i][1]);
		check(fontsel_rank(same[i][0]) == fontsel_rank(same[i][1]), m);
	}
	check(fontsel_rank("Regular") == 0 && fontsel_rank("Italic") == 1 && fontsel_rank("Bold") == 2 &&
	      fontsel_rank("Bold Italic") == 3, "Regular, Italic, Bold, Bold Italic first");

	check(fontsel_symbols("Symbol Monospaced"), "Symbol Monospaced: symbols");
	check(fontsel_symbols("ITC Zapf Dingbats"), "Zapf Dingbats: symbols");
	check(fontsel_symbols("Wingdings"), "Wingdings: symbols");
	check(fontsel_symbols("Wingbats"), "Wingbats: symbols");
	check(!fontsel_symbols("Courier 10 Pitch"), "Courier 10 Pitch: letters");
	check(!fontsel_symbols("Liberation Mono"), "Liberation Mono: letters");

	/* NAME.FNT: FNT2's lines, EContact's with the id, ECal's and EMail's
	   with the name alone */
	check(fontsel_fnt_line("1 Courier 10 Pitch Bold\t5\tCourier 10 Pitch\tBold\t2\r\n", &l) &&
	      l.use == 1 && l.id == 5 && l.format == 2 && !strcmp(l.name, "Courier 10 Pitch Bold") &&
	      !strcmp(l.family, "Courier 10 Pitch") && !strcmp(l.style, "Bold"), "an FNT2 line");
	check(fontsel_fnt_line("1 Monospace 821 BT\t7\tMonospace 821\tRoman\t4", &l) &&
	      !strcmp(l.family, "Monospace 821") && !strcmp(l.style, "Roman") && l.format == 4,
	      "an FNT2 line with the VDI's names, at the file's end");
	check(fontsel_fnt_line("1 Liberation Mono Bold\t12\r\n", &l) && l.id == 12 && !l.format &&
	      !strcmp(l.family, "Liberation Mono") && !strcmp(l.style, "Bold"), "EContact's line");
	check(fontsel_fnt_line("0 Swiss 721\r\n", &l) && l.use == 0 && !l.id && !strcmp(l.name, "Swiss 721"),
	      "ECal's line");
	check(fontsel_fnt_line("1 Letter Gothic 12 Pitch Italic\n", &l) && l.use == 1 &&
	      !strcmp(l.family, "Letter Gothic 12 Pitch") && !strcmp(l.style, "Italic"), "a line ending in LF");
	check(fontsel_fnt_line("1 X\t3\tX Family\t\t1\r\n", &l) && !strcmp(l.style, "Regular"),
	      "an empty style is Regular");
	check(!fontsel_fnt_line(FS_FNT_HEAD "\r\n", &l), "the head is no font");
	check(!fontsel_fnt_line("2 Courier\r\n", &l), "neither 0 nor 1");
	check(!fontsel_fnt_line("1 \r\n", &l), "no name");
	check(!fontsel_fnt_line("1 A name far longer than a VDI font name can be\r\n", &l), "too long a name");
	check(!fontsel_fnt_line("", &l), "an empty line");
	check(fontsel_fnt_line("1 Courier\t5\tA family name longer than thirty-two\tBold\t2", &l) &&
	      strlen(l.family) == FS_NAME - 1 && !strcmp(l.style, "Bold"), "a long family cut short");

	printf("fontsel: %d checks, %s\n", checks, failed ? "FAILED" : "all passed");
	return failed ? 1 : 0;
}
