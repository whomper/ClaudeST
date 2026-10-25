/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alert.h - the AERA programs' alert (the guidelines, 6.2): form_alert's
 * string with printf formatting, the text wrapped into lines of at most
 * 30 characters, at most 5 lines, and Hebrew put into display order
 * (bidi), since the alert shows everything left to right. From ECal's
 * and EContact's ui/field.c.
 *
 *   alert(1, "[3][Can't reach %s.|Check the network.][ OK ]", host);
 *
 * It returns 1 to 3, the button, as form_alert does. Return picks the
 * default button, and Esc and Undo the last one (3.5), which is Cancel or
 * the one that does nothing. On TOS and EmuTOS the box is our own
 * (ui/dialog.c runs it, in the system font), since their form_alert
 * ignores Esc; where the AES runs several programs at once (MiNT with
 * XaAES, N.AES or MyAES; MagiC) it is the AES's own form_alert, with the
 * same wrapped text (Erez, 25 Oct 2026: as the other programs there). A string
 * it can't show that way (more than 3 buttons, one over 20 characters,
 * an icon that isn't a digit, a screen too small) goes to form_alert.
 * Links with ui/dialog.c.
 */
#ifndef AERA_ALERT_H
#define AERA_ALERT_H

short alert(short def, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* optional: called with each line that has Hebrew, once it is in display
   order (ECal and EContact map the letters to where the system font has
   them) */
extern void (*alert_hebrew)(char *line, long n);

/* the guidelines' 5.2: on a screen narrower than 640 pixels (ST low) an
   alert says the app needs a wider one; 0 then: a program quits, an
   accessory doesn't open its window */
int screen_check(const char *app, short width);

#endif
