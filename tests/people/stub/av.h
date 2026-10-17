/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* av.h for tests/people: PEOPLE messages go to the test */
#ifndef AERA_AV_H
#define AERA_AV_H
int av_people(short id, const char *text);
int av_people_ready(void);
const char *av_name(void);
extern void (*av_people_hook)(const char *s, short from);
#endif
