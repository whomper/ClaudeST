/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fakenet.c - a stand-in for the network inside LANGTX.PRG: the Gateway's three requests of the
 * Languages page answered in the program itself, as fakegate.py answers them (the same words), so
 * that the dialog can be run where there is no network to the emulator: under FreeMiNT with XaAES
 * (tests/xaaes: no network), or in a window forced on TOS (the file WINDOW). Replaces net_tos.c.
 * The Gateway has Hebrew, room for 600 MB; a job installs 40 MB a second.
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "plat.h"

static const struct { const char *code, *name; int mb; } rows[] = {
	{ "en", "English", 0 },
	{ "ar", "Arabic", 162 },
	{ "az", "Azerbaijani", 234 },
	{ "bg", "Bulgarian", 138 },
	{ "bn", "Bengali", 135 },
	{ "ca", "Catalan", 141 },
	{ "cs", "Czech", 132 },
	{ "da", "Danish", 130 },
	{ "de", "German", 287 },
	{ "el", "Greek", 135 },
	{ "eo", "Esperanto", 233 },
	{ "es", "Spanish", 355 },
	{ "et", "Estonian", 298 },
	{ "eu", "Basque", 144 },
	{ "fa", "Persian", 231 },
	{ "fi", "Finnish", 132 },
	{ "fr", "French", 126 },
	{ "ga", "Irish", 185 },
	{ "gl", "Galician", 145 },
	{ "he", "Hebrew", 230 },
	{ "hi", "Hindi", 199 },
	{ "hu", "Hungarian", 133 },
	{ "id", "Indonesian", 130 },
	{ "it", "Italian", 167 },
	{ "ja", "Japanese", 227 },
	{ "ko", "Korean", 229 },
	{ "ky", "Kyrgyz", 295 },
	{ "lt", "Lithuanian", 133 },
	{ "lv", "Latvian", 133 },
	{ "ms", "Malay", 132 },
	{ "nb", "Norwegian", 127 },
	{ "nl", "Dutch", 135 },
	{ "pb", "Portuguese (Brazil)", 129 },
	{ "pl", "Polish", 133 },
	{ "pt", "Portuguese", 129 },
	{ "ro", "Romanian", 135 },
	{ "ru", "Russian", 336 },
	{ "sk", "Slovak", 136 },
	{ "sl", "Slovenian", 135 },
	{ "sq", "Albanian", 134 },
	{ "sv", "Swedish", 193 },
	{ "sw", "Swahili", 144 },
	{ "th", "Thai", 132 },
	{ "tl", "Tagalog", 134 },
	{ "tr", "Turkish", 234 },
	{ "uk", "Ukrainian", 187 },
	{ "ur", "Urdu", 132 },
	{ "vi", "Vietnamese", 127 },
	{ "zh", "Chinese", 138 },
	{ "zt", "Chinese (traditional)", 139 },
};
#define NROWS ((int)(sizeof(rows) / sizeof(rows[0])))
#define CAP 600

static char have[NROWS], want[NROWS];	/* installed; wanted by the job */
static int job_id, job_on, job_total_mb;
static unsigned long job_t0;
static char req[4200], resp[6000];
static int reqn, respn, respat, is_open;

int net_init(void)
{
	int i;
	for (i = 0; i < NROWS; i++)
		have[i] = !strcmp(rows[i].code, "en") || !strcmp(rows[i].code, "he");
	return 1;
}

const char *net_stack(void)
{
	return "fake (in the program)";
}

static int used_mb(void)
{
	int i, t = 0;
	for (i = 0; i < NROWS; i++)
		if (have[i] && strcmp(rows[i].code, "en"))
			t += rows[i].mb;
	return t;
}

static void settle(void)
{
	if (job_on && tos_ms() - job_t0 >= (unsigned long)(job_total_mb * 1000L / 40 > 1000 ? job_total_mb * 1000L / 40 : 1000)) {
		memcpy(have, want, sizeof(have));
		job_on = 0;
	}
}

static void reply(int status, const char *why, const char *body)
{
	char b[300];
	const char *t = status == 200 ? "OK" : status == 202 ? "Accepted" : "Refused";
	if (!body) {
		snprintf(b, sizeof(b), "translategate: %s\n", why);
		body = b;
	}
	respn = snprintf(resp, sizeof(resp), "HTTP/1.0 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
			 "Content-Length: %d\r\nConnection: close\r\n\r\n%s", status, t, (int)strlen(body), body);
	respat = 0;
}

static void answer(void)
{
	req[reqn] = 0;
	if (!strncmp(req, "GET /languages?all=1 ", 21)) {
		int i, k;
		char body[5400];
		k = snprintf(body, sizeof(body), "cap_mb %d\nused_mb %d\n", CAP, used_mb());
		for (i = 0; i < NROWS; i++)
			k += snprintf(body + k, sizeof(body) - k, "%s\t%s\t%d\t%s\n", rows[i].code, rows[i].name, rows[i].mb,
				      have[i] ? "installed" : "available");
		reply(200, 0, body);
	} else if (!strncmp(req, "GET /job ", 9)) {
		char b[120];
		settle();
		if (!job_id)
			reply(200, 0, "job 0\nnone\n");
		else if (job_on) {
			unsigned long ms = tos_ms() - job_t0, tot = (unsigned long)job_total_mb * 1000UL / 40;
			unsigned long mb;
			if (tot < 1000)
				tot = 1000;
			mb = ms >= tot ? (unsigned long)job_total_mb : (unsigned long)job_total_mb * ms / tot;
			snprintf(b, sizeof(b), "job %d\nrunning\nbytes %lu of %lu\n", job_id, mb << 20, (unsigned long)job_total_mb << 20);
			reply(200, 0, b);
		} else {
			snprintf(b, sizeof(b), "job %d\ndone\n", job_id);
			reply(200, 0, b);
		}
	} else if (!strncmp(req, "POST /languages ", 16)) {
		char *body = strstr(req, "\r\n\r\n"), *p;
		int i, need = 0, same = 1, add = 0;
		char w[NROWS];
		if (!body) {
			reply(400, "no body", 0);
			return;
		}
		body += 4;
		memset(w, 0, sizeof(w));
		for (i = 0; i < NROWS; i++)
			if (!strcmp(rows[i].code, "en"))
				w[i] = 1;
		for (p = body; *p;) {
			char code[8];
			int n = 0, f = -1;
			while (*p && *p != ',' && *p != '\n' && *p != ' ' && n < 7)
				code[n++] = *p++;
			code[n] = 0;
			while (*p == ',' || *p == '\n' || *p == ' ')
				p++;
			for (i = 0; i < NROWS; i++)
				if (!strcmp(rows[i].code, code))
					f = i;
			if (f < 0) {
				reply(400, "a language code isn't right", 0);
				return;
			}
			w[f] = 1;
		}
		for (i = 0; i < NROWS; i++) {
			if (w[i] && strcmp(rows[i].code, "en"))
				need += rows[i].mb;
			if (w[i] != have[i])
				same = 0;
			if (w[i] && !have[i])
				add += rows[i].mb;
		}
		if (need > CAP) {
			char b[80];
			snprintf(b, sizeof(b), "those languages take %d MB; %d MB fit", need, CAP);
			reply(422, b, 0);
			return;
		}
		settle();
		if (same) {
			reply(200, 0, "job 0\ndone\n");
			return;
		}
		if (job_on) {
			reply(503, "a languages job is running", 0);
			return;
		}
		memcpy(want, w, sizeof(want));
		job_id++;
		job_on = 1;
		job_total_mb = add;
		job_t0 = tos_ms();
		{
			char b[40];
			snprintf(b, sizeof(b), "job %d\n", job_id);
			reply(202, 0, b);
		}
	} else {
		reply(404, "no such page", 0);
	}
}

int net_open(const char *host, unsigned short port, char *err, int errlen)
{
	is_open = 1;
	reqn = respn = respat = 0;
	return 5;
}

long net_write(int h, const void *buf, long n)
{
	if (reqn + n < (long)sizeof(req)) {
		memcpy(req + reqn, buf, n);
		reqn += (int)n;
	}
	return n;
}

long net_read(int h, void *buf, long max)
{
	/* the request is answered once it is all here: the headers, and a POST's body by its length */
	if (!respn) {
		char *e;
		req[reqn] = 0;
		e = strstr(req, "\r\n\r\n");
		if (e) {
			long have_body = reqn - (e + 4 - req), want_body = 0;
			char *cl = strstr(req, "Content-Length: ");
			if (cl)
				want_body = 0 + (cl[16] >= '0' && cl[16] <= '9' ? (cl[17] >= '0' && cl[17] <= '9' ?
						 (cl[18] >= '0' && cl[18] <= '9' ? (cl[16] - '0') * 100 + (cl[17] - '0') * 10 + (cl[18] - '0') :
						  (cl[16] - '0') * 10 + (cl[17] - '0')) : cl[16] - '0') : 0);
			if (have_body >= want_body)
				answer();
		}
	}
	if (respat >= respn)
		return respn ? -1 : 0;
	if (max > respn - respat)
		max = respn - respat;
	if (max > 61)
		max = 61;		/* (a few bytes at a time, as a slow line gives them) */
	memcpy(buf, resp + respat, max);
	respat += (int)max;
	return max;
}

void net_close(int h)
{
	is_open = 0;
}

void net_close_wait(int h, int seconds)
{
	is_open = 0;
}
