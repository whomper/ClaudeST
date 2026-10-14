/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * alert.h - the AERA programs' alert (the guidelines, 6.2): form_alert
 * with printf formatting, the text wrapped into lines of at most 30
 * characters, at most 5 lines, and Hebrew put into display order (bidi),
 * since form_alert shows everything left to right. From ECal's and
 * EContact's ui/field.c.
 *
 *   alert(1, "[3][Can't reach %s.|Check the network.][ OK ]", host);
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
