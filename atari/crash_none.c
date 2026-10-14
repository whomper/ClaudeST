/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * crash_none.c - crash.h's calls, doing nothing, for an edition that is
 * only ever a desk accessory (ECal's and EContact's ST editions): an
 * accessory never installs the crash catcher, so it needn't carry it.
 */
#include "crash.h"

void (*crash_cleanup)(const char *what);
void (*crash_report)(const char *text, long n);

void crash_install(const char *workdir, const char *app, const char *version)
{
	(void)workdir;
	(void)app;
	(void)version;
}

void crash_remove(void)
{
}

void crash_note(const char *what)
{
	(void)what;
}

const char *crash_previous(void)
{
	return 0;
}

const char *crash_log(void)
{
	return "";
}

int crash_test(short scan)
{
	(void)scan;
	return 0;
}
