/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * prt.c - PRT.PRG, printing through GDOS/NVDI's printer workstations (run.py): for each device asked for
 * (91, NVDI's picture files; 21, the default printer, PCL here), v_opnwk, its page size and pixel size,
 * text, a box and lines drawn, v_updwk, v_clswk; then the files the drivers wrote. Lines go to the
 * emulator's console as "PRT ...", the last "PRT done".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "print.h"

long nf_on = 1;		/* a test program: NatFeats always */

static char line[200];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

static void page(short h, short w, short ht, short n)
{
	short p[10], cw, ch;
	vst_height(h, (short)(ht / 60), &cw, &ch);
	vswr_mode(h, 1);
	vst_color(h, 1);
	snprintf(line, sizeof(line), "AERA print test: page %d", n);
	v_gtext_n(h, (short)(w / 10), (short)(ht / 10), line, (short)strlen(line));
	vsf_interior(h, 2);
	vsf_style(h, (short)(n + 2));
	vsf_color(h, 1);
	p[0] = (short)(w / 10), p[1] = (short)(ht / 5), p[2] = (short)(w / 10 + w * n / 8), p[3] = (short)(ht / 3);
	v_bar(h, p);
	vsl_color(h, 1);
	p[0] = (short)(w / 10), p[1] = (short)(ht / 2), p[2] = (short)(w * 9 / 10), p[3] = (short)(ht / 2);
	p[4] = (short)(w * 9 / 10), p[5] = (short)(ht * 9 / 10);
	v_pline(h, 3, p);
}

static void dir(const char *pat)
{
	long dta[11];
	void *was = Fgetdta();
	Fsetdta(dta);
	if (Fsfirst(pat, 0) == 0)
		do {
			snprintf(line, sizeof(line), "PRT file %s %s %ld", pat, (char *)dta + 30, dta[6]);
			say(line);
		} while (Fsnext() == 0);
	Fsetdta(was);
}

/* C:\\PRT.CFG: "DRIVER PAGES DEVICE" (DEVICE "-" leaves the printer's own): print.c's whole path, three pages
   by default, as an app prints */
static void job(void)
{
	PRINTJOB pj;
	short drv = 91, pages = 3, n, r;
	char dev[100] = "C:\\PAGE.IMG", cfg[160];
	long fh, k;
	fh = Fopen("C:\\PRT.CFG", 0);
	if (fh >= 0) {
		char *q = cfg, *e;
		k = Fread((short)fh, sizeof(cfg) - 1, cfg);
		Fclose((short)fh);
		cfg[k > 0 ? k : 0] = 0;
		drv = (short)strtol(q, &e, 10), q = e;
		pages = (short)strtol(q, &e, 10), q = e;
		while (*q == ' ')
			q++;
		for (k = 0; q[k] > ' ' && k < 99; k++)
			dev[k] = q[k];
		dev[k] = 0;
	}
	r = print_setup(&pj, pages);
	snprintf(line, sizeof(line), "PRT setup %d pages %d-%d", r, pj.first, pj.last);
	say(line);
	if (!r)
		return;
	print_device(&pj, drv, strcmp(dev, "-") ? dev : 0);
	r = print_open(&pj);
	snprintf(line, sizeof(line), "PRT open %d handle %d page %dx%d pixel %dx%d um planes %d", r, pj.h, pj.w, pj.ht,
		 pj.um_w, pj.um_h, pj.planes);
	say(line);
	if (r)
		for (n = pj.first; n <= pj.last; n++) {
			page(pj.h, pj.w, pj.ht, n);
			print_page(&pj, n == pj.last);
			snprintf(line, sizeof(line), "PRT page %d", n);
			say(line);
		}
	print_end(&pj);
	say("PRT ended");
}

int main(void)
{
	appl_init();
	job();
	dir("C:\\*.*");
	say("PRT done");
	appl_exit();
	return 0;
}
