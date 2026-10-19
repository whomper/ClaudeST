/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
void model_reset(void);
void model_put(unsigned long w);
unsigned long model_get(void);
extern unsigned long model_ops[8];
