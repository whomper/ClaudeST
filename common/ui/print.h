/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * Printing through the GDOS (NVDI 5): the family's one way (common/ui/print.c).
 *
 *	PRINTJOB pj;
 *	if (print_setup(&pj, pages) <= 0) ...	(no GDOS: say so)
 *	if (print_dialog(&pj, "Report.pdf") != 1) { print_end(&pj); return; }	(WDIALOG's dialog; 1 = Print)
 *	if (!print_open(&pj)) ...		(the printer couldn't be opened: say so)
 *	for (n = pj.first; n <= pj.last; n++) {
 *		draw page n on pj.h, pj.w x pj.ht pixels of pj.um_w x pj.um_h micrometres;
 *		print_page(&pj, n == pj.last);	(the page out; the next one started)
 *		(the app's progress, Esc or Stop between pages: print_end at once)
 *	}
 *	print_end(&pj);				(the job closed and everything given back)
 *
 * With WDIALOG (MagiC, or WDIALOG.PRG in AUTO under TOS) the user chooses the printer, the pages, the copies
 * and the paper in its dialog and the printer is opened with those settings (v_opnprn); without it the
 * GDOS's default printer (ASSIGN.SYS's 21, NVDI.INF's DEFAULT_PRINTER) is opened as it is set up, all pages.
 * Every page is drawn with the VDI on pj.h as on the screen; the driver makes the printer's own language.
 */
#ifndef PRINT_H
#define PRINT_H

typedef struct {
	short h;		/* the printer's workstation, 0 while closed */
	short w, ht;		/* the page's printable area in pixels */
	short um_w, um_h;	/* one pixel's width and height in micrometres */
	short planes;		/* 1 black and white, more for colour */
	short first, last;	/* the pages to print, from 1 */
	short copies;		/* how many times the app prints them (1 unless WDIALOG says) */
	short pages;		/* the document's pages, given to print_setup */
	void *dlg, *set;	/* WDIALOG's dialog and settings, 0 without it */
} PRINTJOB;

int print_setup(PRINTJOB *pj, short pages);	/* 1 with WDIALOG, 2 the default printer only, 0 no GDOS */
int print_dialog(PRINTJOB *pj, const char *name);	/* 1 print, 0 cancelled; 1 at once without WDIALOG */
int print_open(PRINTJOB *pj);			/* 1 open, 0 the printer couldn't be opened */
void print_page(PRINTJOB *pj, int last);	/* the page drawn goes to the printer */
void print_end(PRINTJOB *pj);			/* the job closed (what was printed goes out) */
/* for the rigs and the app's own choice of output: the driver and the device (a file name) to use */
void print_device(PRINTJOB *pj, short driver, const char *device);

#endif
