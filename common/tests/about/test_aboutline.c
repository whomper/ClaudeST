/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ui/aboutline.c on the host: the About box's connection and memory
 * lines in the family's words (the guidelines, 1.3). make test
 */
#include <stdio.h>
#include <string.h>
#include "aboutline.h"

static int checks, failed;
static void is(const char *got, const char *want, const char *what)
{
	checks++;
	if (strcmp(got, want)) {
		failed++;
		printf("FAIL %s: \"%s\", not \"%s\"\n", what, got, want);
	}
}

#define MB (1024L * 1024)

int main(void)
{
	char b[80];
	about_net_line(b, sizeof(b), "STinG", ABOUT_DIRECT, ABOUT_ONLINE);
	is(b, "STinG  \xF9  Direct  \xF9  online", "STinG, Direct, online");
	about_net_line(b, sizeof(b), "none", ABOUT_GATEWAY, ABOUT_OFFLINE);
	is(b, "No network  \xF9  Pi gateway  \xF9  offline", "no stack: No network");
	about_net_line(b, sizeof(b), "MiNTnet", "Serial port, 19200 baud", 0);
	is(b, "MiNTnet  \xF9  Serial port, 19200 baud", "no state: two items");

	about_mem_format(b, sizeof(b), 1, 12 * MB, 0, 98 * MB + MB / 5, 3 * MB + MB / 10);
	is(b, "TT RAM  \xF9  Free: 98.2 MB TT, 3.1 MB ST", "all in TT RAM");
	about_mem_format(b, sizeof(b), 0, 0, 2 * MB, 120 * MB, 13 * MB);
	is(b, "ST RAM  \xF9  Free: 120.0 MB TT, 13.0 MB ST", "a TT RAM machine, all in ST RAM");
	about_mem_format(b, sizeof(b), 1, 4 * MB, MB, 50 * MB, 2 * MB);
	is(b, "TT and ST RAM  \xF9  Free: 50.0 MB TT, 2.0 MB ST", "both");
	about_mem_format(b, sizeof(b), 0, 0, 512 * 1024, -1, 1200 * 1024);
	is(b, "ST RAM  \xF9  Free: 1.2 MB", "an ST: no TT figure");
	about_mem_format(b, sizeof(b), 0, 0, 512 * 1024, -1, 300 * 1024);
	is(b, "ST RAM  \xF9  Free: 300 KB", "under a megabyte: KB");
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
