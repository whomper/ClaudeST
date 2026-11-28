/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sb_printf (src/util.c) on the build machine, with AddressSanitizer: a line
 * of any length added whole, never cut at the stack's 512 bytes (a request
 * line with a 520-byte address had lost its " HTTP/1.1": EWeb, 25 Nov 2026).
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util.h"

static int failed, checks;
static void check(int ok, const char *what)
{
	checks++;
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failed++;
}

int main(void)
{
	static const int lens[] = { 0, 1, 499, 500, 510, 511, 512, 513, 520, 1000, 4096, 70000 };
	unsigned i;
	for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
		SBUF b;
		char *url = malloc(lens[i] + 1), want[80], what[120];
		long wl;
		memset(url, 'a', lens[i]);
		url[lens[i]] = 0;
		sb_init(&b);
		sb_adds(&b, "x");
		check(sb_printf(&b, "GET /%s HTTP/1.1\r\n", url) == 1, "added");
		wl = 1 + 5 + lens[i] + 11;
		snprintf(want, sizeof(want), " HTTP/1.1\r\n");
		snprintf(what, sizeof(what), "a %d-byte address: the whole line, its end kept", lens[i]);
		check(b.len == wl && (long)strlen(b.s) == wl && !strcmp(b.s + wl - 11, want) &&
		      !memcmp(b.s, "xGET /", 6), what);
		/* and something after it, as a request's next line */
		sb_printf(&b, "Host: %s\r\n", "example.com");
		check(b.len == wl + 19 && !strcmp(b.s + wl, "Host: example.com\r\n"), "the next line after it");
		sb_free(&b);
		free(url);
	}
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
