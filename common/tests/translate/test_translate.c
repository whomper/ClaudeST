/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_translate.c - common/src/translate.c against a scripted network: the codes, the split,
 * the request, every answer the service may give (and the ones it must not), the whole text in
 * pieces with Hebrew, and that no word of the text reaches the protocol log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "conn.h"
#include "charset.h"
#include "translate.h"

static int fails;
static int ph[16], nph;
static long phb[16];
static void on_phase(int p, long b)
{
	if (nph < 16) {
		ph[nph] = p;
		phb[nph++] = b;
	}
}

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* ---- the stand-in platform ---- */
static unsigned long now;
static char logbuf[200000];
static long loglen;
static char sent[40000];	/* what the client wrote to the current connection */
static long sentlen;
static const char *answers[40];	/* the canned answer of each connection, in order */
static long answer_len[40];
static int nconn, cur;
static long fed;
static int refuse_open;

long pf_read(int h, void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
int pf_open(const char *path, int mode) { return 7; }
long pf_write(int h, const void *buf, long n)
{
	if (loglen + n < (long)sizeof(logbuf)) {
		memcpy(logbuf + loglen, buf, n);
		loglen += n;
	}
	return n;
}
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
	long n = answer_len[cur] - fed;
	now += 1;
	if (n <= 0)
		return -1;		/* the other side has closed */
	if (n > 61)
		n = 61;
	if (n > max)
		n = max;
	memcpy(buf, answers[cur] + fed, n);
	fed += n;
	return n;
}
void net_close(int h) { }
void net_close_wait(int h, int seconds) { }

/* one canned answer for the next connection(s) */
static void script(int i, const char *a, long n)
{
	answers[i] = a;
	answer_len[i] = n < 0 ? (long)strlen(a) : n;
}
static void reset(void)
{
	nconn = 0;
	loglen = 0;
	memset(answers, 0, sizeof(answers));
	refuse_open = 0;
}

/* an answer with a Content-Length */
static char abuf[40][2048];
static const char *ok200(int i, const char *body, const char *from)
{
	snprintf(abuf[i], sizeof(abuf[i]), "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
		 "X-Translate-From: %s\r\nContent-Length: %d\r\n\r\n%s", from, (int)strlen(body), body);
	return abuf[i];
}

/* ---- translate_langs_parse against the first way it was written (a line copied, strchr for the
   tabs), kept here as the reference: the new one reads the lines in place (the guidelines' Speed) and
   must keep every list and every mangled list as it was ---- */
static int t_utf8_ok(const unsigned char *s, long n)
{
	long i = 0;
	while (i < n) {
		unsigned char c = s[i];
		int k;
		unsigned long u;
		if (c < 0x80) {
			i++;
			continue;
		}
		if (c >= 0xC2 && c <= 0xDF) {
			k = 1;
			u = c & 0x1F;
		} else if (c >= 0xE0 && c <= 0xEF) {
			k = 2;
			u = c & 0x0F;
		} else if (c >= 0xF0 && c <= 0xF4) {
			k = 3;
			u = c & 0x07;
		} else
			return 0;
		if (i + k >= n)
			return 0;
		{
			int j;
			for (j = 1; j <= k; j++) {
				if ((s[i + j] & 0xC0) != 0x80)
					return 0;
				u = (u << 6) | (s[i + j] & 0x3F);
			}
		}
		if ((k == 2 && u < 0x800) || (k == 3 && u < 0x10000) || u > 0x10FFFF ||
		    (u >= 0xD800 && u <= 0xDFFF))
			return 0;
		i += k + 1;
	}
	return 1;
}

static int old_next_line(const char *s, long n, long *at, char *buf, int max)
{
	long i = *at, k = 0;
	if (i >= n)
		return 0;
	while (i < n && s[i] != '\n') {
		if (s[i] != '\r' && k < max - 1)
			buf[k++] = s[i];
		i++;
	}
	buf[k] = 0;
	*at = i < n ? i + 1 : n;
	return 1;
}

static int old_digits_only(const char *p, int *out)
{
	long v = 0;
	int any = 0;
	while (*p >= '0' && *p <= '9' && v < 1000000L) {
		v = v * 10 + (*p++ - '0');
		any = 1;
	}
	if (!any || *p)
		return 0;
	*out = (int)v;
	return 1;
}

/* digits at p, then whatever follows (a job's number, then its line end) */
int old_langs_parse(TR_LANGS *out, const char *text, long n)
{
	char line[160];
	long at = 0;
	int have_cap = 0;
	memset(out, 0, sizeof(*out));
	while (old_next_line(text, n, &at, line, sizeof(line))) {
		char *t1, *t2, *t3;
		int mb;
		if (!strncmp(line, "cap_mb ", 7)) {
			have_cap = old_digits_only(line + 7, &out->cap_mb);
			continue;
		}
		if (!strncmp(line, "used_mb ", 8)) {
			old_digits_only(line + 8, &out->used_mb);
			continue;
		}
		t1 = strchr(line, '\t');
		if (!t1)
			continue;
		*t1++ = 0;
		t2 = strchr(t1, '\t');
		if (!t2)
			continue;
		*t2++ = 0;
		t3 = strchr(t2, '\t');
		if (!t3)
			continue;
		*t3++ = 0;
		if (!translate_code_ok(line) || strlen(line) >= TR_LANG || !old_digits_only(t2, &mb))
			continue;
		if (strcmp(t3, "installed") && strcmp(t3, "available"))
			continue;
		if (!t_utf8_ok((const unsigned char *)t1, (long)strlen(t1)) || out->n >= TR_LANGS_MAX)
			continue;
		str_copy(out->l[out->n].code, line, TR_LANG);
		str_copy(out->l[out->n].name, t1, TR_LANGNAME);
		out->l[out->n].mb = mb;
		out->l[out->n].installed = out->l[out->n].want = !strcmp(t3, "installed");
		out->n++;
	}
	return have_cap;
}


static unsigned long rnd_state = 12345;
static unsigned rnd(unsigned n)
{
	rnd_state = rnd_state * 1103515245UL + 12345UL;
	return (unsigned)((rnd_state >> 16) % n);
}

static void same_parse(void)
{
	static const char *rowpool[] = {
		"fr\tFrench\t125\tavailable\n", "he\t\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\t229\tinstalled\n",
		"de\tGerman\t286\tavailable\n", "es\tSpanish\t354\tavailable\n", "en\tEnglish\t0\tinstalled\n",
		"pt-BR\tPortuguese (Brazil)\t129\tavailable\r\n", "zt\tChinese (traditional)\t139\tinstalled\n",
		"ru\t\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9\t336\tavailable\n", "cap_mb 1108\n", "used_mb 230\n",
		"cap_mb 600\r\n", "xx\tCaf\xC3\xA9\t12\tavailable\n", "\n", "garbage line\n",
	};
	static const char alphabet[] = "\t\n abcxyz019-:_\xC3\xA9\xD7\xA2\xFF\x80";	/* (no CR: the first way dropped every CR, a CR inside a field is no list) */
	static TR_LANGS a, b;
	long iter, bad = 0, kept = 0;
	for (iter = 0; iter < 30000; iter++) {
		char buf[900];
		long len = 0, k, m;
		int ra, rb, i;
		int rows = (int)rnd(14) + 1;
		for (i = 0; i < rows; i++) {
			const char *r = rowpool[rnd((unsigned)(sizeof(rowpool) / sizeof(rowpool[0])))];
			long rl = (long)strlen(r);
			if (len + rl < (long)sizeof(buf) - 8) {
				memcpy(buf + len, r, rl);
				len += rl;
			}
		}
		m = (long)rnd(5);		/* 0 to 4 mangles */
		for (k = 0; k < m && len > 0; k++) {
			long at = (long)rnd((unsigned)len);
			switch (rnd(4)) {
			case 0:
				buf[at] = alphabet[rnd(sizeof(alphabet) - 1)];
				break;
			case 1:
				memmove(buf + at + 1, buf + at, (size_t)(len - at));
				buf[at] = alphabet[rnd(sizeof(alphabet) - 1)];
				len++;
				break;
			case 2:
				memmove(buf + at, buf + at + 1, (size_t)(len - at - 1));
				len--;
				break;
			default:
				len = at;		/* cut short */
			}
		}
		for (k = 0; k < len; k++)		/* a CR is a line end's (the first way dropped every CR, even inside a number) */
			if (buf[k] == '\r' && (k + 1 >= len || buf[k + 1] != '\n'))
				buf[k] = 'x';
		memset(&a, 0xAA, sizeof(a));
		memset(&b, 0xBB, sizeof(b));
		ra = old_langs_parse(&a, buf, len);
		rb = translate_langs_parse(&b, buf, len);
		if (ra != rb || a.cap_mb != b.cap_mb || a.used_mb != b.used_mb || a.n != b.n) {
			bad++;
			continue;
		}
		for (i = 0; i < a.n; i++)
			if (strcmp(a.l[i].code, b.l[i].code) || strcmp(a.l[i].name, b.l[i].name) || a.l[i].mb != b.l[i].mb ||
			    a.l[i].installed != b.l[i].installed || a.l[i].want != b.l[i].want)
				bad++;
		kept += a.n;
	}
	check(bad == 0 && kept > 20000, "languages: 30,000 mangled lists parse as the first way read them");
	printf("     (%ld rows kept in all, %ld differences)\n", kept, bad);
}

int main(void)
{
	SBUF out, req;
	char err[160], from[TR_LANG];
	static char big[30000];
	long i, k;

	conn_logfile[0] = 'L';	/* any path: pf_open above takes it */
	conn_logfile[1] = 0;

	/* codes */
	check(translate_code_ok("en") && translate_code_ok("he") && translate_code_ok("pt-BR") &&
	      translate_code_ok("zh-Hans") && translate_code_ok("yue"), "language codes in");
	check(!translate_code_ok("") && !translate_code_ok("e") && !translate_code_ok("english") &&
	      !translate_code_ok("EN") && !translate_code_ok("en ") && !translate_code_ok("en\r\nX: y") &&
	      !translate_code_ok("en&to=x") && !translate_code_ok("en-") && !translate_code_ok("en-x") &&
	      !translate_code_ok(NULL), "everything else out");

	/* split */
	check(translate_split("abc", 3, 10) == 3, "split: it all fits");
	check(translate_split("aaaa\n\nbbbb\n\ncccc", 16, 12) == 12, "split: at the last paragraph break that fits (12)");
	check(translate_split("aaaa\n\nbbbbbbb\ncc cc", 19, 17) == 6, "split: the paragraph break wins over a later line end (6)");
	check(translate_split("aaaa bbbb cccc dddd", 19, 12) == 10, "split: else at a space");
	check(translate_split("Aaa bbb. Ccc ddd eee fff", 24, 18) == 9, "split: a sentence end before a line end or a space");
	check(translate_split("Aaa bbb\nccc ddd. Eee fff ggg", 28, 20) == 17, "split: ...and before an earlier line end");
	check(translate_split("aaaabbbbccccdddd", 16, 7) == 7, "split: else at the limit");
	check(translate_split("aaaa\r\n\r\nbbbbbbbbbbbbbbbbbbbb", 28, 10) == 8, "split: CR LF CR LF is a paragraph break");
	{
		const char *h = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";	/* 2 bytes a letter */
		check(translate_split(h, 16, 5) == 4 && translate_split(h, 16, 3) == 2, "split: never inside a UTF-8 character");
	}
	for (i = 0; i < (long)sizeof(big); i++)
		big[i] = i % 100 == 99 ? '\n' : (i % 7 == 6 ? ' ' : 'a' + i % 26);
	for (k = 0, i = 0; i < (long)sizeof(big);) {
		long p = translate_split(big + i, sizeof(big) - i, TR_PIECE);
		if (p < 1 || p > TR_PIECE)
			break;
		i += p;
		k++;
	}
	check(i == (long)sizeof(big) && k >= 4, "split: 30,000 bytes in pieces that add up, none over 8 KB");

	/* the request */
	sb_init(&req);
	check(translate_request(&req, "PI_IP", "en", "he", "shalom", 6) &&
	      !strncmp(req.s, "POST /translate?to=en&from=he HTTP/1.0\r\nHost: PI_IP\r\n", 53) &&
	      strstr(req.s, "Content-Length: 6\r\n") && strstr(req.s, "charset=utf-8") &&
	      !strcmp(req.s + req.len - 10, "\r\n\r\nshalom"), "request with from, length and body");
	check(translate_request(&req, "PI_IP", "en", "", "x", 1) && !strstr(req.s, "from="), "request: no from means detect");
	check(!translate_request(&req, "PI_IP", "en\r\nX: 1", NULL, "x", 1) &&
	      !translate_request(&req, "PI IP", "en", NULL, "x", 1) &&
	      !translate_request(&req, "PI_IP", "en", "h e", "x", 1) &&
	      !translate_request(&req, "", "en", NULL, "x", 1), "request: nothing unchecked reaches a line");
	sb_free(&req);

	/* status line and refusals */
	check(translate_status_code("HTTP/1.0 200 OK") == 200 && translate_status_code("HTTP/1.1 422 x") == 422 &&
	      translate_status_code("HTTP/1.0 200") == 200 && !translate_status_code("HTTP/2 200 OK") &&
	      !translate_status_code("ICY 200 OK") && !translate_status_code("HTTP/1.0 2x0 OK") &&
	      !translate_status_code("HTTP/1.0 2000 OK") && !translate_status_code(""), "status lines");

	/* the phases for the status line: send, wait, receive, done, in that order,
   with the sizes; none for the quick questions */
	{
		extern void (*conn_phase)(int, long);
		reset();
		nph = 0;
		conn_phase = on_phase;
		sb_init(&out);
		script(0, ok200(0, "Hello world", "he"), -1);
		translate_piece("PI_IP", 8480, "en", NULL, "SHALOM OLAM", 11, &out, from, NULL, err, sizeof(err));
		check(nph == 4 && ph[0] == GATE_SEND && phb[0] == sentlen && ph[1] == GATE_WAIT && ph[2] == GATE_RECEIVE &&
		      phb[2] == 11 && ph[3] == GATE_DONE, "phases: send (request size), wait, receive (answer size), done");
		reset();
		nph = 0;
		script(0, "HTTP/1.0 503 Loading\r\n\r\n", -1);
		sb_reset(&out);
		translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err));
		check(nph == 3 && ph[1] == GATE_WAIT && ph[2] == GATE_DONE, "phases: a refusal has no receive, and ends with done");
		reset();
		nph = 0;
		refuse_open = 1;
		translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err));
		check(nph == 2 && ph[0] == GATE_SEND && ph[1] == GATE_DONE, "phases: no connection gives send, then done");
		reset();
		nph = 0;
		script(0, "HTTP/1.0 200 OK\r\nContent-Length: 50\r\n\r\nshort", -1);
		translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err));
		check(nph == 4 && ph[3] == GATE_DONE, "phases: an answer that breaks off still ends with done");
		reset();
		nph = 0;
		{
			char info[80];
			script(0, "HTTP/1.0 200 OK\r\nContent-Length: 4\r\n\r\nok\nr", -1);
			translate_status("PI_IP", 8480, info, sizeof(info), err, sizeof(err));
			check(nph == 0, "phases: none for /status, a quick question");
		}
		conn_phase = 0;
		sb_free(&out);
	}

	/* a piece: 200 with a Content-Length */
	sb_init(&out);
	reset();
	script(0, ok200(0, "Hello world", "he"), -1);
	check(translate_piece("PI_IP", 8480, "en", NULL, "SHALOM OLAM", 11, &out, from, NULL, err, sizeof(err)) &&
	      out.len == 11 && !memcmp(out.s, "Hello world", 11) && !strcmp(from, "he"), "a piece: answer and the language used");
	check(!strncmp(sent, "POST /translate?to=en HTTP/1.0\r\n", 32) && sentlen > 11 && !memcmp(sent + sentlen - 11, "SHALOM OLAM", 11),
	      "a piece: the request as built went out");

	/* refusals */
	{
		static const struct { int code; const char *text; } r[] = {
			{ 400, "HTTP/1.0 400 Bad Request\r\n\r\nxyzzy-a" }, { 413, "HTTP/1.0 413 Too Large\r\n\r\nxyzzy-b" },
			{ 422, "HTTP/1.0 422 No Pair\r\n\r\nxyzzy-c" }, { 503, "HTTP/1.0 503 Loading\r\n\r\nxyzzy-d" },
			{ 504, "HTTP/1.0 504 Slow\r\n\r\nxyzzy-e" },
		};
		for (i = 0; i < 5; i++) {
			char what[80];
			reset();
			script(0, r[i].text, -1);
			sb_reset(&out);
			snprintf(what, sizeof(what), "refusal %d: nothing returned, the app's own words in err", r[i].code);
			check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)) &&
			      out.len == 0 && err[0] && !strstr(err, "xyzzy") &&
			      !strcmp(err, translate_refusal(r[i].code)), what);
		}
	}

	/* answers it must not take */
	reset();
	script(0, "<html>hello</html>", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)), "not an HTTP answer: refused");
	reset();
	script(0, "HTTP/1.0 200 OK\r\nContent-Length: 99999\r\n\r\nx", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)), "more than 4 times the request: refused");
	reset();
	script(0, ok200(0, "caf\xE9 latin1", "de"), -1);
	sb_reset(&out);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)) && out.len == 0, "not UTF-8: refused");
	reset();
	script(0, ok200(0, "a\n\nb\n\nc\n\nd\n\ne", "de"), -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "one paragraph only", 18, &out, from, NULL, err, sizeof(err)), "more paragraphs than the request: refused");
	reset();
	script(0, "HTTP/1.0 200 OK\r\nContent-Length: 20\r\n\r\nshort", -1);
	check(!translate_piece("PI_IP", 8480, "en", NULL, "hello", 5, &out, from, NULL, err, sizeof(err)), "an answer that breaks off: refused");
	reset();
	refuse_open = 1;
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 1, &out, from, NULL, err, sizeof(err)) && !strcmp(err, "connection refused"), "no Gateway: the network's own error");
	check(!translate_piece("PI_IP", 8480, "en", NULL, "x", 0, &out, from, NULL, err, sizeof(err)) &&
	      !translate_piece("PI_IP", 8480, "en", NULL, big, TR_PIECE + 1, &out, from, NULL, err, sizeof(err)) &&
	      !translate_piece("PI_IP", 8480, "EN", NULL, "x", 1, &out, from, NULL, err, sizeof(err)), "empty, too long, bad code: no connection tried");
	check(nconn == 0, "...and none was");

	/* an HTTP/1.0 answer ended by the close */
	reset();
	script(0, "HTTP/1.0 200 OK\r\nX-Translate-From: fr\r\n\r\nBonjour tout le monde", -1);
	sb_reset(&out);
	check(translate_piece("PI_IP", 8480, "en", NULL, "Hello", 5, &out, from, NULL, err, sizeof(err)) && out.len == 21 && !strcmp(from, "fr"),
	      "no Content-Length: read to the close");

	/* status and languages */
	{
		char info[80];
		reset();
		script(0, ok200(0, "translategate 1\nengine argos 1.9.6\nready\n", "en"), -1);
		check(translate_status("PI_IP", 8480, info, sizeof(info), err, sizeof(err)) &&
		      !strcmp(info, "translategate 1, engine argos 1.9.6, ready"), "GET /status as one line");
		check(!strncmp(sent, "GET /status HTTP/1.0\r\n", 22), "...a GET");
		reset();
		script(0, ok200(0, "hello world", "en"), -1);
		check(!translate_status("PI_IP", 8480, info, sizeof(info), err, sizeof(err)), "a server that isn't translategate: refused");
		reset();
		script(0, "HTTP/1.0 200 OK\r\n\r\nen\tEnglish\nhe\t\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\n", -1);
		sb_reset(&out);
		check(translate_languages("PI_IP", 8480, &out, err, sizeof(err)) && strstr(out.s, "en\tEnglish\n") &&
		      strstr(out.s, "he\t\xD7\xA2"), "GET /languages");
	}

	/* the Languages page: the list with sizes, the ticks, the request, the job */
	{
		static const char list[] =
			"cap_mb 600\nused_mb 229\n"
			"fr\tFrench\t125\tavailable\n"
			"he\t\xD7\xA2\xD7\x91\xD7\xA8\xD7\x99\xD7\xAA\t229\tinstalled\n"
			"de\tGerman\t286\tavailable\n"
			"es\tSpanish\t354\tavailable\n"
			"bad code\tNo\t5\tavailable\n"
			"xx\tNo mb\tx\tavailable\n"
			"en\tEnglish\t0\tinstalled\n";
		static TR_LANGS L;
		static TR_JOB J;
		char codes[64];
		int job = -1;
		check(translate_langs_parse(&L, list, (long)strlen(list)) && L.cap_mb == 600 && L.used_mb == 229 && L.n == 5,
		      "languages: the list parsed, bad rows left out");
		check(L.l[1].installed && L.l[1].want && !L.l[0].installed && L.l[1].mb == 229 && !strcmp(L.l[4].code, "en"),
		      "languages: installed and wanted start the same");
		check(translate_langs_total(&L) == 229 && !translate_langs_changed(&L), "languages: the total and nothing changed");
		check(translate_langs_fits(&L, 2) && translate_langs_over(&L, 2) == 0, "languages: German fits (229 + 286 of 600)");
		L.l[2].want = 1;
		check(translate_langs_total(&L) == 515 && translate_langs_changed(&L), "languages: ticking German: 515 MB, changed");
		check(!translate_langs_fits(&L, 3) && translate_langs_over(&L, 3) == 269,
		      "languages: Spanish would go 269 MB past the room");
		check(translate_langs_codes(&L, codes, sizeof(codes)) && !strcmp(codes, "he,de"), "languages: the set is he,de (no English)");
		check(!translate_langs_codes(&L, codes, 4), "languages: no room for the codes: refused");
		L.l[1].want = 0;
		check(translate_langs_codes(&L, codes, sizeof(codes)) && !strcmp(codes, "de"), "languages: Hebrew unticked: de");
		L.l[2].want = 0;
		check(translate_langs_codes(&L, codes, sizeof(codes)) && !codes[0], "languages: nothing ticked: an empty set");
		check(!translate_langs_parse(&L, "hello\n", 6), "languages: not the list: refused");

		reset();
		script(0, ok200(0, list, "en"), -1);
		check(translate_langs_get("PI_IP", 8480, &L, err, sizeof(err)) && L.n == 5 &&
		      !strncmp(sent, "GET /languages?all=1 HTTP/1.0\r\n", 31), "languages: GET /languages?all=1");

		reset();
		script(0, "HTTP/1.0 202 Accepted\r\nX-Translate-Job: 7\r\nContent-Length: 6\r\n\r\njob 7\n", -1);
		check(translate_langs_set("PI_IP", 8480, "he,de", &job, err, sizeof(err)) && job == 7 &&
		      !strncmp(sent, "POST /languages HTTP/1.0\r\n", 26) && strstr(sent, "Content-Length: 5\r\n") &&
		      !strncmp(sent + sentlen - 5, "he,de", 5), "languages: POST /languages, job 7");
		reset();
		script(0, "HTTP/1.0 200 OK\r\nContent-Length: 11\r\n\r\njob 0\ndone\n", -1);
		check(translate_langs_set("PI_IP", 8480, "he", &job, err, sizeof(err)) && job == 0, "languages: nothing to do: job 0");
		reset();
		script(0, "HTTP/1.0 422 Unprocessable\r\nContent-Length: 0\r\n\r\n", -1);
		check(!translate_langs_set("PI_IP", 8480, "he,de,es", &job, err, sizeof(err)) && err[0], "languages: refused: 422");
		check(strstr(err, "don't fit") != NULL, "languages: a 422 says the languages don't fit (not 'that language pair')");
		reset();
		script(0, "HTTP/1.0 503 Busy\r\nContent-Length: 0\r\n\r\n", -1);
		check(!translate_langs_set("PI_IP", 8480, "he", &job, err, sizeof(err)) && strstr(err, "busy with languages"),
		      "languages: a 503 says a job is running");
		{
			/* AERA-Gateway's table has 50 rows (en and 49): none may be dropped */
			static char big_list[8000];
			int k, len = snprintf(big_list, sizeof(big_list), "cap_mb 1108\nused_mb 0\nen\tEnglish\t0\tinstalled\n");
			for (k = 0; k < 49; k++)
				len += snprintf(big_list + len, sizeof(big_list) - len, "%c%c\tLanguage %d\t%d\tavailable\n",
						'a' + k / 26, 'a' + k % 26, k, 100 + k);
			check(translate_langs_parse(&L, big_list, len) && L.n == 50 && !strcmp(L.l[49].code, "bw"),
			      "languages: the Gateway's 50 rows all kept (TR_LANGS_MAX was 48)");
		}
		same_parse();
		{
			static TR_LANGS P;
			char big_name[200];
			int k, len;
			check(translate_langs_parse(&P, "cap_mb 600\r\nfr\tFrench\t125\tavailable", 35) && P.n == 1 && !strcmp(P.l[0].name, "French") &&
			      P.cap_mb == 600, "languages: CR LF line ends, no line end on the last row");
			strcpy(big_name, "cap_mb 1\nxx\t");
			for (k = 0; k < 22; k++)
				strcat(big_name, "\xC3\xA9");	/* 44 bytes of 2-byte letters: over TR_LANGNAME */
			strcat(big_name, "\t5\tavailable\n");
			len = (int)strlen(big_name);
			check(translate_langs_parse(&P, big_name, len) && P.n == 1 && strlen(P.l[0].name) == 38 &&
			      !(((unsigned char)P.l[0].name[37]) & 0x40) , "languages: a long name is cut between two letters, never inside one");
		}
		check(translate_timeout_ms == 60000L, "translate_timeout_ms starts at 60 s");
		reset();
		check(!translate_langs_set("PI_IP", 8480, "he;de", &job, err, sizeof(err)) && nconn == 0,
		      "languages: a code with a stray character is never sent");

		check(translate_job_parse(&J, "job 7\nrunning\nbytes 120 of 290\n", 31) && J.id == 7 && J.state == TR_JOB_RUNNING &&
		      J.done == 120 && J.total == 290, "job: running, 120 of 290");
		check(translate_job_parse(&J, "job 7\ndone\n", 11) && J.state == TR_JOB_DONE, "job: done");
		check(translate_job_parse(&J, "job 8\nfailed: no room\n", 22) && J.state == TR_JOB_FAILED && !strcmp(J.why, "no room"),
		      "job: failed with its reason");
		check(translate_job_parse(&J, "job 0\nnone\n", 11) && J.state == TR_JOB_NONE, "job: none");
		check(!translate_job_parse(&J, "what\n", 5), "job: not a job answer: refused");
		reset();
		script(0, ok200(0, "job 7\nrunning\nbytes 1 of 2\n", "en"), -1);
		check(translate_job_get("PI_IP", 8480, &J, err, sizeof(err)) && J.done == 1 && !strncmp(sent, "GET /job HTTP/1.0\r\n", 19),
		      "job: GET /job");
	}

	/* the whole text, Hebrew in Atari text, three pieces */
	{
		/* 0xC2.. are Atari's Hebrew letters (alef = 0xC2, bet = 0xC3) */
		static char atari[24000], want[24000];
		char word[] = { (char)0xC2, (char)0xC3, (char)0xC4, ' ', 0 };
		long n = 0, wn = 0, outlen = 0;
		char *res;
		static char eng[40][100];
		while (n < 20000) {
			memcpy(atari + n, word, 4);
			n += 4;
			if (n % 400 == 0) {
				atari[n++] = '\n';
				atari[n++] = '\n';
			}
		}
		reset();
		/* the "engine" answers each piece with the same letters in UTF-8: alef bet gimel */
		for (i = 0; i < 40; i++) {
			long m = 0;
			m += snprintf(eng[i] + m, sizeof(eng[i]) - m, "PIECE%ld \xD7\x90\xD7\x91\xD7\x92\n\n", i);
			script(i, ok200(i, eng[i], "he"), -1);
		}
		res = translate_atari("PI_IP", 8480, "en", NULL, atari, n, NULL, from, &outlen, err, sizeof(err));
		if (!res)
			printf("     err: %s (connections %d)\n", err, nconn);
		check(res != NULL, "the whole text in pieces (UTF-8 doubles the Hebrew), answers joined");
		if (res) {
			char *u = atari_to_utf8(res, outlen, &wn);
			check(nconn >= 15 && nconn <= 40 && strstr(res, "PIECE0") && strstr(res, "PIECE1") && strstr(res, "PIECE2") &&
			      (unsigned char)strstr(res, "PIECE0")[7] == 0xC2 && !strcmp(from, "he"),
			      "...in order, Hebrew back in Atari's letters");
			check(strstr(res, "PIECE0") < strstr(res, "PIECE1") && strstr(res, "\n\nPIECE1"), "...paragraph breaks kept between the pieces");
			free(u);
			free(res);
		}
		(void)want;
	}

	/* already in the target language: no body, the piece stands */
	reset();
	script(0, "HTTP/1.0 200 OK\r\nX-Translate-From: en\r\nX-Translate-Same: 1\r\nContent-Length: 0\r\n\r\n", -1);
	sb_reset(&out);
	{
		int same = 0;
		check(translate_piece("PI_IP", 8480, "en", NULL, "Already English.", 16, &out, from, &same, err, sizeof(err)) &&
		      same == 1 && out.len == 16 && !memcmp(out.s, "Already English.", 16) && !strcmp(from, "en"),
		      "already in the target language: 200, no body, same=1, the text as it was");
	}

	/* the cursor: a piece at a time, the app shows each, a stop between or inside */
	{
		static char text[9000];
		TRANSLATE t;
		SBUF got;
		int r, steps = 0;
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 60 == 59 ? '\n' : (i % 11 == 10 ? ' ' : 'a' + i % 26);
		reset();
		for (i = 0; i < 10; i++)
			script(i, ok200(i, "piece", "de"), -1);
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, sizeof(text) - 1);
		sb_init(&got);
		while ((r = translate_step(&t, &got)) > 0)
			steps++;
		check(r == 0 && steps == nconn && steps >= 4 && steps <= 6 && got.len == 5 * steps && t.pos == t.len &&
		      !strcmp(t.from_used, "de"), "translate_step: 2 KB pieces to the end, the first language kept");
		sb_free(&got);
		/* a failure keeps what was done and says why */
		reset();
		script(0, ok200(0, "one", "de"), -1);
		script(1, "HTTP/1.0 504 Slow\r\n\r\nx", -1);
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, sizeof(text) - 1);
		sb_init(&got);
		check(translate_step(&t, &got) == 1 && translate_step(&t, &got) == -1 &&
		      !strcmp(t.err, translate_refusal(504)) && got.len == 3 && t.pos > 0 && t.pos < t.len,
		      "translate_step: a 504 on the second piece: -1, the first stays");
		sb_free(&got);
		/* the app drops it at once: conn_stop set from pf_idle */
		reset();
		script(0, "HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\nxx", -1);	/* the rest never comes */
		translate_begin(&t, "PI_IP", 8480, "en", NULL, text, 300);
		sb_init(&got);
		conn_stop = 1;
		check(translate_step(&t, &got) == -1 && !strcmp(t.err, "stopped") && got.len == 0, "conn_stop: the piece is dropped, \"stopped\"");
		conn_stop = 0;
		sb_free(&got);
	}

	/* UTF-8 in and out, no Atari conversion */
	{
		char *r;
		long rl = 0;
		reset();
		for (i = 0; i < 4; i++)
			script(i, ok200(i, "\xD0\x9F\xD1\x80\xD0\xB8", "ru"), -1);
		r = translate_utf8("PI_IP", 8480, "en", NULL, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", 12, NULL, from, &rl, err, sizeof(err));
		check(r && rl == 6 && !memcmp(r, "\xD0\x9F\xD1\x80\xD0\xB8", 6) && !r[6] && !strcmp(from, "ru"), "translate_utf8: Cyrillic stays Cyrillic, no '?'");
		free(r);
	}

	/* a turn between pieces, and a stop */
	{
		static int turns, stop_at;
		static long last_done;
		static char text[20000];
		char *r;
		int stopper(long done, long total) { turns++; last_done = done; return stop_at && turns >= stop_at; }
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 50 == 49 ? '\n' : 'a' + i % 26;
		reset();
		for (i = 0; i < 40; i++)
			script(i, ok200(i, "ok", "en"), -1);
		turns = 0;
		stop_at = 0;
		r = translate_atari("PI_IP", 8480, "he", "en", text, sizeof(text) - 1, stopper, from, NULL, err, sizeof(err));
		check(r != NULL && turns >= 8 && turns == nconn && last_done > 0, "a turn before each piece (2 KB a piece)");
		free(r);
		reset();
		for (i = 0; i < 40; i++)
			script(i, ok200(i, "ok", "en"), -1);
		turns = 0;
		stop_at = 2;
		r = translate_atari("PI_IP", 8480, "he", "en", text, sizeof(text) - 1, stopper, from, NULL, err, sizeof(err));
		check(r == NULL && !strcmp(err, "stopped") && nconn == 1, "Esc between pieces: nothing partial, no more connections");
	}

	/* a refusal in the middle returns nothing */
	{
		static char text[20000];
		char *r;
		for (i = 0; i < (long)sizeof(text) - 1; i++)
			text[i] = i % 50 == 49 ? '\n' : 'a' + i % 26;
		reset();
		script(0, ok200(0, "first", "en"), -1);
		script(1, "HTTP/1.0 503 Busy\r\n\r\nbusy", -1);
		r = translate_atari("PI_IP", 8480, "he", NULL, text, sizeof(text) - 1, NULL, from, NULL, err, sizeof(err));
		check(r == NULL && !strcmp(err, translate_refusal(503)), "a refusal on the second piece: NULL and the reason");
	}

	/* no word of the text in the protocol log */
	{
		static const char secret[] = "THE-SECRET-LETTER-TEXT";
		reset();
		script(0, ok200(0, "THE-SECRET-ANSWER-TEXT", "en"), -1);
		sb_reset(&out);
		translate_piece("PI_IP", 8480, "en", NULL, secret, sizeof(secret) - 1, &out, from, NULL, err, sizeof(err));
		conn_log_flush();
		check(loglen > 0 && !memmem(logbuf, loglen, "SECRET", 6), "the protocol log has the pair and the sizes, no word of the text or of the answer");
		check(memmem(logbuf, loglen, "auto>en, 22 bytes", 17) != NULL, "...the pair and the size are there");
	}

	sb_free(&out);
	printf("%s\n", fails ? "FAILED" : "all translate checks passed");
	return fails != 0;
}
