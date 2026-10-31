/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * clip.h - the GEM clipboard (the AERA guidelines, 4.4): SCRAP.TXT in the
 * folder the AES keeps (scrp_read; X:\CLIPBRD\ on the boot drive when it
 * has none yet), in the Atari character set, lines ending CR LF there.
 *
 *   clip_put(text, n)          ^C, ^X: the clipboard is this text now (the
 *                              old SCRAP.* go; other programs are told,
 *                              SC_CHANGED, where the AES can tell them all)
 *   clip_put_begin()           a long text in slices (a whole document),
 *   clip_put_step(text, n)     each slice's text in turn, and
 *   clip_put_end(ok)           ok 1: the clipboard is it now, as clip_put;
 *                              ok 0: given up, the old clipboard kept. It
 *                              is written 16 KB at a time (32 KB on a
 *                              68020 or better), one Fwrite a piece, and
 *                              no file stays open between the calls.
 *   n = clip_get(buf, size)    ^V: SCRAP.TXT, at most size - 1 bytes, NUL
 *                              ended, lines ending '\n'; 0 when the
 *                              clipboard has no text
 *   clip_line(buf)             for a field of one line: the first line
 *                              only, tabs as spaces
 *
 * text given to clip_put() may end its lines '\n' or CR LF.
 *
 *   switch (clip_key(key))     the key from evnt_multi: CLIP_COPY for ^C,
 *                              CLIP_CUT ^X, CLIP_PASTE ^V, else CLIP_NONE
 *   clip_edit(tree, obj, &idx, key)  in a dialog's own key loop: ^C ^X ^V
 *                              on the edit field obj (clipedit.c), 1 when
 *                              it was one of them. Nothing with nothing
 *                              selected takes the whole field; a paste
 *                              types the clipboard's first line in, as far
 *                              as the field takes it. Never on a password.
 */
#ifndef AERA_CLIP_H
#define AERA_CLIP_H

enum { CLIP_NONE, CLIP_COPY, CLIP_CUT, CLIP_PASTE };

int clip_put(const char *text, long n);		/* 1 when written */
int clip_put_begin(void);			/* 1 when the folder is there */
int clip_put_step(const char *text, long n);	/* 0 once it failed */
int clip_put_end(int ok);			/* 1 when written */
long clip_get(char *buf, long size);
char *clip_line(char *buf);
int clip_key(short key);
#ifdef GEM_H
int clip_edit(OBJECT *tree, short obj, short *idx, short key);
#endif

#endif
