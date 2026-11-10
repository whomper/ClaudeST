/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * translate.h - the client of the Gateway's translation service (translategate;
 * standards/TRANSLATION-SERVICE.md). Plain HTTP/1.0 on the home network, UTF-8
 * on the wire, one request and one answer a connection. An app touches this
 * only when its Translate setting is On: with it Off nothing here is called.
 *
 * The text is never written to the protocol log (conn_logfile): only the
 * language pair, the sizes and the answer's status.
 */
#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "util.h"

#define TR_PIECE 8192		/* the most the protocol lets a request carry (UTF-8 bytes) */
#define TR_PIECE_DEFAULT 2048	/* what the client sends at a time: a few paragraphs, seconds on the Gateway */
#define TR_LANG  8		/* a language code with its NUL: "en", "he", "pt-BR" */

/* how long a request waits for the Gateway, ms (60 s: a piece of text takes seconds). A quick
   question that must not hold the app up (the job's progress, langs_job_step) sets less
   for its own request and puts this back */
extern long translate_timeout_ms;

/* a turn between pieces: done and total in bytes of the Atari text; non-zero
   stops (Esc) */
typedef int (*translate_turn_fn)(long done, long total);

/* ---- the pure parts (host tests, no network) ---- */

/* 1 if code is a language code the protocol accepts ([a-z]{2,3}, then
   optionally - and 2-4 letters or digits), else 0. Nothing else is ever put
   in a request line. */
int  translate_code_ok(const char *code);
/* the length of the first piece of s[0..n): at most max bytes, ending after
   the last blank line (a paragraph) that fits, else after the last line
   end, else after the last space, else at max, never inside a UTF-8
   character. n > 0 gives at least 1. */
long translate_split(const char *s, long n, long max);
/* the request for a piece: the lines up to the empty one, then the body.
   from may be NULL or "" (the engine detects it). 0 if a code is not ok or
   there is no room. */
int  translate_request(SBUF *out, const char *host, const char *to, const char *from,
		       const char *body, long n);
/* "HTTP/1.0 200 OK" -> 200; 0 if the line isn't a status line */
int  translate_status_code(const char *line);
/* what to tell the user for a refusal: "the language pair isn't installed
   on the Gateway" ... (code 400, 413, 422, 503, 504, else the number) */
const char *translate_refusal(int code);

/* ---- the network (conn.c: SEC_PLAIN, a turn for the machine while it waits) ---- */

/* GET /status: info gets "translategate 1, engine NAME VERSION, ready" as
   one line. 1 when the service answers; 0 with err otherwise. */
int  translate_status(const char *host, unsigned short port, char *info, long infomax,
		      char *err, int errlen);
/* GET /languages: appends "CODE<TAB>NAME" lines to out. */
int  translate_languages(const char *host, unsigned short port, SBUF *out, char *err, int errlen);
/* one piece (n <= TR_PIECE UTF-8 bytes) appended to out; from_used (TR_LANG
   bytes) gets the language the engine used; *same (may be NULL) is 1 when the
   text is already in the target language (the Gateway sent no body: out then gets the
   piece as it was). 0 with err on a refusal, a timeout, a stop (conn_stop) or an
   answer that breaks the protocol (not UTF-8, more than 4 times the request, more
   paragraphs than it). */
int  translate_piece(const char *host, unsigned short port, const char *to, const char *from,
		     const char *utf8, long n, SBUF *out, char *from_used, int *same,
		     char *err, int errlen);

/* ---- the text in pieces, for an app that shows each as it comes ----
   The app owns the UTF-8 text, which must stay while the cursor is used. One
   step is one piece of at most t->piece bytes (TR_PIECE_DEFAULT; set it before the
   first step), one connection, appended to the app's SBUF. Between steps the app
   draws, and may stop: it just doesn't call again. Inside a step the machine gets
   its turns through pf_idle; to drop a translation at once (the user went to the
   next letter, Esc) the app sets conn_stop from there: the step returns -1 with
   err "stopped", the connection is closed, and the Gateway stops the job when it
   sees the close. */
typedef struct {
	char host[104];
	unsigned short port;
	char to[TR_LANG], from[TR_LANG];
	const char *text;
	long len, pos;		/* the text, and how much of it is done */
	long piece;
	char from_used[TR_LANG];	/* the language of the first piece */
	int same;		/* the last piece was already in the target language */
	char err[160];
} TRANSLATE;
void translate_begin(TRANSLATE *t, const char *host, unsigned short port, const char *to,
		     const char *from, const char *utf8, long n);
/* 1: a piece was appended to out; 0: the text is done; -1: failed (t->err), nothing appended */
int  translate_step(TRANSLATE *t, SBUF *out);

/* ---- the whole text ---- */

/* UTF-8 in, UTF-8 out (malloc'd, NUL-terminated, *outlen its length): the pieces
   in order, a turn (turn, may be NULL) before each, the line ends a piece ended
   with kept between the answers. NULL with err on the first failure (nothing
   partial is returned). The Gateway decides how the lines inside a paragraph are
   handled (standards/TRANSLATION-SERVICE.md): the answer's paragraphs are
   the request's; the app wraps the lines. */
char *translate_utf8(const char *host, unsigned short port, const char *to, const char *from,
		     const char *utf8, long n, translate_turn_fn turn, char *from_used,
		     long *outlen, char *err, int errlen);
/* the same for Atari text (logical-order Hebrew, 0xC2-0xDC) through common's charsets,
   in and out. EMail's letters are UTF-8 (the Atari set has lost Cyrillic, Greek and
   CJK already): it uses translate_utf8. */
char *translate_atari(const char *host, unsigned short port, const char *to, const char *from,
		      const char *atari, long n, translate_turn_fn turn, char *from_used,
		      long *outlen, char *err, int errlen);

/* ---- the Languages page (standards/TRANSLATION-SERVICE.md, "Managing the installed
   languages"): what the Gateway has, the set the user ticks, and the job that installs it ----

   The dialog (ui/langs.c) shows the list with each language's size in MB and the
   room, lets the user tick and untick, and sends ONE request when it closes with
   OK; Cancel sends nothing. English is always there (it is not ticked). The
   sizes, the room and what is installed are the Gateway's numbers. */
#define TR_LANGS_MAX 64		/* (AERA-Gateway's table has 50 rows; 48 left two out) */
#define TR_LANGNAME  40		/* the name in UTF-8, with its NUL */
typedef struct {
	char code[TR_LANG];
	char name[TR_LANGNAME];
	int mb;			/* download and disk size, both directions together */
	int installed;		/* the Gateway has it */
	int want;		/* ticked: starts as installed */
} TR_LANGINFO;
typedef struct {
	int cap_mb, used_mb;	/* the room for languages, and what the installed ones take */
	int n;
	TR_LANGINFO l[TR_LANGS_MAX];
} TR_LANGS;
/* the pure parts: "cap_mb N / used_mb N / CODE<TAB>NAME<TAB>MB<TAB>installed|available" lines.
   1 if it is the list (cap_mb found), else 0; bad rows are left out; want = installed. */
int  translate_langs_parse(TR_LANGS *out, const char *text, long n);
/* MB the ticked set takes (English is free), and whether ticking language i would still fit */
int  translate_langs_total(const TR_LANGS *l);
int  translate_langs_fits(const TR_LANGS *l, int i);
/* how many MB past the room ticking i would go (0 if it fits) */
int  translate_langs_over(const TR_LANGS *l, int i);
/* has the user changed anything (want differs from installed)? */
int  translate_langs_changed(const TR_LANGS *l);
/* the ticked set for POST /languages: "he,de" (English left out, the Gateway adds it); 0 if no room */
int  translate_langs_codes(const TR_LANGS *l, char *out, int max);
/* the network: GET /languages?all=1 */
int  translate_langs_get(const char *host, unsigned short port, TR_LANGS *out, char *err, int errlen);
/* POST /languages with codes (translate_langs_codes): *job gets the job number (0: nothing to do) */
int  translate_langs_set(const char *host, unsigned short port, const char *codes, int *job,
			 char *err, int errlen);
/* the job: "job N / running / bytes D of T", "done", "failed: why", "none" */
enum { TR_JOB_NONE, TR_JOB_RUNNING, TR_JOB_DONE, TR_JOB_FAILED };
typedef struct {
	int id, state;
	long done, total;	/* bytes, while running */
	char why[96];		/* a failed job's reason */
} TR_JOB;
int  translate_job_parse(TR_JOB *j, const char *text, long n);
int  translate_job_get(const char *host, unsigned short port, TR_JOB *j, char *err, int errlen);

#endif
