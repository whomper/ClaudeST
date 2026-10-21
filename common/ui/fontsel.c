/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fontsel.c - the family's Font dialog (fontsel.h; the guidelines, 5.3),
 * from ECal's font selector, laid out as NVDI's WDIALOG one: the Family
 * list, and beside it the Style list and the Size list; below them the
 * Type line, a small sample in the font chosen (Erez, 20 Oct 2026: no
 * Hebrew samples, no Pt: field, a quarter smaller), OK and Cancel. 48 by
 * 17 cells of the system font: ST medium's 640 x 200 too.
 *
 * The programs lay their text out in character cells, so only the
 * system font and the fixed-width GDOS fonts with letters are listed
 * ('i', 'W' and 'm' as wide at 12 points; not Symbol Monospaced, whose
 * letters are symbols). Measuring means loading each font, slow with
 * hundreds of vector fonts: the answers are kept in the app's NAME.FNT
 * and only new fonts are measured. A font's family and style come from
 * vqt_xfntinfo where the VDI has it (NVDI, SpeedoGDOS), else from its
 * name ("Courier 10 Pitch Bold Italic": Courier 10 Pitch, Bold Italic).
 *
 * The keys: Up and Down in the list with the focus_ring (Shift: a page), Tab,
 * Shift+Tab, Left and Right from list to list, a letter to the next
 * family starting with it, Return OK, Esc and Undo Cancel. A row is chosen by a press of the button in
 * a list, never by where the pointer rests (ClaudeST's stray row came
 * from taking the menu's release for a click). The type line and the
 * sample, which may be slow under NVDI, are drawn once no key waits.
 *
 * Built with -DFONTSEL_NAMES_ONLY, only the names part (no GEM), for
 * tests/fontsel on the build machine.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "fontsel.h"

/* ---------------- the names ---------------- */

static char low(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int strncmp_ci(const char *a, const char *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (low(a[i]) != low(b[i]))
			return 1;
	return 0;
}

static int same_word(const char *a, const char *b, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		if (!b[i] || low(a[i]) != low(b[i]))
			return 0;
	return !b[n];
}

/* the words a name ends in that say its style (any case) */
static const char *const style_words[] = {
	"Regular", "Roman", "Book", "Normal", "Plain",
	"Italic", "Oblique", "Ital", "Obl", "It", "Kursiv", "Slanted",
	"Bold", "Bd", "Light", "Lt", "Medium", "Med", "Thin", "Hairline",
	"Heavy", "Black", "Blk", "Demi", "Demibold", "Semibold", "Extrabold",
	"Extralight", "Ultralight", "Ultrabold", "BoldItalic", "BoldOblique",
	"Condensed", "Cond", "Narrow", "Compressed", "Wide", "Expanded", "Extended", 0
};
/* and the ones that only go with another: Semi Bold, Extra Light */
static const char *const style_mods[] = { "Semi", "Extra", "Ultra", 0 };

static int in_list(const char *const *list, const char *w, size_t n, int cut)
{
	for (; *list; list++)
		if (same_word(w, *list, n) || (cut && n >= 2 && n < strlen(*list) &&
						 !strncmp_ci(w, *list, n)))
			return 1;
	return 0;
}

void fontsel_split(const char *name, char *family, char *style)
{
	char s[FS_NAME];
	size_t n, st = FS_NAME;		/* where the style starts in s */
	int cut;			/* 32 characters: the VDI may have cut its last word */
	strncpy(s, name, FS_NAME - 1);
	s[FS_NAME - 1] = 0;
	n = strlen(s);
	cut = n == FS_NAME - 1;
	while (n && (s[n - 1] == ' ' || s[n - 1] == '-'))
		s[--n] = 0;
	for (;; cut = 0) {
		size_t w = n, len;
		while (w && s[w - 1] != ' ' && s[w - 1] != '-')
			w--;
		len = n - w;
		if (!w || !len)
			break;		/* the family keeps its first word */
		if (!in_list(style_words, s + w, len, cut) &&
		    !(st < FS_NAME && in_list(style_mods, s + w, len, 0)))
			break;
		/* Times New Roman: the Roman is the family's */
		if (same_word(s + w, "Roman", len) && w >= 4 && same_word(s + w - 4, "New", 3) &&
		    (w == 4 || s[w - 5] == ' '))
			break;
		st = w;
		n = w - 1;
		while (n && (s[n - 1] == ' ' || s[n - 1] == '-'))
			n--;
	}
	if (st < FS_NAME) {
		size_t k, o = 0;
		for (k = st; s[k] && o < FS_NAME - 1; k++)
			style[o++] = s[k] == '-' ? ' ' : s[k];
		style[o] = 0;
		s[n] = 0;
	} else {
		strcpy(style, "Regular");
	}
	strcpy(family, s);
}

static int says(const char *s, const char *what)
{
	size_t n = strlen(what);
	for (; *s; s++)
		if (low(*s) == what[0]) {
			size_t i;
			for (i = 1; i < n && low(s[i]) == what[i]; i++)
				;
			if (i == n)
				return 1;
		}
	return 0;
}

/* a word on its own (It, Bd, Med, Lt, Blk) */
static int says_word(const char *s, const char *what)
{
	size_t n = strlen(what);
	const char *p = s;
	while (*p) {
		size_t len = strcspn(p, " -");
		if (len == n && same_word(p, what, n))
			return 1;
		p += len;
		while (*p == ' ' || *p == '-')
			p++;
	}
	return 0;
}

short fontsel_rank(const char *style)
{
	short italic = says(style, "ital") || says(style, "obl") || says(style, "kursiv") ||
		       says(style, "slant") || says_word(style, "it");
	short width = says(style, "cond") || says(style, "narrow") || says(style, "compress") ? 100 :
		      says(style, "wide") || says(style, "expand") ? 200 : 0;
	short weight;
	if (says(style, "thin") || says(style, "hair"))
		weight = 1;
	else if (says(style, "extralight") || says(style, "extra light") || says(style, "ultralight") ||
		 says(style, "ultra light"))
		weight = 2;
	else if (says(style, "light") || says_word(style, "lt"))
		weight = 3;
	else if (says(style, "semibold") || says(style, "semi bold") || says(style, "demi"))
		weight = 6;
	else if (says(style, "extrabold") || says(style, "extra bold") || says(style, "ultrabold") ||
		 says(style, "ultra bold") || says(style, "heavy"))
		weight = 8;
	else if (says(style, "black") || says_word(style, "blk"))
		weight = 9;
	else if (says(style, "bold") || says_word(style, "bd"))
		weight = 7;
	else if (says(style, "medium") || says_word(style, "med"))
		weight = 5;
	else
		weight = 4;
	if (weight == 4)
		return width + italic;		/* Regular, Italic */
	if (weight == 7)
		return width + 2 + italic;	/* Bold, Bold Italic */
	return width + 10 + 2 * weight + italic;
}

int fontsel_symbols(const char *name)
{
	return says(name, "symbol") || says(name, "dingbat") || says(name, "wingding") ||
	       says(name, "webding") || says(name, "wingbat");
}

/* a field of the line, up to its tab or end, into out (FS_NAME); the
   next field */
static const char *fnt_field(const char *p, char *out)
{
	size_t n = strcspn(p, "\t\r\n");
	size_t k = n < FS_NAME - 1 ? n : FS_NAME - 1;
	memcpy(out, p, k);
	out[k] = 0;
	p += n;
	return *p == '\t' ? p + 1 : 0;
}

int fontsel_fnt_line(const char *line, fs_line *l)
{
	const char *p;
	char num[FS_NAME];
	size_t n;
	memset(l, 0, sizeof(*l));
	if ((line[0] != '0' && line[0] != '1') || line[1] != ' ')
		return 0;
	n = strcspn(line + 2, "\t\r\n");
	if (!n || n >= FS_NAME)
		return 0;
	l->use = (short)(line[0] - '0');
	p = fnt_field(line + 2, l->name);
	if (p) {
		p = fnt_field(p, num);
		l->id = (short)atoi(num);
	}
	if (p)
		p = fnt_field(p, l->family);
	if (p)
		p = fnt_field(p, l->style);
	if (p) {
		fnt_field(p, num);
		l->format = (short)atoi(num);
	}
	if (!l->family[0])
		fontsel_split(l->name, l->family, l->style);
	else if (!l->style[0])
		strcpy(l->style, "Regular");
	return 1;
}

#ifndef FONTSEL_NAMES_ONLY

/* ---------------- the fonts ---------------- */

#include "plat.h"
#include "dialog.h"
#ifdef FONTSEL_TRACE
#include "tos.h"
#define TRACE(...) do { char tb[160]; snprintf(tb, sizeof(tb), __VA_ARGS__); nf_debug(tb); } while (0)
#define TRACE_MS(what) TRACE("fontsel: %lu ms %s\n", (unsigned long)tos_ms(), what)
#else
#define TRACE(...) do { } while (0)
#define TRACE_MS(what) do { } while (0)
#endif

#define MAXSIZE 24

typedef struct {
	short id, format, rank;
	char later;			/* from NAME.FNT, not loaded (the accessory) */
	char name[FS_NAME], family[FS_NAME], style[FS_NAME];
} FSF;				/* a font listed */

typedef struct {
	short first, n;			/* its styles: fo[first] ... */
} FSFAM;

static FSF *fo, fo_sys;		/* the fonts listed, the system font first */
static FSFAM *fa, fa_sys;
static short nfo, nfa;
static short sizes[MAXSIZE], nsizes;
static char size_label[MAXSIZE][8];
static short fs_sys_cw, fs_sys_ch;	/* the system font's cell, as font_sys_push left it */
static short xfnt = -1;		/* vqt_xfntinfo answers: -1 not asked yet */
static XFNT_INFO xi;

/* Unicode of the Hebrew letters in the Atari order (0xC2-0xDC), less 0x5D0 */
static const unsigned char heb_uni[27] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	17, 18, 20, 22, 23, 24, 25, 26, 15, 10, 13, 19, 21
};

static int is_vector(short format)
{
	return format == 2 || format == 4 || format == 8;
}

/* the dialog's own text: the system font at the AES's size */
static void sys_font(void)
{
	short w, h;
	vst_font(vdi_h, 1);
	vst_height(vdi_h, gl_hchar >= 16 ? 13 : 6, &w, &h);
	cw = fs_sys_cw;
	ch = fs_sys_ch;
}

short fontsel_hebrew(short id, short format, unsigned short *idx, short *why)
{
	short k;
	long a;
	*why = 0;
	/* a bitmap font draws the Atari codes itself; a vector font (or one
	   whose kind the VDI doesn't say) is asked for its Hebrew letters */
	if (id == 1 || format == 1)
		return FS_HEB_MAP;
	a = vqt_char_index(vdi_h, 'A', 1, 0);
	if (a > 0) {
		for (k = 0; k < 27; k++) {
			long i = vqt_char_index(vdi_h, (unsigned short)(0x5D0 + heb_uni[k]), 2, 0);
			if (i <= 0)
				break;
			if (idx)
				idx[k] = (unsigned short)i;
		}
		if (k == 27)
			return FS_HEB_UNICODE;
		*why = FS_WHY_NOHEB;
		return FS_HEB_SYSTEM;
	}
	if (format) {
		*why = FS_WHY_NOLOOKUP;	/* a vector font, and no NVDI 5 to ask */
		return FS_HEB_SYSTEM;
	}
	return FS_HEB_MAP;
}

/* whether the program can use the font: fixed-width ('i', 'W' and 'm'
   as wide at 12 points) and with letters. A vector font whose VDI looks
   characters up (a space is found) but has no 'A' by Unicode has
   symbols in their place */
static int usable(short id, short format, const char *name)
{
	short w, h, iw;
	long sp, ua;
	if (fontsel_symbols(name) || vst_font(vdi_h, id) != id)
		return 0;
	vst_point(vdi_h, 12, &w, &h);
	if (w < 1)
		return 0;
	iw = vqt_width(vdi_h, 'i');
	if (iw != vqt_width(vdi_h, 'W') || iw != vqt_width(vdi_h, 'm'))
		return 0;
	if (!is_vector(format))
		return 1;
	sp = vqt_char_index(vdi_h, ' ', 1, 0);
	ua = vqt_char_index(vdi_h, 'A', 2, 0);
	TRACE("fontsel: %d '%s' format %d: space %ld, U+0041 %ld\n", id, name, format, sp, ua);
	return sp < 0 || ua >= 0;
}

/* the kind of font (1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1; 0 not
   known): vqt_name's answer, else vqt_xfntinfo's (NVDI 5 answers only
   that one) */
static short format_of(short id, short index)
{
	short f = vqt_font_format(vdi_h, index);
	if (!f && xfnt && vqt_xfntinfo(vdi_h, 0, id, 0, &xi))
		f = xi.format;
	return f;
}

/* what the VDI calls the font, or what its name says */
static void names_of(FSF *f)
{
	if (xfnt && vqt_xfntinfo(vdi_h, XFNT_NAME | XFNT_FAMILY | XFNT_STYLE, f->id, 0, &xi)) {
		xfnt = 1;
		if (!f->format)
			f->format = xi.format;
		xi.family_name[FS_NAME - 1] = xi.style_name[FS_NAME - 1] = 0;
		TRACE("fontsel: %d '%s': family '%s', style '%s'\n", f->id, f->name, xi.family_name, xi.style_name);
		if (xi.family_name[0]) {
			strcpy(f->family, xi.family_name);
			strcpy(f->style, xi.style_name[0] ? xi.style_name : "Regular");
			return;
		}
	} else if (xfnt < 0) {
		xfnt = 0;
	}
	fontsel_split(f->name, f->family, f->style);
}

static const char *format_name(short format)
{
	switch (format) {
	case 1: return "Bitmap (GDOS)";
	case 2: return "Speedo";
	case 4: return "TrueType";
	case 8: return "Type 1";
	}
	return "GDOS font";
}

/* NAME.FNT's answer for a font: 1 listed, 0 not, -1 to be measured
   (new, or a "1" of a file before FNT2, from before the letters were
   looked at) */
static short known_use(const char *known, int v2, const char *name)
{
	const char *line;
	size_t n = strlen(name);
	for (line = known; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : 0)
		if ((line[0] == '0' || line[0] == '1') && line[1] == ' ' && !strncmp(line + 2, name, n) &&
		    strchr("\t\r\n", line[2 + n]) && line[2 + n])
			return line[0] == '0' ? 0 : v2 ? 1 : -1;
	return -1;
}

static void name_trim(char *name)
{
	short n;
	for (n = 31; n >= 0 && (name[n] == ' ' || !name[n]); n--)
		name[n] = 0;
}

static int cmp_font(const FSF *a, const FSF *b)
{
	const char *p = a->family, *q = b->family;
	while (*p && low(*p) == low(*q))
		p++, q++;
	if (low(*p) != low(*q))
		return low(*p) < low(*q) ? -1 : 1;
	if (a->rank != b->rank)
		return a->rank < b->rank ? -1 : 1;
	return strcmp(a->name, b->name);
}

static void fonts_free(void)
{
	if (fo && fo != &fo_sys)
		free(fo);
	if (fa && fa != &fa_sys)
		free(fa);
	fo = 0;
	fa = 0;
	nfo = nfa = 0;
}

/* the progress of the first look, in the sample's box */
static void show_progress(GRECT *r, short done, short todo)
{
	char m[64];
	if (!r)
		return;
	snprintf(m, sizeof(m), "Measuring the fonts: %d of %d...", done, todo);
	sys_font();			/* (the last font measured was set) */
	graf_mouse(M_OFF, 0);
	clip_on(r);
	fill(r, 0);
	text_at(r->x + gl_wchar, r->y + 2, m, (long)strlen(m), (r->w - 2 * gl_wchar) / gl_wchar, TX_LTR);
	clip_off();
	graf_mouse(M_ON, 0);
}

/* the accessory without its fonts: the ones NAME.FNT knows, for the next
   start (they were measured when last loaded, by the program or at a
   start with one chosen) */
static void known_fonts(const char *known)
{
	const char *line;
	short k, n = 1;
	fs_line l;
	for (line = known; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : 0)
		n++;
	fo = malloc(n * sizeof(FSF));
	if (!fo)
		return;
	nfo = 1;
	for (line = known; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : 0) {
		FSF *f = &fo[nfo];
		if (!fontsel_fnt_line(line, &l) || l.use != 1 || l.id <= 1 || fontsel_symbols(l.name))
			continue;
		for (k = 1; k < nfo && fo[k].id != l.id; k++)
			;
		if (k < nfo)
			continue;
		f->id = l.id;
		f->format = l.format;
		f->later = 1;
		strcpy(f->name, l.name);
		strcpy(f->family, l.family);
		strcpy(f->style, l.style);
		nfo++;
	}
}

/* the fonts the program can use, into fo (sorted by family and style)
   and fa (one per family). Names first, quick whatever their number;
   then the fonts NAME.FNT doesn't know are measured, and NAME.FNT
   written again */
static void collect(fontsel *s, GRECT *show)
{
	short n = s->gdos > 0 ? s->gdos : 0, i, k, todo = 0, done = 0, nuse = 0;
	char *known = s->fnt ? pf_load(s->fnt, 0) : 0, name[34];
	int v2 = known && !strncmp(known, FS_FNT_HEAD, 5);
	signed char *use = 0;
	short *ids = 0;

	TRACE_MS("collect");
	fonts_free();
	if (s->gdos < 0) {
		if (s->acc && vq_gdos())
			known_fonts(known);
		n = 0;
	} else if (n) {
		use = malloc(n);
		ids = malloc(n * sizeof(short));
		if (!use || !ids)
			n = 0;		/* the system font, at least */
	}
	/* each font once (a bitmap font has an index for each size) */
	for (i = 0; i < n; i++) {
		ids[i] = vqt_name(vdi_h, i + 2, name);
		name_trim(name);
		use[i] = -2;
		if (ids[i] <= 1)
			continue;
		for (k = 0; k < i && ids[k] != ids[i]; k++)
			;
		if (k < i)
			continue;
		use[i] = (signed char)known_use(known, v2, name);
		if (use[i] < 0)
			todo++;
	}
	TRACE("fontsel: %d GDOS fonts, %d to measure, NAME.FNT %s\n", n, todo,
	      v2 ? "FNT2" : known ? "older" : "none");
	if (todo) {
		graf_mouse(BUSYBEE, 0);
		for (i = 0; i < n; i++)
			if (use[i] == -1) {
				show_progress(show, ++done, todo);
				vqt_name(vdi_h, i + 2, name);
				name_trim(name);
				use[i] = (signed char)usable(ids[i], format_of(ids[i], i + 2), name);
			}
		graf_mouse(ARROW, 0);
		sys_font();
	}
	TRACE_MS("measured");
	for (i = 0; i < n; i++)
		if (use[i] == 1)
			nuse++;
	if (n) {
		fo = malloc((nuse + 1) * sizeof(FSF));
		nfo = 1;
		for (i = 0; fo && i < n; i++)
			if (use[i] == 1) {
				FSF *f = &fo[nfo++];
				f->id = vqt_name(vdi_h, i + 2, f->name);
				name_trim(f->name);
				f->format = vqt_font_format(vdi_h, i + 2);
				f->later = 0;
				names_of(f);
			}
	}
	TRACE_MS("named");
	/* NAME.FNT again when something was measured, or it was older */
	if (n && fo && s->fnt && (todo || !v2)) {
		long size = 8 + (long)n * (FS_NAME + 4) + (long)nuse * (2 * FS_NAME + 16), len;
		char *out = malloc(size);
		if (out) {
			len = sprintf(out, "%s\r\n", FS_FNT_HEAD);
			for (i = 0; i < n; i++) {
				if (use[i] < 0)
					continue;
				vqt_name(vdi_h, i + 2, name);
				name_trim(name);
				if (use[i]) {
					for (k = 1; k < nfo && fo[k].id != ids[i]; k++)
						;
					len += sprintf(out + len, "1 %s\t%d\t%s\t%s\t%d\r\n", name, ids[i],
						       fo[k].family, fo[k].style, fo[k].format);
				} else {
					len += sprintf(out + len, "0 %s\r\n", name);
				}
			}
			pf_save(s->fnt, out, len);
			free(out);
		}
	}
	TRACE_MS("written");
	free(use);
	free(ids);
	free(known);
	if (!fo) {
		fo = &fo_sys;
		nfo = 1;
	}
	fo[0].id = 1;
	fo[0].format = 1;
	fo[0].later = 0;
	strcpy(fo[0].name, "System font");
	strcpy(fo[0].family, "System font");
	strcpy(fo[0].style, "Regular");
	for (i = 1; i < nfo; i++)
		fo[i].rank = fontsel_rank(fo[i].style);
	/* by family, then style (few: an insertion sort) */
	for (i = 2; i < nfo; i++) {
		FSF t = fo[i];
		for (k = i; k > 1 && cmp_font(&t, &fo[k - 1]) < 0; k--)
			fo[k] = fo[k - 1];
		fo[k] = t;
	}
	fa = malloc(nfo * sizeof(FSFAM));
	if (!fa) {
		fa = &fa_sys;
		nfo = 1;
	}
	nfa = 0;
	for (i = 0; i < nfo; i++) {
		if (i == 0 || strcmp(fo[i].family, fo[i - 1].family)) {
			fa[nfa].first = i;
			fa[nfa].n = 0;
			nfa++;
		} else if (!strcmp(fo[i].style, fo[i - 1].style) && strlen(fo[i].style) + 12 < FS_NAME) {
			/* the same style twice (a Speedo and a TrueType copy) */
			strcat(fo[i].style, " (");
			strcat(fo[i].style, format_name(fo[i].format));
			strcat(fo[i].style, ")");
		}
		fa[nfa - 1].n++;
	}
}

int fontsel_usable(fontsel *s, short id)
{
	short i, r = 0;
	if (id == 1)
		return 1;
	font_sys_push();
	fs_sys_cw = cw;
	fs_sys_ch = ch;
	collect(s, 0);
	for (i = 1; i < nfo; i++)
		if (fo[i].id == id && !fo[i].later)
			r = 1;
	fonts_free();
	font_sys_pop();
	return r;
}

/* the sizes the font draws: the system font's two, a font of the next
   start the usual ones, else those the VDI gives for 6 to 36 points
   (a bitmap font's own; cells of 4 x 6 at least) */
static void sizes_of(FSF *f)
{
	static const short try_pt[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36 };
	static const short usual[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24 };
	short i, w, h;
	nsizes = 0;
	if (f->id == 1) {
		sizes[0] = 8;
		strcpy(size_label[0], "8 x 8");
		sizes[1] = 16;
		strcpy(size_label[1], "8 x 16");
		nsizes = 2;
		return;
	}
	if (f->later) {
		for (i = 0; i < (short)(sizeof(usual) / sizeof(usual[0])); i++)
			sizes[nsizes++] = usual[i];
	} else if (vst_font(vdi_h, f->id) == f->id) {
		for (i = 0; i < (short)(sizeof(try_pt) / sizeof(try_pt[0])) && nsizes < MAXSIZE; i++) {
			short got = vst_point(vdi_h, try_pt[i], &w, &h);
			if (w < 4 || h < 6 || (nsizes && sizes[nsizes - 1] >= got))
				continue;
			sizes[nsizes++] = got;
		}
		sys_font();
	}
	for (i = 0; i < nsizes; i++)
		snprintf(size_label[i], sizeof(size_label[0]), "%d", sizes[i]);
}

/* the size used for want: any from 4 to 255 points in a vector font,
   else the font's own at or below it (its smallest, if want is less) */
static short fit_pt(FSF *f, short want)
{
	short k, best;
	if (!nsizes)
		return 0;
	if (is_vector(f->format) && !f->later && f->id != 1) {
		/* the size the VDI draws for it (NVDI: the nearest of its
		   table, as vst_point gives the programs) */
		short w, h, got;
		want = want < 4 ? 4 : want > 255 ? 255 : want;
		if (vst_font(vdi_h, f->id) == f->id) {
			got = vst_point(vdi_h, want, &w, &h);
			sys_font();
			if (got >= 4 && w >= 4 && h >= 6)
				return got;
		}
	}
	best = sizes[0];
	for (k = 0; k < nsizes; k++)
		if (sizes[k] <= want)
			best = sizes[k];
	return best;
}

static short size_index(short pt)
{
	short k;
	for (k = 0; k < nsizes; k++)
		if (sizes[k] == pt)
			return k;
	return -1;
}

/* ---------------- the dialog's fs_tree, built in code ---------------- */

#define DMAX 20
static OBJECT fs_tree[DMAX];
static short nobj, last_child;

static void tr_begin(short w, short h)
{
	memset(fs_tree, 0, sizeof(fs_tree));
	nobj = 1;
	last_child = 0;
	fs_tree[0].ob_next = -1;
	fs_tree[0].ob_head = fs_tree[0].ob_tail = -1;
	fs_tree[0].ob_type = G_BOX;
	fs_tree[0].ob_state = OUTLINED;
	fs_tree[0].ob_spec = 0x00021100L;
	fs_tree[0].ob_width = w;
	fs_tree[0].ob_height = h;
}

static short tr_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &fs_tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		fs_tree[last_child].ob_next = i;
	else
		fs_tree[0].ob_head = i;
	fs_tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static short tr_text(short x, short y, const char *s)
{
	return tr_add(G_STRING, 0, 0, (long)s, x, y, (short)strlen(s), 1);
}

static void tr_end(void)
{
	short i;
	fs_tree[last_child].ob_next = 0;
	fs_tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(fs_tree, i);
}

/* ---------------- a list ---------------- */

enum { L_FAM, L_STYLE, L_SIZE };	/* the lists */

typedef struct {
	short obj;		/* its G_BOX */
	GRECT r;		/* inside the border */
	short n, top, sel, rows, kind;
} LIST;

static LIST lists[3];
#define lf lists[L_FAM]
#define lst lists[L_STYLE]
#define ls lists[L_SIZE]

static short focus;		/* L_* */

static FSF *chosen(void)
{
	return &fo[fa[lf.sel].first + (lst.sel >= 0 ? lst.sel : 0)];
}

static const char *row_text(LIST *l, short k)
{
	if (l->kind == L_FAM)
		return fo[fa[k].first].family;
	if (l->kind == L_STYLE)
		return fo[fa[lf.sel].first + k].style;
	return size_label[k];
}

static short sb_width(void)
{
	return 2 * gl_wchar;
}

static void fl_place(LIST *l)
{
	short x, y;
	objc_offset(fs_tree, l->obj, &x, &y);
	l->r.x = x + 1;
	l->r.y = y + 1;
	l->r.w = fs_tree[l->obj].ob_width - 2;
	l->r.h = fs_tree[l->obj].ob_height - 2;
	l->rows = l->r.h / gl_hchar;
}

static void edge(short x, short y, short w, short h, short color)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	frame(&r, color);
}

static void sb_arrow(short x, short y, short w, short h, int up)
{
	short cx = x + w / 2, cy = y + h / 2, k = h >= 14 ? 3 : 2;
	short y1 = up ? cy + k / 2 + 1 : cy - k / 2 - 1, y2 = up ? cy - k / 2 - 1 : cy + k / 2 + 1;
	edge(x, y, w, h, 1);
	line_col(cx - k, y1, cx, y2, 1);
	line_col(cx, y2, cx + k, y1, 1);
}

/* the ring round the list the keys go to */
static void focus_ring(LIST *l, short on)
{
	graf_mouse(M_OFF, 0);
	edge(l->r.x - 3, l->r.y - 3, l->r.w + 6, l->r.h + 6, on);
	graf_mouse(M_ON, 0);
}

static void fl_draw(LIST *l)
{
	GRECT r = l->r, t;
	short i, sw = sb_width(), cols = (r.w - sw - 4) / gl_wchar, ah = gl_hchar;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	for (i = 0; i < l->rows && l->top + i < l->n; i++) {
		short k = l->top + i, y = r.y + i * gl_hchar, fl = TX_LTR;
		const char *s = row_text(l, k);
		if (k == l->sel) {
			t.x = r.x;
			t.y = y;
			t.w = r.w - sw;
			t.h = gl_hchar;
			fill(&t, 1);
			fl |= TX_INVERSE;
		}
		text_at(r.x + 2, y, s, (long)strlen(s), cols, fl);
	}
	/* the scroll bar: the arrows, the track and the slider */
	t.x = r.x + r.w - sw;
	t.y = r.y;
	t.w = sw;
	t.h = r.h;
	fill(&t, 0);
	sb_arrow(t.x, t.y, sw, ah, 1);
	sb_arrow(t.x, t.y + t.h - ah, sw, ah, 0);
	if (l->n > l->rows) {
		short ty = t.y + ah, th = t.h - 2 * ah, h = (short)((long)th * l->rows / l->n), y;
		if (h < 6)
			h = 6;
		y = ty + (short)((long)(th - h) * l->top / (l->n - l->rows));
		t.y = y;
		t.h = h;
		fill(&t, 1);
	}
	edge(r.x + r.w - sw, r.y, sw, r.h, 1);
	clip_off();
	graf_mouse(M_ON, 0);
}

static void fl_show(LIST *l, short k)
{
	if (k < l->top)
		l->top = k;
	if (k >= l->top + l->rows)
		l->top = k - l->rows + 1;
	if (l->top > l->n - l->rows)
		l->top = l->n - l->rows;
	if (l->top < 0)
		l->top = 0;
}

/* ---------------- the dialog ---------------- */

#define D_SIZES   1		/* the font changed: its sizes to look at */
#define D_PREVIEW 4		/* the type line and the sample to draw */

static short o_ok, o_cancel, o_type, o_prev;
static short dirty, pt, want;
static fontsel *fs;

static void tr_draw(short obj)
{
	short x, y;
	objc_offset(fs_tree, obj, &x, &y);
	graf_mouse(M_OFF, 0);
	objc_draw(fs_tree, 0, 8, x - 2, y - 2, fs_tree[obj].ob_width + 4, fs_tree[obj].ob_height + 4);
	graf_mouse(M_ON, 0);
}

static void tr_box(short obj, GRECT *r, short in)
{
	short x, y;
	objc_offset(fs_tree, obj, &x, &y);
	r->x = x + in;
	r->y = y + in;
	r->w = fs_tree[obj].ob_width - 2 * in;
	r->h = fs_tree[obj].ob_height - 2 * in;
}

static void ok_state(void)
{
	short dis = !nsizes;
	if (dis == ((fs_tree[o_ok].ob_state & DISABLED) != 0))
		return;
	fs_tree[o_ok].ob_state ^= DISABLED;
	tr_draw(o_ok);
}

static void draw_type(void)
{
	FSF *f = chosen();
	char line[64];
	GRECT r;
	short why;
	if (f->later)
		snprintf(line, sizeof(line), "GDOS font: used from the next start");
	else if (f->id == 1 && nfo == 1 && fs->acc && fs->gdos < 0 && vq_gdos()) {
		/* none known yet: the accessory doesn't load fonts after its
		   start (2.2); the program's Font... looks at them for it */
		char prg[16];
		short i;
		for (i = 0; fs->app && fs->app[i] && i < 8; i++)
			prg[i] = fs->app[i] >= 'a' && fs->app[i] <= 'z' ? (char)(fs->app[i] - 32) : fs->app[i];
		prg[i] = 0;
		snprintf(line, sizeof(line), "GDOS fonts: Font... in %s.PRG", prg);
	} else if (f->id == 1) {
		snprintf(line, sizeof(line), "Bitmap (built in)");
	} else if (is_vector(f->format) || !f->format) {
		/* setting it says where its Hebrew comes from */
		short w, h, m;
		vst_font(vdi_h, f->id);
		vst_point(vdi_h, pt ? pt : 12, &w, &h);
		m = fontsel_hebrew(f->id, f->format, 0, &why);
		sys_font();
		/* (the line has 38 columns) */
		snprintf(line, sizeof(line), "%s, %s", format_name(f->format),
			 m == FS_HEB_UNICODE ? "Hebrew from the font" :
			 why == FS_WHY_NOHEB ? "system font's Hebrew" :
			 why == FS_WHY_NOLOOKUP ? "system Hebrew (no NVDI 5)" : "Hebrew as the font has it");
	} else {
		snprintf(line, sizeof(line), "%s", format_name(f->format));
	}
	tr_box(o_type, &r, 0);
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	text_at(r.x, r.y, line, (long)strlen(line), r.w / gl_wchar, TX_LTR);
	clip_off();
	graf_mouse(M_ON, 0);
}

static void draw_preview(void)
{
	FSF *f = chosen();
	GRECT r;
	short cols, w, h;
	tr_box(o_prev, &r, 1);
	cols = (r.w - 2 * gl_wchar) / gl_wchar;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	if (f->later) {
		static const char l1[] = "Used from the next start: the accessory";
		static const char l2[] = "loads its fonts only as the Atari starts.";
		text_at(r.x + gl_wchar, r.y + 2, l1, (long)strlen(l1), cols, TX_LTR);
		text_at(r.x + gl_wchar, r.y + 2 + gl_hchar, l2, (long)strlen(l2), cols, TX_LTR);
	} else if (nsizes) {
		GRECT c = r;		/* half a cell's margin at each side: the
					   system font's sample fits whole */
		c.x += gl_wchar / 2;
		c.w -= gl_wchar;
		clip_on(&c);
		if (f->id == 1) {
			vst_font(vdi_h, 1);
			vst_height(vdi_h, pt >= 16 ? 13 : 6, &w, &h);
		} else {
			vst_font(vdi_h, f->id);
			vst_point(vdi_h, pt, &w, &h);
		}
		/* (h: the cell's height) in the middle of the box, or from its
		   top when it is taller */
		v_gtext_n(vdi_h, c.x, h < r.h ? r.y + (r.h - h) / 2 : r.y, FS_SAMPLE,
			  (short)strlen(FS_SAMPLE));
		sys_font();
	}
	clip_off();
	graf_mouse(M_ON, 0);
}

/* the size list for the font now chosen, and the size kept as near the
   one last chosen as the font has */
static void sizes_now(void)
{
	FSF *f = chosen();
	sizes_of(f);
	pt = fit_pt(f, want);
	ls.n = nsizes;
	ls.sel = size_index(pt);
	ls.top = 0;
	fl_show(&ls, ls.sel < 0 ? 0 : ls.sel);
	fl_draw(&ls);
	ok_state();
}

/* what waits for the keys to stop */
static void catch_up(void)
{
	if (dirty & D_SIZES)
		sizes_now();
	if (dirty) {
		draw_type();
		draw_preview();
	}
	dirty = 0;
}

static void focus_set(short f)
{
	if (f == focus)
		return;
	if (focus >= 0)
		focus_ring(&lists[focus], 0);
	focus = f;
	focus_ring(&lists[focus], 1);
}

/* the family's styles: the one of the same name as before, else the
   first (Regular, as a rule) */
static void family_set(short k)
{
	char was[FS_NAME];
	short i;
	strcpy(was, lst.n ? chosen()->style : "");
	lf.sel = k;
	fl_show(&lf, k);
	fl_draw(&lf);
	lst.n = fa[k].n;
	lst.sel = 0;
	for (i = 0; i < lst.n; i++)
		if (!strcmp(fo[fa[k].first + i].style, was))
			lst.sel = i;
	lst.top = 0;
	fl_show(&lst, lst.sel);
	fl_draw(&lst);
	dirty |= D_SIZES | D_PREVIEW;
}

static void style_set(short k)
{
	lst.sel = k;
	fl_show(&lst, k);
	fl_draw(&lst);
	dirty |= D_SIZES | D_PREVIEW;
}

static void size_set(short k)
{
	if (dirty & D_SIZES)
		catch_up();
	if (k < 0 || k >= nsizes)
		return;
	ls.sel = k;
	pt = want = sizes[k];
	fl_show(&ls, k);
	fl_draw(&ls);
	dirty |= D_PREVIEW;
}

static void fl_pick(LIST *l, short k)
{
	if (k < 0)
		k = 0;
	if (k >= l->n)
		k = l->n - 1;
	if (k < 0 || (k == l->sel && l->kind != L_SIZE))
		return;
	if (l->kind == L_FAM)
		family_set(k);
	else if (l->kind == L_STYLE)
		style_set(k);
	else
		size_set(k);
}

static short left_down(void)
{
	short mx, my, mb, ks;
	graf_mkstate(&mx, &my, &mb, &ks);
	return mb & 1;
}

static void left_up(void)
{
	while (left_down())
		evnt_timer_(10);
}

/* a press in a list: its scroll bar, or a row (1: a double click on one) */
static int fl_click(LIST *l, short mx, short my, short clicks)
{
	short sw = sb_width(), ah = gl_hchar;
	if (mx >= l->r.x + l->r.w - sw) {
		if (my < l->r.y + ah || my >= l->r.y + l->r.h - ah) {
			short d = my < l->r.y + ah ? -1 : 1, first = 1;
			do {
				short old = l->top;
				l->top += d;
				fl_show(l, l->top < old ? l->top : l->top + l->rows - 1);
				if (l->top != old)
					fl_draw(l);
				evnt_timer_(first ? 250 : 50);
				first = 0;
			} while (left_down());
		} else {
			short mid = l->r.y + l->r.h / 2;
			l->top += my < mid ? -l->rows : l->rows;
			fl_show(l, l->top < 0 ? 0 : l->top);
			if (l->top > l->n - l->rows)
				l->top = l->n - l->rows;
			if (l->top < 0)
				l->top = 0;
			fl_draw(l);
		}
		return 0;
	}
	{
		short k = l->top + (my - l->r.y) / gl_hchar;
		if (k < 0 || k >= l->n)
			return 0;
		fl_pick(l, k);
		return clicks >= 2;
	}
}

static int rect_in(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* a key; 1 OK, -1 Cancel, 0 on */
static int on_key(short k, short kstate)
{
	short sc = (k >> 8) & 0xff, as = k & 0xff, shift = kstate & (K_LSHIFT | K_RSHIFT), i;
	LIST *l = &lists[focus];
	if (dlg_esc(k))
		return -1;
	if (as == 0x0d || sc == 0x72 || sc == 0x1c)
		return 1;
	if (sc == 0x0f) {		/* Tab */
		focus_set((focus + (shift ? 2 : 1)) % 3);
		return 0;
	}
	switch (sc) {
	case 0x48:			/* Up, Down */
	case 0x50:
		fl_pick(l, l->sel + (sc == 0x48 ? -1 : 1) * (shift ? l->rows - 1 : 1));
		return 0;
	case 0x47:			/* Home: the first; Shift: the last */
		fl_pick(l, shift ? l->n - 1 : 0);
		return 0;
	case 0x4b:			/* Left, Right */
		focus_set(focus ? focus - 1 : L_SIZE);
		return 0;
	case 0x4d:
		focus_set(focus == L_SIZE ? L_FAM : focus + 1);
		return 0;
	}
	if ((as >= 'a' && as <= 'z') || (as >= 'A' && as <= 'Z')) {
		for (i = 1; i <= lf.n; i++) {
			short j = (lf.sel + i) % lf.n;
			if (low(fo[fa[j].first].family[0]) == low((char)as)) {
				if (focus != L_FAM)
					focus_set(L_FAM);
				family_set(j);
				break;
			}
		}
	}
	return 0;
}

/* a press of the button (never the pointer's rest); 1 OK, -1 Cancel */
static int on_click(short mx, short my, short clicks)
{
	short obj = objc_find(fs_tree, 0, 8, mx, my), i;
	for (i = 0; i < 3; i++)
		if (rect_in(&lists[i].r, mx, my)) {
			if (focus != i)
				focus_set(i);
			if (fl_click(&lists[i], mx, my, clicks) && nsizes)
				return 1;
			return 0;
		}
	if ((obj == o_ok && !(fs_tree[o_ok].ob_state & DISABLED)) || obj == o_cancel) {
		fs_tree[obj].ob_state |= SELECTED;
		tr_draw(obj);
		left_up();
		fs_tree[obj].ob_state &= ~SELECTED;
		return obj == o_ok ? 1 : -1;
	}
	return 0;
}

int fontsel_run(fontsel *s)
{
	short i, x, y, w, h, m[8], up = 0, done = 0, o_size;
	AEVENT e;
	GRECT prev;

	fs = s;
	font_sys_push();
	fs_sys_cw = cw;
	fs_sys_ch = ch;
	/* the program loads the GDOS fonts when they're first wanted; the
	   accessory never here (2.2): it had them at boot, or not */
	if (s->gdos < 0 && !s->acc && vq_gdos()) {
		graf_mouse(BUSYBEE, 0);
		s->gdos = vst_load_fonts(vdi_h, 0);
		graf_mouse(ARROW, 0);
		sys_font();
	}

	/* 48 x 17 cells: ST medium's and ST high's rows under the menu bar,
	   a quarter smaller than the first (Erez) */
	tr_begin(48, 17);
	tr_text(2, 1, "Family");
	tr_text(30, 1, "Style");
	lf.obj = tr_add(G_BOX, 0, 0, 0x00FF1100L, 2, 2, 26, 9);
	lst.obj = tr_add(G_BOX, 0, 0, 0x00FF1100L, 30, 2, 16, 4);
	o_size = tr_text(30, 6, "Size");
	ls.obj = tr_add(G_BOX, 0, 0, 0x00FF1100L, 30, 7, 10, 4);
	tr_text(2, 11, "Type:");
	o_type = tr_add(G_IBOX, 0, 0, 0L, 8, 11, 38, 1);
	o_prev = tr_add(G_BOX, 0, 0, 0x00FF1100L, 2, 12, 44, 3);
	o_ok = tr_add(G_BUTTON, SELECTABLE | EXIT | DEFAULT, 0, (long)"OK", 26, 15, 9, 1);
	o_cancel = tr_add(G_BUTTON, SELECTABLE | EXIT, 0, (long)"Cancel", 37, 15, 9, 1);
	tr_end();
	/* the borders and focus rings are drawn outside the boxes: the
	   lists and Size a few pixels down, so no label loses its top or
	   its descenders (no list loses a row) */
	fs_tree[o_size].ob_y += 2;
	fs_tree[o_type - 1].ob_y += 3;		/* Type: and its line, under the lists */
	fs_tree[o_type].ob_y += 3;
	fs_tree[o_prev].ob_y += 5;		/* the sample, under the Type line */
	fs_tree[o_ok].ob_y += 12;		/* OK and Cancel, under the sample */
	fs_tree[o_cancel].ob_y += 12;
	fs_tree[0].ob_height += 14;
	for (i = 0; i < 3; i++) {
		fs_tree[lists[i].obj].ob_y += 3;
		fs_tree[lists[i].obj].ob_height -= 3;
	}
	for (i = 0; i < 3; i++) {
		lists[i].kind = i;
		lists[i].n = 0;
		lists[i].top = 0;
		lists[i].sel = -1;
	}

	form_center(fs_tree, &x, &y, &w, &h);
	wind_update(BEG_UPDATE);
	wind_update(3);			/* BEG_MCTRL */
	form_dial(FMD_START, x, y, w, h);
	graf_mouse(M_OFF, 0);
	objc_draw(fs_tree, 0, 8, x, y, w, h);
	graf_mouse(M_ON, 0);
	for (i = 0; i < 3; i++)
		fl_place(&lists[i]);
	tr_box(o_prev, &prev, 1);

	collect(s, &prev);
	/* the font in use: by its id, else its name (or its family's) */
	{
		short k = -1;
		for (i = 0; i < nfo && k < 0; i++)
			if (fo[i].id == s->id)
				k = i;
		for (i = 0; s->name && i < nfo && k < 0; i++)
			if (!strcmp(fo[i].name, s->name))
				k = i;
		for (i = 0; s->name && i < nfo && k < 0; i++)
			if (!strcmp(fo[i].family, s->name))
				k = i;
		if (k < 0)
			k = 0;
		for (i = 0; i < nfa; i++)
			if (k >= fa[i].first && k < fa[i].first + fa[i].n) {
				lf.sel = i;
				lst.sel = k - fa[i].first;
			}
	}
	want = s->pt;
	if (fo[fa[lf.sel].first].id == 1 && want != 8 && want != 16)
		want = gl_hchar >= 16 ? 16 : 8;
	else if (want <= 0)
		want = 10;
	lf.n = nfa;
	lst.n = fa[lf.sel].n;
	fl_show(&lf, lf.sel);
	fl_show(&lst, lst.sel);
	focus = L_FAM;
	fl_draw(&lf);
	fl_draw(&lst);
	focus_ring(&lf, 1);
	sizes_now();
	draw_type();
	draw_preview();
	dirty = 0;

	while (!done) {
		short flags = MU_KEYBD | MU_BUTTON;
		unsigned long t = 0;
		if (dirty)
			flags |= MU_TIMER;
		if (s->message) {
			flags |= MU_MESAG | MU_TIMER;
			t = dirty ? 0 : 250;
		}
		/* a press when the button is up, else its release first: a
		   button still down from the menu is no click */
		evnt_multi_(flags, up ? 2 : 1, 1, up ? 1 : 0, t, m, &e);
		if ((e.which & MU_MESAG) && s->message)
			s->message(m);
		if (e.which & MU_KEYBD)
			done = on_key(e.kreturn, e.kstate);
		if ((e.which & MU_BUTTON) && !done) {
			if (up) {
				done = on_click(e.mx, e.my, e.breturn);
				up = 0;
			} else {
				up = 1;
			}
		}
		if ((e.which & MU_TIMER) && !(e.which & (MU_KEYBD | MU_BUTTON))) {
			if (dirty)
				catch_up();
			else if (s->message)
				s->message(0);
		}
		if (done > 0) {
			if (dirty)
				catch_up();
			if (!nsizes)
				done = 0;	/* a font the VDI no longer has */
		}
	}
	form_dial(FMD_FINISH, x, y, w, h);
	clip_off();			/* no clip left on vdi_h: NVDI clips raster copies with it */
	wind_update(2);			/* END_MCTRL */
	wind_update(END_UPDATE);
	if (done > 0) {
		FSF *f = chosen();
		s->id = f->id;
		s->pt = pt;
		s->later = f->later;
		s->format = f->format;
		strcpy(s->font, f->name);
		strcpy(s->family, f->family);
		strcpy(s->style, f->style);
	}
	fonts_free();
	font_sys_pop();
	if (done > 0 && s->apply)
		s->apply(s);
	return done > 0;
}

#endif
