/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * THING.ACC - a stand-in AV server for the Hatari checks of av.c (as
 * Thing, the desktop, would be: appl_find("THING   ") finds it). What it
 * is sent goes into C:\AVLOG.TXT; what it does comes from C:\AVSEND.TXT,
 * a line at a time:
 *
 *   until EMAIL         wait (up to 90 s) for AV_PROTOKOLL from EMAIL
 *   await GO.TXT        wait (up to 10 minutes) for C:\GO.TXT, which a
 *                       test puts on the emulated drive when it is ready
 *   wait 3              seconds
 *   send EMAIL|text     VA_START to EMAIL with text (may be empty)
 *   say shot NAME       "AVTEST shot NAME" on the emulator's console, for
 *                       app_av.py (a screenshot; "say key 0x1c": a key),
 *                       then 2 s for it to happen
 *   term EMAIL          AP_TERM to EMAIL
 *   icon EMAIL          WM_ICONIFY to EMAIL for the window on top, then
 *                       a redraw of it: the AES (EmuTOS's) can't shrink
 *                       it, so the icon is drawn in the window as it is
 *   uniconify EMAIL     WM_UNICONIFY to EMAIL, back where it was
 *   dropfiles ECONTACT|C:\A.VCF  files dropped from the desktop on the
 *                       window ECONTACT said it opened (AV_ACCWINDOPEN),
 *                       else the one on top: VA_DRAGACCWIND
 *   drop 300 200 .TXT|text  drag and drop (AP_DRAGDROP, MiNT and MagiC
 *                       only) on the window at 300,200: text (.TXT; \n
 *                       for a new line) or files (ARGS); what the program
 *                       answers is written down
 *   clip text           the clipboard is this text (\n: a new line)
 *   clipshow            what the clipboard holds, written down
 *   done                "AVTEST done"
 */
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "clip.h"

static char script[2048], texts[8][300];
static short ntext, protokoll_from = -1;
static short icon_h, icon_was[4];	/* the window "iconified", and where it was */
static short accwin = -1;		/* the window AV_ACCWINDOPEN named */
static char protokoll_name[9];

static void log_line(const char *s)
{
	static int made;
	char nl[2] = { '\r', '\n' };
	long h = made ? Fopen("C:\\AVLOG.TXT", 1) : Fcreate("C:\\AVLOG.TXT", 0);
	made = 1;
	if (h < 0)
		return;
	Fseek(0, (short)h, 2);
	Fwrite((short)h, strlen(s), s);
	Fwrite((short)h, 2, nl);
	Fclose((short)h);
}

static void hex(char *b, unsigned v)
{
	static const char d[] = "0123456789ABCDEF";
	b[0] = d[(v >> 12) & 15];
	b[1] = d[(v >> 8) & 15];
	b[2] = d[(v >> 4) & 15];
	b[3] = d[v & 15];
	b[4] = 0;
}

static void name8(char *out, const char *in)
{
	short i;
	for (i = 0; i < 8 && in[i] && in[i] != '|'; i++)
		out[i] = in[i];
	for (; i < 8; i++)
		out[i] = ' ';
	out[8] = 0;
}

/* what came in: written down */
static void got(const short *msg)
{
	char l[120], h[5];
	const char *p = (const char *)(((long)msg[6] << 16) | (unsigned short)msg[7]);
	switch (msg[0]) {
	case 0x4700:		/* AV_PROTOKOLL */
		hex(h, (unsigned short)msg[3]);
		strcpy(l, "AV_PROTOKOLL bits ");
		strcat(l, h);
		strcat(l, " name ");
		if (p) {
			strncat(l, p, 8);
			name8(protokoll_name, p);
		}
		protokoll_from = msg[1];
		break;
	case 0x4736:		/* AV_EXIT */
		strcpy(l, msg[3] == msg[1] ? "AV_EXIT" : "AV_EXIT with another id");
		break;
	case 0x4738:		/* AV_STARTED */
		strcpy(l, "AV_STARTED");
		break;
	case 0x4724:		/* AV_ACCWINDOPEN */
	case 0x4726:		/* AV_ACCWINDCLOSED */
		accwin = msg[0] == 0x4724 ? msg[3] : -1;
		strcpy(l, msg[0] == 0x4724 ? "AV_ACCWINDOPEN" : "AV_ACCWINDCLOSED");
		break;
	case 80:		/* SC_CHANGED */
		strcpy(l, "SC_CHANGED");
		break;
	default:
		hex(h, (unsigned short)msg[0]);
		strcpy(l, "message ");
		strcat(l, h);
	}
	log_line(l);
}

/* messages while waiting s seconds; 1 when `until` is met */
static int pause(long ms, const char *until)
{
	short msg[8];
	AEVENT ev;
	long t = 0;
	while (t < ms) {
		short w = evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, msg, &ev);
		if (w & MU_MESAG) {
			got(msg);
			if (until && msg[0] == 0x4700 && !strcmp(protokoll_name, until))
				return 1;
		} else {
			t += 100;
		}
	}
	return 0;
}

/* "\n" in a script line as a new line (CR LF) */
static long unescape(char *s)
{
	char *r = s, *w = s;
	while (*r) {
		if (r[0] == '\\' && r[1] == 'n') {
			*w++ = '\r';
			*w++ = '\n';
			r += 2;
		} else {
			*w++ = *r++;
		}
	}
	*w = 0;
	return w - s;
}

/* the sending side of drag and drop (Atari Compendium, appendix H; cflib's
   dd_create and dd_stry): a pipe, AP_DRAGDROP to the window's owner, then
   one header; the answer written down */
static void drop(short id, short x, short y, const char *kind, const char *data, long n)
{
	static const char *const answer[] = { "DD_OK", "DD_NAK", "DD_EXT", "DD_LEN" };
	char pipe[] = "U:\\PIPE\\DRAGDROP.AA", c, kinds[32], l[120];
	short msg[8], win = wind_find(x, y), owner = -1, d, hdr;
	long fd = -1, mask;
	strcpy(l, "drop ");
	strcat(l, kind);
	strcat(l, ": ");
	if (win <= 0 || !wind_get(win, 20, &owner, &d, &d, &d) || owner < 0) {	/* WF_OWNER */
		strcat(l, "no window there");
		goto out;
	}
	for (pipe[18] = 'A'; pipe[18] <= 'Z'; pipe[18]++)
		if ((fd = Fcreate(pipe, 2)) >= 0)	/* hidden: EOF when the other end goes */
			break;
	if (fd < 0) {
		strcat(l, "no pipe");
		goto out;
	}
	Psignal(SIGPIPE, SIG_IGN);
	memset(msg, 0, sizeof(msg));
	msg[0] = 63;			/* AP_DRAGDROP */
	msg[1] = id;
	msg[3] = win;
	msg[4] = x;
	msg[5] = y;
	msg[7] = (short)(pipe[17] << 8 | pipe[18]);
	appl_write(owner, 16, msg);
	mask = 1L << fd;
	if (Fselect(4000, &mask, 0, 0) <= 0 || !mask || Fread((short)fd, 1, &c) != 1) {
		strcat(l, "no answer");
		goto out;
	}
	if (c != 0 || Fread((short)fd, 32, kinds) != 32) {
		strcat(l, "refused at once");
		goto out;
	}
	hdr = 4 + 4 + 2;		/* the kind, the size, two empty names */
	if (Fwrite((short)fd, 2, &hdr) != 2 || Fwrite((short)fd, 4, kind) != 4 ||
	    Fwrite((short)fd, 4, &n) != 4 || Fwrite((short)fd, 2, "\0") != 2 ||
	    Fread((short)fd, 1, &c) != 1) {
		strcat(l, "broken off");
		goto out;
	}
	strcat(l, c >= 0 && c <= 3 ? answer[(int)c] : "?");
	if (c == 0 && n)
		Fwrite((short)fd, n, data);
out:
	if (fd >= 0)
		Fclose((short)fd);
	log_line(l);
}

static void say(const char *s)
{
	char l[100] = "AVTEST ";
	strncat(l, s, 90);
	strcat(l, "\n");
	nf_debug(l);
}

int main(void)
{
	short msg[8], id = appl_init();
	char line[300], n[9];
	long h, len = 0, at = 0;
	menu_register(id, "  AV server (test)");
	h = Fopen("C:\\AVSEND.TXT", 0);
	if (h >= 0) {
		len = Fread((short)h, sizeof(script) - 1, script);
		Fclose((short)h);
		if (len < 0)
			len = 0;
	}
	script[len] = 0;
	pause(500, 0);
	while (at < len) {
		long e = at, k = 0;
		while (e < len && script[e] != '\n' && script[e] != '\r')
			e++;
		while (at < e && k < (long)sizeof(line) - 1)
			line[k++] = script[at++];
		line[k] = 0;
		while (at < len && (script[at] == '\n' || script[at] == '\r'))
			at++;
		if (!strncmp(line, "until ", 6)) {
			name8(n, line + 6);
			if (strcmp(protokoll_name, n) && !pause(90000L, n))
				log_line("TIMEOUT waiting for AV_PROTOKOLL");
		} else if (!strncmp(line, "await ", 6)) {
			char f[80] = "C:\\";
			long t;
			strncat(f, line + 6, 60);
			for (t = 0; t < 6000 && Fattrib(f, 0, 0) < 0; t++)
				pause(100, 0);
			if (t == 6000)
				log_line("TIMEOUT waiting for the file");
		} else if (!strncmp(line, "wait ", 5)) {
			long s = 0;
			const char *q = line + 5;
			while (*q >= '0' && *q <= '9')
				s = s * 10 + *q++ - '0';
			pause(s * 1000L, 0);
		} else if (!strncmp(line, "send ", 5)) {
			char *t = strchr(line + 5, '|'), *buf = texts[ntext++ & 7];
			short to;
			name8(n, line + 5);
			to = appl_find(n);
			strcpy(buf, t ? t + 1 : "");
			if (to < 0) {
				log_line("not running");
			} else {
				memset(msg, 0, sizeof(msg));
				msg[0] = 0x4711;
				msg[1] = id;
				msg[3] = (short)((long)buf >> 16);
				msg[4] = (short)(long)buf;
				appl_write(to, 16, msg);
				strcpy(line, "VA_START sent: ");
				strncat(line, buf, 200);
				log_line(line);
			}
		} else if (!strncmp(line, "say ", 4)) {
			say(line + 4);
			pause(2000, 0);		/* the screenshot or key first */
		} else if (!strncmp(line, "term ", 5)) {
			short to;
			name8(n, line + 5);
			to = appl_find(n);
			if (to >= 0) {
				memset(msg, 0, sizeof(msg));
				msg[0] = 50;		/* AP_TERM */
				msg[1] = id;
				msg[5] = 50;
				appl_write(to, 16, msg);
				log_line("AP_TERM sent");
			}
		} else if (!strncmp(line, "icon ", 5) || !strncmp(line, "uniconify ", 10)) {
			short to, d, x, y, w, ht, on = line[0] == 'i';
			name8(n, line + (on ? 5 : 10));
			to = appl_find(n);
			if (on)
				wind_get(0, WF_TOP, &icon_h, &d, &d, &d);
			if (to < 0 || icon_h <= 0) {
				log_line("no window to iconify");
				continue;
			}
			memset(msg, 0, sizeof(msg));
			msg[0] = on ? 34 : 35;	/* WM_ICONIFY, WM_UNICONIFY */
			msg[1] = id;
			msg[3] = icon_h;
			if (on) {
				wind_get(icon_h, WF_CURRXYWH, &icon_was[0], &icon_was[1], &icon_was[2], &icon_was[3]);
				msg[4] = 0;		/* where XaAES would put it */
				msg[5] = 400;
				msg[6] = 72;
				msg[7] = 72;
			} else {
				memcpy(&msg[4], icon_was, sizeof(icon_was));
			}
			appl_write(to, 16, msg);
			log_line(on ? "WM_ICONIFY sent" : "WM_UNICONIFY sent");
			pause(500, 0);
			/* what the AES would redraw now */
			wind_get(icon_h, WF_CURRXYWH, &x, &y, &w, &ht);
			form_dial(3, x, y, w, ht);	/* FMD_FINISH */
		} else if (!strncmp(line, "dropfiles ", 10)) {
			char *t = strchr(line + 10, '|'), *buf = texts[ntext++ & 7];
			short to, win = accwin, d, x, y, w, ht;
			name8(n, line + 10);
			to = appl_find(n);
			strcpy(buf, t ? t + 1 : "");
			if (win <= 0)
				wind_get(0, WF_TOP, &win, &d, &d, &d);
			if (to < 0 || win <= 0) {
				log_line("no window to drop on");
				continue;
			}
			wind_get(win, WF_WORKXYWH, &x, &y, &w, &ht);
			memset(msg, 0, sizeof(msg));
			msg[0] = 0x4725;		/* VA_DRAGACCWIND */
			msg[1] = id;
			msg[3] = win;
			msg[4] = x + w / 2;
			msg[5] = y + ht / 2;
			msg[6] = (short)((long)buf >> 16);
			msg[7] = (short)(long)buf;
			appl_write(to, 16, msg);
			strcpy(line, "VA_DRAGACCWIND sent: ");
			strncat(line, buf, 200);
			log_line(line);
		} else if (!strncmp(line, "drop ", 5)) {
			char *p = line + 5, *t = strchr(line, '|'), kind[5];
			short x = 0, y = 0;
			while (*p >= '0' && *p <= '9')
				x = x * 10 + *p++ - '0';
			while (*p == ' ')
				p++;
			while (*p >= '0' && *p <= '9')
				y = y * 10 + *p++ - '0';
			while (*p == ' ')
				p++;
			memcpy(kind, p, 4);
			kind[4] = 0;
			if (t) {
				long len = unescape(t + 1);
				drop(id, x, y, kind, t + 1, len);
			}
		} else if (!strncmp(line, "clip ", 5)) {
			long len = unescape(line + 5);
			if (clip_put(line + 5, len)) {
				log_line("clipboard set");
			} else {
				char dir[160];
				dir[0] = 0;
				scrp_read(dir);
				strcpy(line, "clipboard not set: folder ");
				strcat(line, dir);
				strcat(line, ", boot drive ");
				n[0] = (char)('A' + boot_drive());
				n[1] = 0;
				strcat(line, n);
				log_line(line);
			}
		} else if (!strcmp(line, "clipshow")) {
			char text[200], *q;
			clip_get(text, sizeof(text));
			for (q = text; *q; q++)
				if (*q == '\n')
					*q = '|';
			strcpy(line, "clipboard: ");
			strcat(line, text);
			log_line(line);
		} else if (!strcmp(line, "done")) {
			say("done");
		}
	}
	for (;;)
		pause(1000, 0);
	return 0;
}
