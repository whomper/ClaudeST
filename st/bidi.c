/*
 * bidi.c - a compact subset of the Unicode bidirectional algorithm for
 * Hebrew in the Atari ST character set (letters at 0xC2-0xDC).
 *
 * Per line: characters are Hebrew (R), Latin letters and digits (L), or
 * neutral. A neutral run between two strong characters of the same
 * direction takes that direction; otherwise the paragraph's. Levels:
 * in a Hebrew paragraph R=1 and L=2, in a Latin one L=0 and R=1; then
 * runs are reversed from the highest level down (rule L2), and brackets
 * at odd levels are mirrored. Numbers stay left to right, so
 * "שנת 1990" and "Claude ST" read correctly inside Hebrew text.
 */
#include "tos.h"
#include "bidi.h"

#define C_L 0
#define C_R 1
#define C_N 2

static short cls(u8 c)
{
	if (c >= 0xC2 && c <= 0xDC)
		return C_R;
	if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
		return C_L;
	if ((c >= 0x80 && c <= 0xA5) || (c >= 0xB0 && c <= 0xB8) || (c >= 0xE0 && c <= 0xEF))
		return C_L;	/* accented Latin, Greek */
	return C_N;
}

short bidi_has_rtl(const char *s, short n)
{
	while (n-- > 0) {
		u8 c = (u8)*s++;
		if (c >= 0xC2 && c <= 0xDC)
			return 1;
	}
	return 0;
}

short bidi_is_rtl(const char *s, short n)
{
	while (n-- > 0) {
		u8 c = (u8)*s++;
		short k = cls(c);
		if (k != C_N && !(c >= '0' && c <= '9'))	/* digits are weak */
			return k == C_R;
	}
	return 0;
}

static u8 mirror(u8 c)
{
	switch (c) {
	case '(': return ')';
	case ')': return '(';
	case '[': return ']';
	case ']': return '[';
	case '{': return '}';
	case '}': return '{';
	case '<': return '>';
	case '>': return '<';
	case 0xAE: return 0xAF;	/* « » */
	case 0xAF: return 0xAE;
	}
	return c;
}

/* reorder src into dst; if pos/odd are given, also report for each
 * logical index its visual index and whether it runs right to left */
void bidi_visual_map(const char *src, short n, short rtl, char *dst, short *pos, u8 *odd)
{
	u8 k[BIDI_MAX], lv[BIDI_MAX];
	short idx[BIDI_MAX];
	short i, j, top = 0, level;
	short base = rtl ? C_R : C_L;

	if (n > BIDI_MAX)
		n = BIDI_MAX;
	for (i = 0; i < n; i++)
		k[i] = (u8)cls((u8)src[i]);

	/* resolve neutral runs */
	for (i = 0; i < n; ) {
		short before, after, d;
		if (k[i] != C_N) {
			i++;
			continue;
		}
		for (j = i; j < n && k[j] == C_N; j++)
			;
		before = i > 0 ? k[i - 1] : base;
		after = j < n ? k[j] : base;
		d = before == after ? before : base;
		for (; i < j; i++)
			k[i] = (u8)d;
	}

	/* embedding levels */
	for (i = 0; i < n; i++) {
		if (rtl)
			lv[i] = k[i] == C_R ? 1 : 2;
		else
			lv[i] = k[i] == C_R ? 1 : 0;
		if (lv[i] > top)
			top = lv[i];
		if (odd)
			odd[i] = lv[i] & 1;
		dst[i] = src[i];
		idx[i] = i;
	}

	/* reverse runs, from the highest level down to 1 */
	for (level = top; level >= 1; level--) {
		for (i = 0; i < n; ) {
			if (lv[i] < level) {
				i++;
				continue;
			}
			for (j = i; j < n && lv[j] >= level; j++)
				;
			{
				short a = i, b = j - 1;
				while (a < b) {
					char t = dst[a];
					u8 tl = lv[a];
					short ti = idx[a];
					dst[a] = dst[b];
					dst[b] = t;
					lv[a] = lv[b];
					lv[b] = tl;
					idx[a] = idx[b];
					idx[b] = ti;
					a++;
					b--;
				}
			}
			i = j;
		}
	}

	/* mirrored glyphs for right-to-left brackets */
	for (i = 0; i < n; i++) {
		if (lv[i] & 1)
			dst[i] = (char)mirror((u8)dst[i]);
		if (pos)
			pos[idx[i]] = i;
	}
}

void bidi_visual(const char *src, short n, short rtl, char *dst)
{
	bidi_visual_map(src, n, rtl, dst, 0, 0);
}
