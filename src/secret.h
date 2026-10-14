/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * secret.h - passwords in the settings file (NAME.INF), encrypted.
 *
 * XTEA in counter mode with a random 128-bit key that the program keeps in
 * NAME.KEY next to NAME.INF, and a fresh random nonce each time a value
 * is written. Plain 68000 code: it works on every ST, not only in Falcon
 * mode. It keeps the passwords out of NAME.INF itself (a copy, a backup,
 * someone reading the file); whoever has both files can still read them.
 */
#ifndef SECRET_H
#define SECRET_H

#define SECRET_TAG "{E}"

/* load NAME.KEY, or make one; 0 if there is none and none can be made */
int  secret_init(const char *keyfile);
/* "{E}" + hex of nonce and encrypted text, into out */
void secret_encode(const char *plain, char *out, int size);
/* plain text of an encoded value, or the value itself when it isn't
   encoded (an older settings file); 0 if it is encoded with another key */
int  secret_decode(const char *in, char *out, int size);

#endif
