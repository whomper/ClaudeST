/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dd.c - things dropped on our windows (see dd.h). The receiving side of
 * the drag and drop protocol of MultiTOS (Atari Compendium, appendix H;
 * as cflib's dd_open, dd_rtry and dd_reply), and the AV protocol's
 * VA_DRAGACCWIND.
 */
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "dd.h"

#define AP_DRAGDROP	63
#define VA_DRAGACCWIND	0x4725
#define AV_STARTED	0x4738

#define DD_OK		0		/* the replies */
#define DD_NAK		1
#define DD_EXT		2		/* not that kind: another, please */
#define DD_LEN		3		/* too long */
#define DD_EXTSIZE	32		/* the kinds we take: 8 of 4 bytes */

static long rd(short fd, void *buf, long n)
{
	return Fread(fd, n, buf) == n;
}

static int reply(short fd, char c)
{
	return Fwrite(fd, 1, &c) == 1;
}

/* text as the program keeps it: '\n' at the line ends */
static long lf(char *s, long n)
{
	long i, k = 0;
	for (i = 0; i < n; i++) {
		if (s[i] == '\r') {
			s[k++] = '\n';
			if (i + 1 < n && s[i + 1] == '\n')
				i++;
		} else if (s[i]) {
			s[k++] = s[i];
		}
	}
	s[k] = 0;
	return k;
}

/* the drag and drop pipe: we say what we take, the sender offers one kind
   at a time, until we take one or it gives up */
static int from_pipe(const short *msg, int want, DROP *d)
{
	/* copied, not initialised: GCC 13 at -O2 fills an initialised char
	   array with long moves, at an odd stack offset here (an address
	   error on a 68000: tools/oddaccess.py, EPDF 31 Oct 2026) */
	char name[20], kinds[1 + DD_EXTSIZE], type[5], skip[64];
	long fd, oldsig, size, tries;
	short hdr, k;
	int got = DD_REFUSED;
	strcpy(name, "U:\\PIPE\\DRAGDROP.AA");
	name[17] = (char)(msg[7] >> 8);
	name[18] = (char)msg[7];
	fd = Fopen(name, 2);
	if (fd < 0)
		return DD_REFUSED;
	/* a sender that closes its end mustn't end us */
	oldsig = Psignal(SIGPIPE, SIG_IGN);
	memset(kinds, 0, sizeof(kinds));
	kinds[0] = want ? DD_OK : DD_NAK;
	if (want & DD_WANT_FILES)
		memcpy(kinds + 1, "ARGS", 4);
	if (want & DD_WANT_TEXT)
		memcpy(kinds + 1 + ((want & DD_WANT_FILES) ? 4 : 0), ".TXT", 4);
	if (Fwrite((short)fd, sizeof(kinds), kinds) != (long)sizeof(kinds) || !want)
		goto out;
	for (tries = 0; tries < 8; tries++) {
		/* the header: its length, the kind, the size, then names we skip */
		if (!rd((short)fd, &hdr, 2) || hdr < 8 || !rd((short)fd, type, 4) || !rd((short)fd, &size, 4))
			break;
		type[4] = 0;
		for (hdr -= 8; hdr > 0; hdr -= k) {
			k = hdr > (short)sizeof(skip) ? (short)sizeof(skip) : hdr;
			if (!rd((short)fd, skip, k))
				goto out;
		}
		if (!((want & DD_WANT_FILES) && !strcmp(type, "ARGS")) &&
		    !((want & DD_WANT_TEXT) && !strcmp(type, ".TXT"))) {
			if (!reply((short)fd, DD_EXT))
				break;
			continue;
		}
		if (size < 0 || size > DD_MAX || !(d->data = malloc(size + 1))) {
			if (!reply((short)fd, DD_LEN))
				break;
			continue;
		}
		if (!reply((short)fd, DD_OK) || (size && !rd((short)fd, d->data, size))) {
			dd_done(d);
			break;
		}
		d->data[size] = 0;
		if (type[0] == '.') {
			d->len = lf(d->data, size);
			got = DD_TEXT;
		} else {
			d->len = size;
			got = DD_FILES;
		}
		break;
	}
out:
	Fclose((short)fd);
	if (oldsig != -32)			/* -32: no Psignal (not MiNT) */
		Psignal(SIGPIPE, oldsig);
	return got;
}

/* the desktop: files on an accessory's window, their names in its memory */
static int from_desktop(const short *msg, int want, DROP *d)
{
	const char *p = (const char *)(((unsigned long)(unsigned short)msg[6] << 16) | (unsigned short)msg[7]);
	short ack[8];
	long n = 0;
	int got = DD_REFUSED;
	if ((want & DD_WANT_FILES) && p) {
		while (p[n] && n < 4096)
			n++;
		if ((d->data = malloc(n + 1)) != 0) {
			memcpy(d->data, p, n);
			d->data[n] = 0;
			d->len = n;
			got = DD_FILES;
		}
	}
	/* as for VA_START: the sender may let its copy go */
	memset(ack, 0, sizeof(ack));
	ack[0] = AV_STARTED;
	ack[1] = appl_id();
	ack[3] = msg[6];
	ack[4] = msg[7];
	appl_write(msg[1], 16, ack);
	return got;
}

int dd_message(const short *msg, int want, DROP *d)
{
	memset(d, 0, sizeof(*d));
	if (msg[0] != AP_DRAGDROP && msg[0] != VA_DRAGACCWIND)
		return DD_NONE;
	d->win = msg[3];
	d->x = msg[4];
	d->y = msg[5];
	if (msg[0] == VA_DRAGACCWIND)
		return from_desktop(msg, want, d);
	d->kstate = msg[6];
	return from_pipe(msg, want, d);
}

void dd_done(DROP *d)
{
	if (d->data)
		free(d->data);
	d->data = 0;
	d->len = 0;
}
