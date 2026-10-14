/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * mkroots.c - decodes CACERT.PEM into ROOTS.DAT on this computer, the
 * same file the program writes the first time it connects in Falcon mode. It
 * ships next to CACERT.PEM so the Falcon never has to do the slow part
 * (well over a minute on a 16 MHz machine).
 *
 *   mkroots CACERT.PEM ROOTS.DAT
 */
#include <stdio.h>
#include <string.h>
#include "tls.h"
#include "plat.h"

int main(int argc, char **argv)
{
	char err[200];
	int n;
	if (argc != 3) {
		fprintf(stderr, "usage: mkroots CACERT.PEM ROOTS.DAT\n");
		return 2;
	}
	snprintf(tls_roots_cache, sizeof(tls_roots_cache), "%s", argv[2]);
	remove(argv[2]);
	n = tls_load_anchors(argv[1], err, sizeof(err));
	if (!n) {
		fprintf(stderr, "mkroots: %s\n", err);
		return 1;
	}
	if (pf_size(argv[2]) <= 0) {
		fprintf(stderr, "mkroots: could not write %s\n", argv[2]);
		return 1;
	}
	printf("%s: %d root certificates\n", argv[2], n);
	return 0;
}
