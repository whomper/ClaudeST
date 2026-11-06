/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hebkey.h - the Israeli (SI-1452) Hebrew keyboard, one for the family.
 *
 * F10 (or the app's menu entry) switches it: while hebrew_kbd is set,
 * key_char() gives a letter key's Hebrew Atari code (0xC2-0xDC, logical
 * order) in place of its Latin one. Shift, Alt and Control keys are left as
 * they are. The table is by key position (scancode 0x10-0x35).
 *
 *   hebrew_kbd              1 while it is on
 *   set_hebrew_kbd(on)      sets it and calls hebrew_kbd_hook, if any (the
 *                           app's info line or badge shows it)
 *   key_char(kstate, key)   the character a key types: the AES's key word
 *                           (scancode << 8 | ascii) and kstate
 *
 * The definitions are weak: an app that still has its own copies keeps
 * them (no clash) until it drops them. dlg_do (dialog.c) types through
 * key_char when it is linked, so every dialog takes Hebrew.
 */
#ifndef AERA_HEBKEY_H
#define AERA_HEBKEY_H

extern short hebrew_kbd;
extern void (*hebrew_kbd_hook)(void);
void set_hebrew_kbd(short on);
unsigned char key_char(short kstate, short key);

/* the badge: the letters ayin and bet ("ayin bet", read right to left: ayin
   at the right), a bitmap of its own so that it shows in every app and VDI
   font (the AES draws a dialog's title in the system font, which has no
   Hebrew, and EWeb has no Hebrew VDI font), drawn by the app where it
   draws its dialog title or info line: x, y the top left, colour the pen.
   Does nothing while the keyboard is off. hebrew_badge_size gives its
   size (16 x 16 for a text cell 13 rows high or more, else 16 x 8) so the
   app can place it. (ECal's and EContact's field.c draw "\xD3\xC3" as
   text: that is tsadi and bet, not ayin and bet.) */
void hebrew_badge_size(short vdi, short *w, short *h);
void hebrew_badge(short vdi, short x, short y, short colour);

/* where every app puts it (Erez, 6 Nov 2026): at the RIGHT END of a dialog's
   title band and of a main window's info line, in place of the words
   "Hebrew keyboard (F10)". x, y, w, h is that band or line (its inside);
   the badge sits 3 pixels in from the right edge, centred up and down (at
   the top when the band is lower than the badge). Select the font the band
   is drawn in first (it sets the size). Does nothing while the keyboard is
   off, so an app redraws the band when F10 is pressed. */
void hebrew_badge_right(short vdi, short x, short y, short w, short h, short colour);

#endif
