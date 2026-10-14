/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * The shared alert() on the build machine (ui/alert.c, ../../src/bidi.c),
 * with form_alert writing down what it was given. Hebrew is in the
 * Atari's character set (0xC2-0xDC).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "alert.h"

static char shown[1000];
static int failed, checks;

short form_alert(short def, const char *s)
{
	(void)def;
	snprintf(shown, sizeof(shown), "%s", s);
	return 1;
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

static void upper(char *line, long n)
{
	long i;
	for (i = 0; i < n; i++)
		if (line[i] >= 'a' && line[i] <= 'z')
			line[i] -= 32;
}

int main(void)
{
	alert(1, "[1][Short.][ OK ]");
	expect("a short message as it is", "[1][Short.][ OK ]");
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
	alert_hebrew = upper;
	alert(1, "[1][no hebrew here][ OK ]");
	expect("the hook only for lines with Hebrew", "[1][no hebrew here][ OK ]");
	alert(1, "[1][\xC2\xC3 shalom][ OK ]");
	expect("a Hebrew line: in display order, then the hook", "[1][SHALOM \xC3\xC2][ OK ]");
	alert_hebrew = 0;
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
