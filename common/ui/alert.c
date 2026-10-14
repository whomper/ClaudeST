/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * alert.c - form_alert with formatting, wrapping and bidi (see alert.h).
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "gem.h"
#include "bidi.h"
#include "alert.h"

void (*alert_hebrew)(char *line, long n);

/* format, then make the message fit: words wrapped into lines of at most
   30 characters, at most 5 lines; Hebrew put into display order */
#define ALW 30

short alert(short def, const char *fmt, ...)
{
	char raw[600], out[700];
	const char *msg, *btn = 0, *p;
	va_list ap;
	short o, lines = 0;
	long k;
	va_start(ap, fmt);
	vsnprintf(raw, sizeof(raw), fmt, ap);
	va_end(ap);
	msg = strstr(raw, "][");
	for (p = raw; (p = strstr(p, "][")); p++)
		btn = p;
	if (!msg || !btn || btn == msg)
		return form_alert(def, raw);
	msg += 2;
	memcpy(out, raw, msg - raw);
	o = (short)(msg - raw);
	while (msg < btn && lines < 5) {
		const char *end = msg, *cut;
		char line[64], vis[64];
		short n = 0;
		while (end < btn && *end != '|')
			end++;
		cut = end;
		if (end - msg > ALW) {
			cut = msg + ALW;
			while (cut > msg && *cut != ' ')
				cut--;
			if (cut == msg)
				cut = msg + ALW;
		}
		while (msg < cut && n < (short)sizeof(line) - 1) {
			char c = *msg++;
			line[n++] = c == '[' ? '(' : c == ']' ? ')' : c;
		}
		if (!n)
			line[n++] = ' ';	/* an empty line: EmuTOS takes "||" for a '|' */
		line[n] = 0;
		if (bidi_has_rtl(line, n)) {
			bidi_visual(line, n, bidi_is_rtl(line, n), vis);
			memcpy(line, vis, n);
			if (alert_hebrew)
				alert_hebrew(line, n);
		}
		if (lines)
			out[o++] = '|';
		if (o + n < (short)sizeof(out) - 60) {
			memcpy(out + o, line, n);
			o += n;
		}
		lines++;
		while (msg < btn && *msg == ' ')
			msg++;
		if (msg < btn && *msg == '|')
			msg++;
	}
	/* the buttons, as they were */
	for (k = 0; btn[k] && o < (short)sizeof(out) - 1; k++)
		out[o++] = btn[k];
	out[o] = 0;
	return form_alert(def, out);
}

int screen_check(const char *app, short width)
{
	if (width >= 640)
		return 1;
	alert(1, "[3][%s needs a screen 640 pixels wide: ST medium or high, or a TT or Falcon mode.][ OK ]", app);
	return 0;
}
