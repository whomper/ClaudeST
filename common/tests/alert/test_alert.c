/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * The shared alert() on the build machine (ui/alert.c, ../../src/bidi.c).
 * The dialog loop is a stand-in that reads the alert back from the tree
 * it is given, as the string form_alert would have had, checks the
 * tree's layout and answers with the button the test asks for; the
 * stand-in form_alert writes down what it was given. Hebrew is in the
 * Atari's character set (0xC2-0xDC).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "gem.h"
#include "dialog.h"
#include "alert.h"

static char shown[1000];
static char via;			/* 'T' our tree, 'F' form_alert */
static int failed, checks;
static short cell_w = 8, cell_h = 16, desk_w = 640, desk_h = 381;
static short pick;			/* 0: the cancel object; else this button */
static short got_cancel, got_start, got_default, mouse_arrow;

static int windowed;			/* a multitasking AES (MiNT, MagiC) */
int dlg_windowed(void)
{
	return windowed;
}

short form_alert(short def, const char *s)
{
	(void)def;
	via = 'F';
	snprintf(shown, sizeof(shown), "%s", s);
	return 1;
}

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
	*wchar = cell_w;
	*hchar = cell_h;
	*wbox = cell_w + 2;
	*hbox = cell_h + 3;
	return 1;
}

short graf_mouse(short num, void *form)
{
	(void)form;
	mouse_arrow = num == ARROW;
	return 1;
}

short wind_get(short h, short field, short *a, short *b, short *c, short *d)
{
	if (h != 0 || field != WF_WORKXYWH)
		return 0;
	*a = 0;
	*b = cell_h + 3;
	*c = desk_w;
	*d = desk_h;
	return 1;
}

static void check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static int overlap(const OBJECT *a, const OBJECT *b)
{
	return a->ob_x < b->ob_x + b->ob_width && b->ob_x < a->ob_x + a->ob_width &&
	       a->ob_y < b->ob_y + b->ob_height && b->ob_y < a->ob_y + a->ob_height;
}

/* which of the three bitmaps: the second row tells them apart */
static int icon_of(const OBJECT *o)
{
	const unsigned short *d = (const unsigned short *)((BITBLK *)o->ob_spec)->bi_pdata;
	return d[2] == 0x0001 ? 1 : d[2] == 0x007f ? 2 : d[2] == 0x07ff ? 3 : 9;
}

short dlg_modal_save(OBJECT *tree, short start, short cancel)
{
	short i, n = 0, icon = 0, lines = 0, buttons = 0, first = 0, defaults = 0, lastobs = 0;
	char s[1000];
	via = 'T';
	got_default = 0;
	got_cancel = cancel;
	got_start = start;
	/* the tree as the AES walks it */
	check(tree[0].ob_type == G_BOX && (tree[0].ob_state & OUTLINED), "the root is an outlined box");
	check(tree[0].ob_next == -1, "the root has no sibling");
	check(tree[0].ob_head == 1, "the root's first child is object 1");
	for (i = tree[0].ob_head; i > 0; i = tree[i].ob_next) {
		OBJECT *o = &tree[i];
		n++;
		check(n < 12, "the children's chain ends");
		if (n >= 12)
			break;
		check(o->ob_head == -1 && o->ob_tail == -1, "a child has no children");
		check(o->ob_x >= 0 && o->ob_y >= 0 && o->ob_x + o->ob_width <= tree[0].ob_width &&
		      o->ob_y + o->ob_height <= tree[0].ob_height, "a child inside the box");
		check(o->ob_x > 0 && o->ob_x + o->ob_width < tree[0].ob_width,
		      "a cell of room left and right");
		if (tree[i].ob_flags & LASTOB)
			lastobs++;
		if (tree[i].ob_next == 0)
			check(tree[0].ob_tail == i && (tree[i].ob_flags & LASTOB), "the last child is the tail, LASTOB");
	}
	check(lastobs == 1, "one LASTOB");
	for (i = 1; i <= n; i++) {
		short j;
		for (j = i + 1; j <= n; j++)
			check(!overlap(&tree[i], &tree[j]), "the objects don't overlap");
	}
	for (i = 1; i <= n; i++) {
		OBJECT *o = &tree[i];
		if (o->ob_type == G_IMAGE) {
			check(i == 1 && o->ob_width == 32 && o->ob_height == 32, "the icon first, 32 by 32");
			icon = icon_of(o);
		} else if (o->ob_type == G_STRING) {
			check(!buttons, "the lines before the buttons");
			check(o->ob_height == cell_h, "a line one cell high");
			check(!lines || o->ob_y == tree[i - 1].ob_y + cell_h, "the lines one under the other");
			check(o->ob_flags == 0, "a line can't be selected");
			lines++;
		} else if (o->ob_type == G_BUTTON) {
			if (!buttons)
				first = i;
			check((o->ob_flags & (SELECTABLE | EXIT)) == (SELECTABLE | EXIT), "a button: SELECTABLE, EXIT");
			check(!buttons || (o->ob_y == tree[i - 1].ob_y && o->ob_width == tree[i - 1].ob_width &&
					   o->ob_x == tree[i - 1].ob_x + o->ob_width + 2 * cell_w),
			      "the buttons in a row, as wide, 2 cells apart");
			check(lines == 0 || o->ob_y >= tree[i - buttons - 1].ob_y + 2 * cell_h,
			      "a line's room between the text and the buttons");
			if (o->ob_flags & DEFAULT) {
				defaults++;
				got_default = buttons + 1;
			}
			buttons++;
		} else {
			check(0, "only the icon, lines and buttons");
		}
	}
	check(defaults <= 1, "one DEFAULT button at most");
	check(cancel == first + buttons - 1, "Esc's object is the last button");
	check(start == 0, "no field to start in");
	check(tree[0].ob_width + 8 <= desk_w && tree[0].ob_height + 8 <= desk_h, "the box fits the desk");
	/* the string form_alert would have had */
	n = (short)snprintf(s, sizeof(s), "[%d][", icon);
	for (i = 1; i < first; i++)
		if (tree[i].ob_type == G_STRING)
			n += (short)snprintf(s + n, sizeof(s) - n, "%s%s", n > 4 ? "|" : "",
					     (const char *)tree[i].ob_spec);
	for (i = 0; i < buttons; i++)
		n += (short)snprintf(s + n, sizeof(s) - n, "%s%s", i ? "|" : "][",
				     (const char *)tree[first + i].ob_spec);
	snprintf(s + n, sizeof(s) - n, "]");
	strcpy(shown, s);
	return pick ? first + pick - 1 : cancel;
}

static void expect(const char *what, const char *want)
{
	checks++;
	if (strcmp(shown, want)) {
		failed++;
		printf("FAIL %s\n  got  %s\n  want %s\n", what, shown, want);
	} else {
		printf("ok   %s\n", what);
	}
}

static void expect_n(const char *what, long got, long want)
{
	checks++;
	if (got != want) {
		failed++;
		printf("FAIL %s: got %ld, want %ld\n", what, got, want);
	} else {
		printf("ok   %s\n", what);
	}
}

static void upper(char *line, long n)
{
	long i;
	for (i = 0; i < n; i++)
		if (line[i] >= 'a' && line[i] <= 'z')
			line[i] -= 32;
}

static void screens(void)
{
	static const struct { short cw, ch, w, h; const char *name; } scr[] = {
		{ 8, 16, 640, 381, "ST high" }, { 8, 8, 640, 189, "ST medium" },
		{ 8, 8, 320, 189, "ST low" }, { 8, 16, 640, 461, "Falcon VGA" },
		{ 8, 16, 1280, 941, "TT high" },
	};
	unsigned i;
	char what[100];
	for (i = 0; i < sizeof(scr) / sizeof(scr[0]); i++) {
		cell_w = scr[i].cw;
		cell_h = scr[i].ch;
		desk_w = scr[i].w;
		desk_h = scr[i].h;
		via = 0;
		alert(1, "[3][abcdefghijklmnopqrstuvwxyzabcd|two|three|four|five][Overwrite|Keep both|Cancel]");
		snprintf(what, sizeof(what), "%s: the widest alert, 5 lines, 3 buttons, as our own", scr[i].name);
		expect_n(what, via, 'T');
		via = 0;
		alert(1, "[2][Quit?][Quit|Cancel]");
		snprintf(what, sizeof(what), "%s: a small one, the icon taller than the text", scr[i].name);
		expect_n(what, via, 'T');
	}
	cell_w = 8;
	cell_h = 16;
	desk_w = 640;
	desk_h = 381;
}

int main(void)
{
	setvbuf(stdout, 0, _IOLBF, 0);
	alert(1, "[1][Short.][ OK ]");
	expect("a short message as it is", "[1][Short.][ OK ]");
	expect_n("shown as our own tree, not form_alert", via, 'T');
	expect_n("the mouse an arrow", mouse_arrow, 1);
	alert(1, "[3][Can't reach %s.|Check the network and try again.][ OK ]", "imap.example.com");
	expect("formatted, and a long line wrapped at a space",
	       "[3][Can't reach imap.example.com.|Check the network and try|again.][ OK ]");
	alert(1, "[1][Welcome to EMail!||Let's set up your account.][ OK ]");
	expect("an empty line becomes a space (EmuTOS reads || as |)",
	       "[1][Welcome to EMail!| |Let's set up your account.][ OK ]");
	alert(1, "[1][one|two|three|four|five|six|seven][ OK ]");
	expect("at most 5 lines", "[1][one|two|three|four|five][ OK ]");
	alert(1, "[1][a [bracket] in the text][ OK ]");
	expect("brackets in the text become parentheses", "[1][a (bracket) in the text][ OK ]");
	alert(1, "[1][abcdefghijklmnopqrstuvwxyzabcdefghij][ OK ]");
	expect("one long word broken at 30", "[1][abcdefghijklmnopqrstuvwxyzabcd|efghij][ OK ]");
	alert(2, "[2][Delete it?][Delete|Keep]");
	expect("the buttons as they were", "[2][Delete it?][Delete|Keep]");
	alert(1, "[1][Mixed \xC2\xC3\xC4 text][ OK ]");
	expect("no hook: the Hebrew word in display order", "[1][Mixed \xC4\xC3\xC2 text][ OK ]");
	alert(1, "not an alert string");
	expect("anything else straight to form_alert", "not an alert string");
	expect_n("... through form_alert", via, 'F');
	alert_hebrew = upper;
	alert(1, "[1][no hebrew here][ OK ]");
	expect("the hook only for lines with Hebrew", "[1][no hebrew here][ OK ]");
	alert(1, "[1][\xC2\xC3 shalom][ OK ]");
	expect("a Hebrew line: in display order, then the hook", "[1][SHALOM \xC3\xC2][ OK ]");
	alert_hebrew = 0;

	/* the icons */
	alert(1, "[0][No icon.][ OK ]");
	expect("icon 0: none", "[0][No icon.][ OK ]");
	alert(1, "[5][Five.][ OK ]");
	expect("another digit: the stop sign, as EmuTOS", "[3][Five.][ OK ]");

	/* the answers: 1 to n, as form_alert; Esc's object is the last */
	pick = 0;
	expect_n("Esc's object (the last button) gives 3",
		 alert(1, "[3][Can't reach it.][Retry|Skip|Cancel]"), 3);
	expect_n("... its object: 5, after the icon, a line and two buttons", got_cancel, 5);
	pick = 1;
	expect_n("the first button gives 1", alert(1, "[2][Delete it?][Delete|Keep]"), 1);
	pick = 2;
	expect_n("the second gives 2", alert(1, "[2][Delete it?][Delete|Keep]"), 2);
	pick = 0;
	expect_n("one button: Esc gives 1", alert(1, "[1][Done.][ OK ]"), 1);
	expect_n("... and its object is that button", got_cancel, 3);
	expect_n("no icon: the object numbers move up", (alert(1, "[0][x][A|B]"), got_cancel), 3);

	/* the default button */
	alert(2, "[2][Delete it?][Delete|Keep]");
	expect_n("def 2: the second button is the DEFAULT one", got_default, 2);
	alert(0, "[2][Delete it?][Delete|Keep]");
	expect_n("def 0: none", got_default, 0);
	alert(3, "[2][Delete it?][Delete|Keep]");
	expect_n("def past the last: none", got_default, 0);

	/* what goes to form_alert instead */
	via = 0;
	alert(1, "[1][Four buttons.][A|B|C|D]");
	expect_n("four buttons: form_alert", via, 'F');
	via = 0;
	alert(1, "[1][A long button.][abcdefghijklmnopqrstu]");
	expect_n("a button of 21 characters: form_alert", via, 'F');
	via = 0;
	alert(1, "[1][A button of 20.][abcdefghijklmnopqrst]");
	expect_n("one of 20: our own", via, 'T');
	via = 0;
	alert(1, "[x][Not a digit.][ OK ]");
	expect_n("an icon that isn't a digit: form_alert", via, 'F');
	via = 0;
	alert(1, "[1][][ OK ]");
	expect_n("no text: form_alert", via, 'F');
	via = 0;
	desk_w = 200;
	alert(1, "[1][A desk too narrow for it.][ OK ]");
	expect_n("a desk too narrow: form_alert", via, 'F');
	desk_w = 640;
	desk_h = 60;
	via = 0;
	alert(1, "[1][Too low.][ OK ]");
	expect_n("a desk too low: form_alert", via, 'F');
	desk_h = 381;

	/* where the AES runs several programs at once: always its own, the
	   text wrapped as ours is */
	windowed = 1;
	via = 0;
	alert(1, "[3][Can't find pop3.example.net. Check the server's name in Settings.][Retry|Cancel]");
	expect_n("a multitasking AES: form_alert", via, 'F');
	expect("... the text wrapped into lines of 30 at most",
	       "[3][Can't find pop3.example.net.|Check the server's name in|Settings.][Retry|Cancel]");
	windowed = 0;
	via = 0;
	alert(1, "[1][The folder was saved.][ OK ]");
	expect_n("TOS again: our own", via, 'T');

	screens();

	shown[0] = 0;
	checks++;
	if (!screen_check("ECal", 640) || shown[0]) {
		failed++;
		printf("FAIL screen_check: 640 wide is fine, no alert\n");
	} else {
		printf("ok   screen_check: 640 wide is fine, no alert\n");
	}
	checks++;
	if (screen_check("ECal", 320)) {
		failed++;
		printf("FAIL screen_check: 320 wide is not\n");
	} else {
		printf("ok   screen_check: 320 wide is not\n");
	}
	expect("screen_check's alert, wrapped",
	       "[3][ECal needs a screen 640 pixels|wide: ST medium or high, or a|TT or Falcon mode.][ OK ]");
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
