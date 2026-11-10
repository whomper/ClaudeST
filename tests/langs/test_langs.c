/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_langs.c - ui/langs.c's parts with no GEM: a language's drawn name, a row, the total and the
 * refusal, the tick rules, the order, the scroll arithmetic, the message's lines and the job
 * (its words, its polling against a scripted network: when it asks, how long it waits, when it
 * gives the Gateway up).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "conn.h"
#include "charset.h"
#include "translate.h"
#include "langs.h"
#include "progbar.h"

static int fails;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* ---- the stand-in platform (as tests/translate's) ---- */
static unsigned long now;
static char sent[4000];
static long sentlen;
static const char *answers[40];
static long answer_len[40];
static int nconn, cur;
static long fed;
static int refuse_open;
static long timeout_seen[40];

int pf_open(const char *path, int mode) { return 7; }
long pf_write(int h, const void *buf, long n) { return n; }
void pf_close(int h) { }
long pf_size(const char *path) { return -1; }
int pf_remove(const char *path) { return 0; }
int pf_rename(const char *from, const char *to) { return 0; }
long pf_seek(int h, long off) { return off; }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return now; }
static void idle(void) { }
void (*pf_idle)(void) = idle;

int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	if (refuse_open) {
		snprintf(err, errlen, "connection refused");
		return -1;
	}
	timeout_seen[nconn < 40 ? nconn : 39] = translate_timeout_ms;
	cur = nconn++;
	fed = 0;
	sentlen = 0;
	return 5;
}
long net_write(int h, const void *buf, long n)
{
	if (sentlen + n < (long)sizeof(sent)) {
		memcpy(sent + sentlen, buf, n);
		sentlen += n;
	}
	return n;
}
long net_read(int h, void *buf, long max)
{
	long n = (cur < 40 && answers[cur] ? answer_len[cur] : 0) - fed;
	now += 1;
	if (n <= 0)
		return -1;
	if (n > max)
		n = max;
	memcpy(buf, answers[cur] + fed, n);
	fed += n;
	return n;
}
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

static char abuf[40][512];
static void script(int i, const char *body)
{
	snprintf(abuf[i], sizeof(abuf[i]), "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s",
		 (int)strlen(body), body);
	answers[i] = abuf[i];
	answer_len[i] = (long)strlen(abuf[i]);
}
static void reset(void)
{
	nconn = 0;
	memset(answers, 0, sizeof(answers));
	refuse_open = 0;
	conn_stop = 0;
}

/* the list of translate's own test: he installed, 600 MB of room */
static const char LIST[] =
	"cap_mb 600\nused_mb 229\n"
	"fr\tFrench\t125\tavailable\n"
	"he\t\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\t229\tinstalled\n"
	"de\tGerman\t286\tavailable\n"
	"es\tSpanish\t354\tavailable\n"
	"en\tEnglish\t0\tinstalled\n";

static void names_of(const TR_LANGS *L, langs_nm *names)
{
	int i;
	for (i = 0; i < L->n; i++)
		langs_name(&L->l[i], names[i]);
}

int main(void)
{
	static TR_LANGS L;
	static langs_nm names[TR_LANGS_MAX];
	short ord[TR_LANGS_MAX];
	char out[200], l1[64], l2[64];
	TR_LANGINFO info;
	int i;

	/* ---- a language's name as drawn ---- */
	memset(&info, 0, sizeof(info));
	strcpy(info.code, "de");
	strcpy(info.name, "German");
	langs_name(&info, out);
	check(!strcmp(out, "German"), "name: ASCII as it is");
	strcpy(info.name, "Espa\xC3\xB1ol");
	langs_name(&info, out);
	check(!strcmp(out, "Espa\xA4ol"), "name: n with a tilde in the Atari set (0xA4)");
	strcpy(info.code, "he");
	strcpy(info.name, "\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA");	/* Hebrew: ayin bet resh yod tav */
	langs_name(&info, out);
	check(strlen(out) == 5, "name: Hebrew has five letters");
	{
		/* logical order ayin bet resh yod tav is 0xD2 0xC3 0xD8 0xCB 0xDA?: the letters are in the
		   Atari set at 0xC2-0xDC; drawn left to right they come reversed (bidi) */
		char log[8];
		long n;
		char *a = cs_to_atari(info.name, (long)strlen(info.name), CS_UTF8, &n);
		for (i = 0; i < 5; i++)
			log[i] = a[i];
		check(n == 5 && (unsigned char)log[0] >= 0xC2 && (unsigned char)log[0] <= 0xDC, "name: the Atari letters are 0xC2-0xDC");
		check(out[0] == log[4] && out[4] == log[0] && out[2] == log[2], "name: Hebrew in display order (reversed), for TX_LTR");
		free(a);
	}
	strcpy(info.code, "ru");
	strcpy(info.name, "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");	/* Russian */
	langs_name(&info, out);
	check(!strcmp(out, "ru"), "name: Cyrillic can't be shown: the code, no '?'");
	strcpy(info.code, "ja");
	strcpy(info.name, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
	langs_name(&info, out);
	check(!strcmp(out, "ja"), "name: CJK: the code");
	strcpy(info.code, "xx");
	info.name[0] = 0;
	langs_name(&info, out);
	check(!strcmp(out, "xx"), "name: none given: the code");
	strcpy(info.name, "What? Really");
	langs_name(&info, out);
	check(!strcmp(out, "What? Really"), "name: a '?' of the name's own is kept");
	memset(info.name, 'x', 39);
	info.name[39] = 0;
	langs_name(&info, out);
	check(strlen(out) == LANGS_NAME - 1, "name: cut to what the buffer holds, terminated");

	/* ---- rows ---- */
	check(translate_langs_parse(&L, LIST, (long)sizeof(LIST) - 1) && L.n == 5, "the sample list parsed");
	names_of(&L, names);
	{
		char row[104];
		int k;
		check(langs_row(&L.l[1], names[1], 40, row) == 40, "row: as long as the columns asked for");
		check(row[0] == '[' && row[1] == LANGS_TICK && row[2] == ']' && row[3] == ' ' && !memcmp(row + 4, names[1], strlen(names[1])),
		      "row: [tick] and the name from column 4");
		check(!memcmp(row + 40 - 6, "229 MB", 6), "row: the size at the right edge");
		check(row[40 - 7] == ' ', "row: a blank before the size");
		langs_row(&L.l[0], names[0], 40, row);
		check(row[1] == ' ' && !memcmp(row + 4, "French", 6) && !memcmp(row + 34, "125 MB", 6), "row: an untouched language: empty box, 125 MB");
		L.l[0].want = 1;
		langs_row(&L.l[0], names[0], 40, row);
		check(row[1] == LANGS_TICK, "row: ticked by want");
		L.l[0].want = 0;
		langs_row(&L.l[4], names[4], 40, row);
		check(row[1] == LANGS_TICK && !memcmp(row + 34, "always", 6), "row: English is always ticked, and says always");
		L.l[4].want = 0;
		langs_row(&L.l[4], names[4], 40, row);
		check(row[1] == LANGS_TICK, "row: English ticked whatever want says");
		strcpy(L.l[2].name, "A very long name that does not fit the");
		langs_name(&L.l[2], names[2]);
		langs_row(&L.l[2], names[2], 24, row);
		check(row[24 - 1] == 'B' && !memcmp(row + 24 - 6, "286 MB", 6), "row: a long name is cut, the size kept");
		for (k = 4; k < 24 - 7 - 1; k++)
			if (row[k] == 0)
				break;
		check(k == 24 - 7 - 1, "row: no NUL inside, the name cut before the blank");
		langs_row(&L.l[3], "Spanish", 8, row);
		check(row[0] == '[' && row[2] == ']', "row: a narrow list still has its box");
		check(langs_row(&L.l[3], "Spanish", 0, row) == 0, "row: no columns, no row");
		strcpy(L.l[2].name, "German");
		langs_name(&L.l[2], names[2]);
	}

	/* ---- the total and the refusal ---- */
	langs_total_line(&L, out, sizeof(out));
	check(!strcmp(out, "Installed 229 MB of 600 MB"), "total line: Installed 229 MB of 600 MB");
	check(langs_toggle(&L, 2) == LT_TICKED, "tick: German fits");
	langs_total_line(&L, out, sizeof(out));
	check(!strcmp(out, "Installed 515 MB of 600 MB"), "total line: 515 after ticking German");
	check(langs_toggle(&L, 3) == LT_REFUSED && !L.l[3].want, "tick: Spanish refused, not ticked");
	langs_over_line(&L, 3, names[3], out, sizeof(out));
	check(!strcmp(out, "Spanish does not fit: it needs 269 MB more."), "refusal: the number that does not fit (269)");
	langs_over_line(&L, 3, "", out, sizeof(out));
	check(strstr(out, "es does not fit") != NULL, "refusal: the code when there is no name");
	check(langs_toggle(&L, 4) == LT_FIXED && langs_toggle(&L, 9) == LT_NOTHING && langs_toggle(&L, -1) == LT_NOTHING,
	      "tick: English is fixed, a row that isn't there does nothing");
	check(langs_toggle(&L, 1) == LT_UNTICKED && !L.l[1].want, "tick: Hebrew, installed, can be unticked");
	langs_total_line(&L, out, sizeof(out));
	check(!strcmp(out, "Installed 286 MB of 600 MB"), "total line: 286 after unticking Hebrew");
	check(langs_toggle(&L, 3) == LT_REFUSED, "tick: Spanish still doesn't fit with German (286 + 354 > 600)");
	check(langs_toggle(&L, 2) == LT_UNTICKED && langs_toggle(&L, 3) == LT_TICKED, "tick: German off, then Spanish fits");
	check(langs_toggle(&L, 1) == LT_TICKED, "tick: Hebrew back (354 + 229 = 583 of 600)");
	check(translate_langs_total(&L) == 583 && translate_langs_changed(&L), "tick: the total is the client's (583), changed");
	check(langs_toggle(&L, 2) == LT_REFUSED, "tick: and German no longer fits (583 + 286)");
	{
		char codes[64];
		translate_langs_codes(&L, codes, sizeof(codes));
		check(!strcmp(codes, "he,es"), "tick: the set to send is he,es (English left out)");
	}

	/* ---- the order ---- */
	translate_langs_parse(&L, LIST, (long)sizeof(LIST) - 1);
	names_of(&L, names);
	langs_sort(&L, (const langs_nm *)names, ord);
	/* English, French, German, Hebrew (the Hebrew name in letters sorts after Latin ones), Spanish */
	check(ord[0] == 4 && ord[1] == 0 && ord[2] == 2 && ord[3] == 3 && ord[4] == 1,
	      "order: English first, then by name (French, German, Spanish, then Hebrew's Atari letters)");
	{
		static TR_LANGS M;
		static const char many[] =
			"cap_mb 100\nused_mb 0\nnb\tNorwegian\t5\tavailable\nen\tEnglish\t0\tinstalled\nxx\tbeta\t5\tavailable\n"
			"yy\tAlpha\t5\tavailable\nzz\talpha\t5\tavailable\nca\tCatalan\t5\tavailable\n";
		translate_langs_parse(&M, many, (long)sizeof(many) - 1);
		names_of(&M, names);
		langs_sort(&M, (const langs_nm *)names, ord);
		check(!strcmp(M.l[ord[0]].code, "en") && !strcmp(M.l[ord[1]].code, "yy") && !strcmp(M.l[ord[2]].code, "zz") &&
		      !strcmp(M.l[ord[3]].code, "xx") && !strcmp(M.l[ord[4]].code, "ca") && !strcmp(M.l[ord[5]].code, "nb"),
		      "order: case-insensitive, equal names by code (Alpha, alpha, beta, Catalan, Norwegian)");
	}
	{
		short o0[1];
		static TR_LANGS Z;
		memset(&Z, 0, sizeof(Z));
		langs_sort(&Z, (const langs_nm *)names, o0);
		check(1, "order: no languages, no crash");
	}

	/* ---- scrolling ---- */
	check(langs_clamp_top(5, 9, 50) == 5 && langs_clamp_top(60, 9, 50) == 41 && langs_clamp_top(-3, 9, 50) == 0 &&
	      langs_clamp_top(4, 9, 5) == 0 && langs_clamp_top(1, 9, 0) == 0, "scroll: top kept within 0 .. n - rows");
	check(langs_show(10, 12, 9, 50) == 10 && langs_show(10, 9, 9, 50) == 9 && langs_show(10, 19, 9, 50) == 11 &&
	      langs_show(10, 18, 9, 50) == 10 && langs_show(0, 49, 9, 50) == 41 && langs_show(10, 3, 9, 50) == 3,
	      "scroll: the nearest top that has the cursor in sight");
	check(langs_move(0, 50, 9, 0x50, 0) == 1 && langs_move(0, 50, 9, 0x48, 0) == 0 && langs_move(49, 50, 9, 0x50, 0) == 49 &&
	      langs_move(5, 50, 9, 0x48, 0) == 4, "move: Down, Up, clamped at both ends");
	check(langs_move(5, 50, 9, 0x50, 1) == 13 && langs_move(5, 50, 9, 0x48, 1) == 0 && langs_move(45, 50, 9, 0x50, 1) == 49,
	      "move: Shift is a page (rows - 1)");
	check(langs_move(20, 50, 9, 0x47, 0) == 0 && langs_move(20, 50, 9, 0x47, 1) == 49, "move: Home the first, Shift+Home the last");
	check(langs_move(-1, 50, 9, 0x50, 0) == 1 && langs_move(-1, 0, 9, 0x50, 0) == -1 && langs_move(3, 2, 9, 0x50, 0) == 1,
	      "move: from no cursor, with no rows, from past the end");
	check(langs_move(5, 50, 1, 0x50, 1) == 6, "move: a page of a one-row list is a row");
	names_of(&L, names);
	langs_sort(&L, (const langs_nm *)names, ord);
	check(langs_find_letter(ord, (const langs_nm *)names, 5, 0, 'g') == 2 && langs_find_letter(ord, (const langs_nm *)names, 5, 0, 'S') == 3 &&
	      langs_find_letter(ord, (const langs_nm *)names, 5, 3, 's') == 3 && langs_find_letter(ord, (const langs_nm *)names, 5, 0, 'q') == -1,
	      "letter: the next name that starts with it, in any case, -1 for none");
	check(langs_find_letter(ord, (const langs_nm *)names, 5, 4, 'e') == 0, "letter: wraps round the end");
	check(langs_find_letter(ord, (const langs_nm *)names, 5, -1, 'e') == 0, "letter: from no cursor, the first");
	check(langs_find_letter(ord, (const langs_nm *)names, 5, 2, 'g') == 2, "letter: only itself: itself (after a full turn)");

	/* ---- the message's lines ---- */
	check(langs_wrap("short", 44, l1, l2) == 1 && !strcmp(l1, "short") && !l2[0], "wrap: one line");
	check(langs_wrap("Portuguese (Brazil) does not fit: it needs 269 MB more.", 44, l1, l2) == 2 &&
	      !strcmp(l1, "Portuguese (Brazil) does not fit: it needs") && !strcmp(l2, "269 MB more."), "wrap: at the last blank that fits");
	check(langs_wrap("0123456789012345678901234567890123456789012345678901234567890123456789", 44, l1, l2) == 2 &&
	      strlen(l1) == 44 && strlen(l2) == 26, "wrap: no blank: at the width");
	check(langs_wrap("aaaa   bbbb cccc", 6, l1, l2) == 2 && !strcmp(l1, "aaaa") && !strcmp(l2, "bbbb c"), "wrap: blanks at the break dropped, the rest cut to the width");
	check(langs_wrap("", 44, l1, l2) == 1 && !l1[0], "wrap: nothing");

	/* ---- sizes ---- */
	check(langs_mb(0) == 0 && langs_mb(-5) == 0 && langs_mb(1048576) == 1 && langs_mb(524288) == 1 && langs_mb(524287) == 0 &&
	      langs_mb(126L * 1048576L) == 126 && langs_mb(1900000000L) == 1812, "mb: rounded");

	/* ---- the job ---- */
	{
		static langs_job J;
		TR_JOB t;
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 1000);
		check(J.state == LJ_RUNNING && !strcmp(J.text, LANGS_W_WAIT) && J.next_ms == 1000 && !langs_job_permille(&J),
		      "job: begins running, 'getting ready', bar empty");
		langs_job_begin(&J, "PI_IP", 8480, 7, 0, 1000);
		check(!strcmp(J.text, LANGS_W_REMOVING), "job: removal only: 'removing'");
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 1000);

		translate_job_parse(&t, "job 7\nrunning\nbytes 0 of 0\n", 27);
		check(!langs_job_apply(&J, &t) && J.state == LJ_RUNNING && !strcmp(J.text, LANGS_W_WAIT),
		      "job: running with no figure yet: nothing new");
		translate_job_parse(&t, "job 7\nrunning\nbytes 125829120 of 304087040\n", 43);
		check(langs_job_apply(&J, &t) && !strcmp(J.text, "Installing languages: 120 of 290 MB"), "job: 'Installing languages: 120 of 290 MB'");
		check(langs_job_permille(&J) == 423, "job: the bar 423/1024 (120 of 290)");
		check(!langs_job_apply(&J, &t), "job: the same answer: nothing new");
		translate_job_parse(&t, "job 7\nrunning\nbytes 125829121 of 304087040\n", 43);
		langs_job_apply(&J, &t);
		check(!strcmp(J.text, "Installing languages: 120 of 290 MB"), "job: a byte more: the same words");
		translate_job_parse(&t, "job 7\ndone\n", 11);
		check(langs_job_apply(&J, &t) && J.state == LJ_DONE && !strcmp(J.text, "Languages installed") && langs_job_permille(&J) == PB_FULL,
		      "job: done: 'Languages installed', bar full");
		langs_job_begin(&J, "PI_IP", 8480, 7, 0, 0);
		translate_job_parse(&t, "job 7\ndone\n", 11);
		langs_job_apply(&J, &t);
		check(!strcmp(J.text, "Languages removed"), "job: removal done: 'Languages removed'");
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		translate_job_parse(&t, "job 7\nfailed: no room left on the Gateway\n", 41);
		check(langs_job_apply(&J, &t) && J.state == LJ_FAILED && !strcmp(J.text, "The Gateway could not install: no room left on the Gateway"),
		      "job: failed: 'The Gateway could not install: why'");
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		translate_job_parse(&t, "job 7\nfailed\n", 13);
		langs_job_apply(&J, &t);
		check(J.state == LJ_FAILED && strstr(J.text, "no reason given"), "job: failed with no reason: said so");
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		translate_job_parse(&t, "job 7\nnone\n", 11);
		check(langs_job_apply(&J, &t) && J.state == LJ_LOST && !strcmp(J.text, LANGS_W_UNKNOWN), "job: 'none': the Gateway has forgotten it");
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		translate_job_parse(&t, "job 8\nrunning\nbytes 1 of 2\n", 27);
		check(langs_job_apply(&J, &t) && J.state == LJ_DONE, "job: a later job than ours: ours ended");
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		translate_job_parse(&t, "job 6\ndone\n", 11);
		check(!langs_job_apply(&J, &t) && J.state == LJ_RUNNING, "job: an older job's record says nothing about ours");
		/* big figures: a long can't overflow */
		langs_job_begin(&J, "PI_IP", 8480, 7, 100, 0);
		J.done = 1900000000L;
		J.total = 2000000000L;
		check(langs_job_permille(&J) == 972, "job: bar for 1.9 of 2.0 GB: 972, no overflow");
		J.done = 2100000000L;
		check(langs_job_permille(&J) == PB_FULL, "job: done past total: the bar is full, no more");

		/* ---- the poll ---- */
		reset();
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 10000);
		check(langs_job_step(&J, 9999) == 0 && nconn == 0, "poll: not due: no connection");
		script(0, "job 7\nrunning\nbytes 125829120 of 304087040\n");
		translate_timeout_ms = 60000L;
		check(langs_job_step(&J, 10000) == 1 && nconn == 1 && !strncmp(sent, "GET /job HTTP/1.0\r\n", 19) &&
		      !strcmp(J.text, "Installing languages: 120 of 290 MB"), "poll: due: GET /job, the words");
		check(timeout_seen[0] == LANGS_POLL_TIMEOUT_MS && translate_timeout_ms == 60000L,
		      "poll: waits only a few seconds for the Gateway, and puts the usual back");
		check(J.next_ms == 12000, "poll: the next in 2 s");
		check(langs_job_step(&J, 11999) == 0 && nconn == 1, "poll: 1999 ms later: no connection");
		script(1, "job 7\nrunning\nbytes 125829120 of 304087040\n");
		check(langs_job_step(&J, 12000) == 0 && nconn == 2, "poll: 2 s later: asked, nothing new: 0");
		script(2, "job 7\nrunning\nbytes 200000000 of 304087040\n");
		check(langs_job_step(&J, 14000) == 1 && !strcmp(J.text, "Installing languages: 191 of 290 MB"), "poll: news: 191 of 290 MB");
		script(3, "job 7\ndone\n");
		check(langs_job_step(&J, 16000) == 2 && J.state == LJ_DONE && !strcmp(J.text, "Languages installed"), "poll: done: 2, the job over");
		check(langs_job_step(&J, 99999) == 0 && nconn == 4, "poll: a finished job is not asked about again");

		/* the Gateway not answering: it restarts when the job ends, so wait a long while */
		reset();
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 0);
		refuse_open = 1;
		check(langs_job_step(&J, 0) == 0 && J.state == LJ_RUNNING && J.next_ms == LANGS_POLL_SLOW_MS, "poll: no answer: still running, slower polls");
		check(langs_job_step(&J, 5000) == 0 && langs_job_step(&J, 40000) == 0 && J.state == LJ_RUNNING, "poll: 40 s without an answer: still waiting");
		check(langs_job_step(&J, 89999) == 0 && J.state == LJ_RUNNING, "poll: 90 s less a ms: still waiting");
		check(langs_job_step(&J, 95000) == 2 && J.state == LJ_LOST && !strcmp(J.text, LANGS_W_LOST), "poll: 95 s: the Gateway is gone: 2, 'is it on?'");
		/* an answer in between starts the count again */
		reset();
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 0);
		refuse_open = 1;
		langs_job_step(&J, 0);
		langs_job_step(&J, 60000);
		refuse_open = 0;
		script(0, "job 7\nrunning\nbytes 1 of 100\n");
		check(langs_job_step(&J, 70000) >= 0 && J.lost_since == 0, "poll: an answer after a silence: the count is cleared");
		refuse_open = 1;
		check(langs_job_step(&J, 100000) == 0 && langs_job_step(&J, 150000) == 0 && J.state == LJ_RUNNING,
		      "poll: and counts from the new silence (150 s - 100 s < 90 s)");
		/* conn_stop left set by an Esc long ago doesn't kill the poll */
		reset();
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 0);
		conn_stop = 1;
		script(0, "job 7\nrunning\nbytes 1 of 100\n");
		check(langs_job_step(&J, 0) >= 0 && nconn == 1 && J.lost_since == 0 && conn_stop == 0, "poll: a stale stop is cleared first, and after");
		/* a reply that isn't the Gateway's */
		reset();
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 0);
		script(0, "hello\n");
		check(langs_job_step(&J, 0) == 0 && J.state == LJ_RUNNING && J.lost_since != 0, "poll: not the Gateway's words: counted as no answer");
		/* clock wrap: now before next_ms by wrap */
		langs_job_begin(&J, "PI_IP", 8480, 7, 290, 0xFFFFFF00UL);
		reset();
		check(langs_job_step(&J, 0xFFFFFE00UL) == 0 && nconn == 0, "poll: the clock's wrap: not due before");
		script(0, "job 7\nrunning\nbytes 1 of 100\n");
		check(langs_job_step(&J, 0x00000010UL) >= 0 && nconn == 1, "poll: ...and due after the wrap");
	}

	printf(fails ? "%d FAILED\n" : "all langs checks passed\n", fails);
	return fails != 0;
}
