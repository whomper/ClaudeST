/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aboutline.c - the About box's connection and memory lines (see
 * aboutline.h).
 */
#include <stdio.h>
#include <string.h>
#ifndef ABOUTLINE_HOST
#include <stdlib.h>
#include "tos.h"
#endif
#include "aboutline.h"

#define SEP "  \xF9  "			/* the family's bullet, two spaces each side */

void about_net_line(char *out, int n, const char *stack, const char *route, const char *state)
{
	snprintf(out, n, "%s" SEP "%s%s%s", !stack || !strcmp(stack, "none") ? "No network" : stack, route,
		 state ? SEP : "", state ? state : "");
}

/* bytes as "512 KB", "3.1 MB", "98.2 MB" (one decimal, no floating point) */
static void size(char *out, int n, long bytes)
{
	long tenths;
	if (bytes < 1024L * 1024) {
		snprintf(out, n, "%ld KB", bytes / 1024);
		return;
	}
	tenths = (bytes / 1024 * 10 + 512) / 1024;
	snprintf(out, n, "%ld.%ld MB", tenths / 10, tenths % 10);
}

void about_mem_format(char *out, int n, int prog_tt, long heap_tt, long heap_st, long free_tt, long free_st)
{
	char st[24], tt[24];
	int in_tt = prog_tt || heap_tt, in_st = !prog_tt || heap_st;
	size(st, sizeof(st), free_st);
	if (free_tt >= 0 || heap_tt) {
		size(tt, sizeof(tt), free_tt > 0 ? free_tt : 0);
		snprintf(out, n, "%s" SEP "Free: %s TT, %s ST",
			 in_tt && in_st ? "TT and ST RAM" : in_tt ? "TT RAM" : "ST RAM", tt, st);
	} else {
		snprintf(out, n, "ST RAM" SEP "Free: %s", st);
	}
}

#ifndef ABOUTLINE_HOST
void about_mem_line(char *out, int n)
{
	long st = (long)Mxalloc(-1L, 0), tt = (long)Mxalloc(-1L, 1);
	if (st == -32) {		/* TOS 1.x: no Mxalloc, no TT RAM */
		st = (long)Malloc(-1L);
		tt = -1;
	} else if (tt <= 0) {
		tt = -1;		/* none */
	}
	about_mem_format(out, n, (unsigned long)about_mem_line >= 0x01000000UL, heap_tt, heap_st, tt, st);
}
#endif
