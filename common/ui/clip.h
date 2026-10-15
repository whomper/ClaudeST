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
long clip_get(char *buf, long size);
char *clip_line(char *buf);
int clip_key(short key);
#ifdef GEM_H
int clip_edit(OBJECT *tree, short obj, short *idx, short key);
#endif

#endif
