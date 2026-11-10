/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * langs.h - the family's Languages dialog (standards/TRANSLATION-SERVICE.md,
 * "Managing the installed languages from the Atari"; Erez, 7 and 8 Nov 2026):
 * which languages the Gateway's translation service has installed, and how
 * much memory they take. For the apps that translate (EMail, EWeb, EPDF),
 * opened from Settings by a "Languages..." button beside "Translate into".
 *
 * The user sees the languages the Gateway offers, each a tick box with its
 * size in MB and its name (English is always there, ticked, not to be
 * changed), a line "Installed 229 MB of 600 MB" (the Gateway's own numbers:
 * what the ticked set takes, and the room it has), a message line and Cancel and
 * OK (10 wide, OK at the right, under a black title band " Languages"; D22, D26,
 * C1 of 9 Nov 2026). 48 by 18 cells of the system font (the Font dialog's 17 and a line
 * for the message). Ticking a language the room can't take is refused, with
 * the number that doesn't fit; the ones the Gateway has stay ticked until the
 * user unticks them.
 *
 * NOTHING is sent while the user ticks and unticks, and Cancel, Esc and Undo
 * send nothing (the guidelines, 3.5). Only OK, if something changed, sends
 * ONE request (POST /languages: the ticked set, the installs and the removals
 * together), and the Gateway answers with a job number: the install is a
 * job there and takes minutes. The dialog closes at once; the app follows
 * the job in its own event loop, in its status line (below), and the user
 * can go on working.
 *
 *	static langs l;
 *	l.host = opt.gate_host;  l.port = 8480;
 *	l.wait = busy_bee;			(EMail; 0: the busy bee)
 *	r = langs_run(&l);			(from Settings > Languages...)
 *	if (r == 1 && l.job) {			(OK, and the Gateway has a job)
 *		langs_job_begin(&job, l.host, l.port, l.job, l.add_mb, tos_ms());
 *		...
 *	}
 *	in the event loop, with a timer (MU_TIMER, a second or so is plenty):
 *	if (job_on) {
 *		int s = langs_job_step(&job, tos_ms());	(polls GET /job at most every 2 s)
 *		if (s)				(1 news, 2 finished)
 *			status(job.text), bar(langs_job_permille(&job));
 *		if (s == 2)
 *			job_on = 0;		(job.state: LJ_DONE, LJ_FAILED or LJ_LOST;
 *						 then ask the Gateway for the languages again)
 *	}
 *
 * The list is asked of the Gateway when the dialog opens (GET
 * /languages?all=1), with the busy bee (or the app's wait) and Esc or Undo
 * to stop it. If the Gateway can't be had the user gets an alert and the
 * dialog doesn't open (-1); a stop is no failure (0).
 *
 * The dialog takes the system font (font_sys_push/pop), draws with the app's
 * gemui.h calls and turns every clip it set on vdi_h off before it returns
 * (2.2: NVDI clips raster copies with it). The list is the family's: a pane
 * bar of AES objects at its right (panebar.h, 5.1), a row picked on the press
 * and ticked on the release (5.8), Up and Down (Shift: a page), Home (Shift:
 * the last), a letter to the next language that starts with it, Space to
 * tick, Return OK, Esc and Undo Cancel. In a window under MiNT and MagiC
 * (dlg_open, 3.5); 3D where the AES has it (5.6). Only the row that changed
 * and the total line are drawn again (the guidelines' Speed).
 *
 * Links with ui/dialog.c, click.c, panebar.c, alert.c and the app's gemui.h
 * calls, and with common's src/translate.c and charset.c.
 * Built with -DLANGS_PURE_ONLY, only the parts with no GEM (the rows' text,
 * the total, the tick rules, the scroll arithmetic, the job's words):
 * tests/langs checks them on the build machine.
 */
#ifndef AERA_LANGS_H
#define AERA_LANGS_H

#include "translate.h"

/* ---------------- the words and the rules (no GEM) ---------------- */

#define LANGS_NAME 32		/* a language's name as drawn (Atari text), with its NUL */
#define LANGS_TICK 8		/* the system font's tick */

typedef char langs_nm[LANGS_NAME];

/* the name to draw (Atari text, logical order; out is LANGS_NAME long): the
   Gateway's UTF-8 name through common's charsets; a name the Atari set can't
   show (Cyrillic, CJK, Arabic: they would come out as '?') is shown by its
   code instead; Hebrew letters come in display order (bidi), for drawing
   left to right (TX_LTR) */
void langs_name(const TR_LANGINFO *i, char *out);

/* a row of cols columns, for drawing: "[x] Hebrew          229 MB", the tick
   box ticked as the user has it (want), the name cut to what is left, the
   size at the right edge ("always" for English). Exactly cols characters,
   padded with blanks; returns cols. name is langs_name's */
int langs_row(const TR_LANGINFO *i, const char *name, int cols, char *out);

/* "Installed 229 MB of 600 MB": the MB of the ticked set (translate_langs_total)
   and the Gateway's room (cap_mb) */
int langs_total_line(const TR_LANGS *l, char *out, int max);

/* the words for a refused tick: "German does not fit: it needs 269 MB more." */
int langs_over_line(const TR_LANGS *l, int i, const char *name, char *out, int max);

/* the tick rules: a click or Space on language i */
enum { LT_NOTHING, LT_TICKED, LT_UNTICKED, LT_REFUSED, LT_FIXED };
/* LT_TICKED / LT_UNTICKED: want changed. LT_REFUSED: it would go past the room (translate_langs_over
   MB), want unchanged. LT_FIXED: English, which is always there. LT_NOTHING: no such language */
int langs_toggle(TR_LANGS *l, int i);

/* the list order: English first, then the others by name (case-insensitive,
   by their drawn names), as indexes into l->l[]; ord gets l->n of them */
void langs_sort(const TR_LANGS *l, const langs_nm *names, short *ord);

/* the scrolling: the first row shown (top) of n rows, rows of them shown at once.
   clamp: top kept within 0 .. n - rows; show: the nearest top that has row cur in sight */
int langs_clamp_top(int top, int rows, int n);
int langs_show(int top, int cur, int rows, int n);
/* the cursor after a key (a scan code: 0x48 Up, 0x50 Down, 0x47 Home), Shift for a page / the last
   one; cur -1 (none) starts at the first. Always within 0 .. n - 1 (-1 when n is 0) */
int langs_move(int cur, int n, int rows, int scan, int shift);
/* the next row after cur (wrapping) whose name starts with the letter ch, in any case; -1 if none.
   ord and names as langs_sort's: the rows are ord[0 .. n - 1] */
int langs_find_letter(const short *ord, const langs_nm *names, int n, int cur, int ch);

/* s on up to two lines of cols columns, broken at the last blank that fits (the message
   line): l1 and l2 are cols + 1 long. Returns the lines used (1 or 2; the rest of a text over
   two lines is cut) */
int langs_wrap(const char *s, int cols, char *l1, char *l2);

/* MB for a size in bytes, rounded: the job's "120 of 290 MB" */
long langs_mb(long bytes);

/* ---------------- the job the Gateway runs after OK ---------------- */

/* the words (Erez decides them later) */
#define LANGS_W_WAIT      "The Gateway is getting ready to install languages..."
#define LANGS_W_RUN       "Installing languages: %ld of %ld MB"
#define LANGS_W_REMOVING  "The Gateway is removing languages..."
#define LANGS_W_DONE      "Languages installed"
#define LANGS_W_REMOVED   "Languages removed"
#define LANGS_W_FAIL      "The Gateway could not install: %s"
#define LANGS_W_FAIL_RM   "The Gateway could not remove: %s"
#define LANGS_W_LOST      "The Gateway doesn't answer: is it on?"
#define LANGS_W_UNKNOWN   "The Gateway has no record of the install: see Languages"

enum { LJ_RUNNING, LJ_DONE, LJ_FAILED, LJ_LOST };	/* langs_job.state */
#define LANGS_POLL_MS 2000L		/* GET /job no oftener than this */
#define LANGS_POLL_SLOW_MS 5000L	/* after a poll that got no answer */
#define LANGS_LOST_MS 90000L		/* no answer for this long: the Gateway is gone (it also restarts
					   itself when the job ends, to load the models: seconds) */
#define LANGS_POLL_TIMEOUT_MS 8000L	/* a poll's own wait (translate_timeout_ms) */

typedef struct {
	char host[104];
	unsigned short port;
	int id;			/* the Gateway's job number */
	int adding_mb;		/* MB the job downloads (0: it only removes): for the words */
	int state;		/* LJ_* */
	long done, total;	/* bytes, as the Gateway says; total 0 until it knows */
	unsigned long next_ms;	/* the next poll is due */
	unsigned long lost_since;	/* the first poll in a row with no answer, 0 when the last had one */
	char text[160];		/* the status line's words now */
} langs_job;

/* follow job id, which langs_run gave (langs.job) and langs.add_mb; now_ms the app's clock */
void langs_job_begin(langs_job *j, const char *host, unsigned short port, int id, int adding_mb,
		     unsigned long now_ms);
/* what the Gateway said (translate_job_parse's answer) put into j: its state, figures and words.
   1 when the text or the figures changed. Pure; langs_job_step calls it */
int langs_job_apply(langs_job *j, const TR_JOB *t);
/* the poll: GET /job when it is due (not before LANGS_POLL_MS since the last), at most a few
   seconds' wait; 0: nothing new (not due, or no answer yet), 1: j->text / j->done changed,
   2: finished (state LJ_DONE, LJ_FAILED or, no answer for LANGS_LOST_MS, LJ_LOST). The wait for the Gateway
   goes through pf_idle, so the app's Esc handling applies */
int langs_job_step(langs_job *j, unsigned long now_ms);
/* how much of the progress bar is full, 0 to PB_FULL (progbar.h: 1024); the whole bar when finished,
   0 while the Gateway has not said how much there is */
short langs_job_permille(const langs_job *j);

#ifndef LANGS_PURE_ONLY
#include "gem.h"
#include "gemui.h"

typedef struct langs langs;
struct langs {
	/* the app's, before langs_run */
	const char *host;	/* the Gateway's address (Settings' Secure page) */
	unsigned short port;	/* translategate's: 8480 */
	/* the wait for the Gateway (the list is asked, an OK is sent): on 1 before, on 0 after. The
	   app's own indication, EMail's busy_bee (its pf_idle takes Esc); 0: the busy bee, and Esc
	   and Undo stop it through langs.c's own pf_idle when the app has none */
	void (*wait)(int on);
	/* a message while the dialog is up (EMail: EContact's answers can't wait); with 0 every
	   250 ms. 0: none taken */
	void (*message)(short *msg);
	void *user;		/* the app's */

	/* langs_run's answer */
	TR_LANGS list;		/* what the Gateway had, and the ticks as the user left them (want) */
	int job;		/* OK and something changed: the Gateway's job (follow it with langs_job_*);
				   0: nothing to do (nothing changed, or the Gateway had it already) */
	int add_mb;		/* the MB that job downloads */
	char err[160];		/* why -1 (the user has been told: an alert) */
};

/* the dialog: 1 OK (job and add_mb filled in), 0 Cancel, Esc, Undo, or a stop while the list was
   asked for, -1 the Gateway's list couldn't be had (err; an alert was shown). If the request
   of OK is refused the dialog stays open and says why in an alert (the user may try again,
   change the ticks or cancel) */
int langs_run(langs *l);
#endif

#endif
