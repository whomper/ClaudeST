/*
 * bidi.h - right-to-left (Hebrew) text for the Atari ST character set.
 */
#ifndef BIDI_H
#define BIDI_H

/* 1 if the first strong character in s[0..n) is Hebrew */
short bidi_is_rtl(const char *s, short n);

/* 1 if s[0..n) contains any Hebrew letter (cheap test for the fast path) */
short bidi_has_rtl(const char *s, short n);

/* Reorder one display line from logical to visual (left-to-right screen)
 * order, for a paragraph of the given base direction (rtl = 1 for
 * Hebrew). Writes n bytes to dst; n must be <= BIDI_MAX. */
#define BIDI_MAX 256
void bidi_visual(const char *src, short n, short rtl, char *dst);

#endif
