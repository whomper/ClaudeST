/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * charset.c - mail charsets <-> the Atari ST character set.
 *
 * Everything the user sees is kept in the Atari charset, where Hebrew
 * letters sit at 0xC2-0xDC, so the system font shows them directly.
 * Incoming text (UTF-8, ISO-8859-1/15, windows-1252, ISO-8859-8 and
 * windows-1255 Hebrew) is converted on the way in; outgoing mail is
 * sent as UTF-8.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "charset.h"
#include "charset_tab.h"
#include "bidi.h"

typedef unsigned char u8;

int cs_id(const char *name)
{
	char n[32];
	int i;
	if (!name || !*name)
		return CS_ASCII;
	for (i = 0; i < 31 && name[i]; i++)
		n[i] = (char)tolower((u8)name[i]);
	n[i] = 0;
	if (!strcmp(n, "utf-8") || !strcmp(n, "utf8"))
		return CS_UTF8;
	if (!strcmp(n, "us-ascii") || !strcmp(n, "ascii"))
		return CS_ASCII;
	if (!strcmp(n, "iso-8859-15") || !strcmp(n, "latin9"))
		return CS_LATIN15;
	if (!strcmp(n, "windows-1252") || !strcmp(n, "cp1252"))
		return CS_CP1252;
	if (!strncmp(n, "iso-8859-8", 10) || !strcmp(n, "hebrew") || !strcmp(n, "iso_8859-8"))
		return CS_ISO8859_8;
	if (!strcmp(n, "windows-1255") || !strcmp(n, "cp1255"))
		return CS_CP1255;
	if (!strcmp(n, "atari") || !strcmp(n, "atarist"))
		return CS_ATARI;
	return CS_LATIN1;	/* iso-8859-1 and anything unknown */
}

unsigned long atari_to_uni(u8 c)
{
	return c < 0x80 ? c : st_high[c - 0x80];
}

/* Latin-1 supplement -> Atari byte (0 = not in the ST font), built once */
static u8 latin1_map[96];
static short latin1_ready;

static void init_latin1(void)
{
	short i;
	for (i = 0; i < 128; i++)
		if (st_high[i] >= 0xA0 && st_high[i] <= 0xFF)
			latin1_map[st_high[i] - 0xA0] = (u8)(0x80 + i);
	latin1_ready = 1;
}

static int is_dropped(unsigned long u)
{
	if (u >= 0x0300 && u <= 0x036F)			/* combining accents */
		return 1;
	if (u >= 0x0591 && u <= 0x05C7 && u != 0x05BE && u != 0x05C0 && u != 0x05C3 && u != 0x05C6)
		return 1;				/* Hebrew points (niqqud) */
	if ((u >= 0x200B && u <= 0x200F) || (u >= 0x202A && u <= 0x202E) ||
	    (u >= 0x2066 && u <= 0x2069) || u == 0x061C || u == 0xFEFF ||
	    (u >= 0xFE00 && u <= 0xFE0F))
		return 1;				/* direction marks, ZW chars */
	if (u >= 0x1F000 || (u >= 0x2600 && u <= 0x27BF) || (u >= 0xE000 && u <= 0xF8FF))
		return 1;				/* emoji, pictographs */
	return 0;
}

int uni_to_atari(unsigned long u, char *out)
{
	short i, lo, hi;
	if (u < 0x80) {
		if (u < 32 && u != 9 && u != 10 && u != 13)
			return 0;
		if (u == 127)
			return 0;
		out[0] = (char)u;
		return 1;
	}
	if (u >= 0x05D0 && u <= 0x05EA) {		/* Hebrew letters */
		for (i = 0x42; i <= 0x5C; i++)
			if (st_high[i] == u) {
				out[0] = (char)(0x80 + i);
				return 1;
			}
	}
	if (u >= 0xA0 && u <= 0xFF) {
		if (!latin1_ready)
			init_latin1();
		if (latin1_map[u - 0xA0]) {
			out[0] = (char)latin1_map[u - 0xA0];
			return 1;
		}
	} else {
		for (i = 0; i < 128; i++)
			if (st_high[i] == u) {
				out[0] = (char)(0x80 + i);
				return 1;
			}
	}
	if (is_dropped(u))
		return 0;
	lo = 0;
	hi = sizeof(fallback) / sizeof(fallback[0]) - 1;
	while (lo <= hi) {
		short mid = (lo + hi) / 2;
		if (fallback[mid].u == u) {
			short n = (short)strlen(fallback[mid].s);
			memcpy(out, fallback[mid].s, n);
			return n;
		}
		if (fallback[mid].u < u)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	out[0] = '?';
	return 1;
}

/* decode one character from src[*i]; never fails (bad UTF-8 is Latin-1) */
static unsigned long next_char(const u8 *s, long n, long *i, int cs)
{
	u8 c = s[*i];
	(*i)++;
	if (c < 0x80)
		return c;
	switch (cs) {
	case CS_UTF8: {
		short need;
		unsigned long u;
		long j;
		if ((c & 0xE0) == 0xC0) {
			need = 1;
			u = c & 0x1F;
		} else if ((c & 0xF0) == 0xE0) {
			need = 2;
			u = c & 0x0F;
		} else if ((c & 0xF8) == 0xF0) {
			need = 3;
			u = c & 0x07;
		} else {
			return c;
		}
		if (*i + need > n)
			return c;
		for (j = 0; j < need; j++)
			if ((s[*i + j] & 0xC0) != 0x80)
				return c;
		for (j = 0; j < need; j++)
			u = (u << 6) | (s[*i + j] & 0x3F);
		*i += need;
		return u;
	}
	case CS_CP1252:
		return c < 0xA0 ? cp1252_80[c - 0x80] : c;
	case CS_LATIN15:
		switch (c) {
		case 0xA4: return 0x20AC;
		case 0xA6: return 0x0160;
		case 0xA8: return 0x0161;
		case 0xB4: return 0x017D;
		case 0xB8: return 0x017E;
		case 0xBC: return 0x0152;
		case 0xBD: return 0x0153;
		case 0xBE: return 0x0178;
		}
		return c;
	case CS_ISO8859_8:
		return c < 0xA0 ? c : iso8859_8_a0[c - 0xA0];
	case CS_CP1255:
		return cp1255_80[c - 0x80];
	case CS_ATARI:
		return st_high[c - 0x80];
	case CS_ASCII:		/* 8-bit in "ASCII" text is usually Latin-1 */
	case CS_LATIN1:
	default:
		return c;
	}
}

char *cs_to_atari(const char *src, long n, int cs, long *outlen)
{
	const u8 *s = (const u8 *)src;
	char *out = malloc(n * 3 + 1), *p = out;
	long i = 0;
	if (!out)
		return 0;
	if (cs == CS_ATARI) {
		memcpy(out, src, n);
		p = out + n;
	} else {
		while (i < n) {
			unsigned long u;
			if (s[i] < 0x80) {	/* fast path */
				u8 c = s[i++];
				if (c >= 32 || c == 9 || c == 10 || c == 13)
					*p++ = (char)c;
				continue;
			}
			u = next_char(s, n, &i, cs);
			p += uni_to_atari(u, p);
		}
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

char *atari_to_utf8(const char *src, long n, long *outlen)
{
	char *out = malloc(n * 3 + 1), *p = out;
	long i;
	if (!out)
		return 0;
	for (i = 0; i < n; i++) {
		unsigned long u = atari_to_uni((u8)src[i]);
		if (u < 0x80) {
			*p++ = (char)u;
		} else if (u < 0x800) {
			*p++ = (char)(0xC0 | (u >> 6));
			*p++ = (char)(0x80 | (u & 0x3F));
		} else {
			*p++ = (char)(0xE0 | (u >> 12));
			*p++ = (char)(0x80 | ((u >> 6) & 0x3F));
			*p++ = (char)(0x80 | (u & 0x3F));
		}
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

int has_8bit(const char *s, long n)
{
	while (n-- > 0)
		if ((u8)*s++ >= 0x80)
			return 1;
	return 0;
}

/* ---------------- base64 ---------------- */

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64val(u8 c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == ',') return 62;	/* ',' is modified UTF-7's '/' */
	if (c == '/') return 63;
	return -1;
}

char *base64_decode(const char *src, long n, long *outlen)
{
	char *out = malloc(n * 3 / 4 + 4), *p = out;
	unsigned long acc = 0;
	short bits = 0;
	long i;
	if (!out)
		return 0;
	for (i = 0; i < n; i++) {
		int v;
		if (src[i] == '=')
			break;
		v = b64val((u8)src[i]);
		if (v < 0)
			continue;
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*p++ = (char)(acc >> bits);
		}
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

char *base64_encode(const char *src, long n, int linelen, long *outlen)
{
	long cap = (n + 2) / 3 * 4 + 1 + (linelen ? ((n / 3 * 4) / linelen + 1) * 2 : 0);
	char *out = malloc(cap + 4), *p = out;
	const u8 *s = (const u8 *)src;
	long i;
	short col = 0;
	if (!out)
		return 0;
	for (i = 0; i < n; i += 3) {
		unsigned long v = (unsigned long)s[i] << 16;
		if (i + 1 < n)
			v |= (unsigned long)s[i + 1] << 8;
		if (i + 2 < n)
			v |= s[i + 2];
		*p++ = b64[(v >> 18) & 63];
		*p++ = b64[(v >> 12) & 63];
		*p++ = i + 1 < n ? b64[(v >> 6) & 63] : '=';
		*p++ = i + 2 < n ? b64[v & 63] : '=';
		col += 4;
		if (linelen && col >= linelen && i + 3 < n) {
			*p++ = '\r';
			*p++ = '\n';
			col = 0;
		}
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

/* ---------------- quoted-printable ---------------- */

static int hexval(u8 c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

char *qp_decode(const char *src, long n, int header, long *outlen)
{
	char *out = malloc(n + 1), *p = out;
	long i;
	if (!out)
		return 0;
	for (i = 0; i < n; i++) {
		u8 c = (u8)src[i];
		if (c == '=') {
			if (i + 1 < n && src[i + 1] == '\n') {
				i++;
				continue;
			}
			if (i + 2 < n && src[i + 1] == '\r' && src[i + 2] == '\n') {
				i += 2;
				continue;
			}
			if (i + 2 < n && hexval((u8)src[i + 1]) >= 0 && hexval((u8)src[i + 2]) >= 0) {
				*p++ = (char)(hexval((u8)src[i + 1]) * 16 + hexval((u8)src[i + 2]));
				i += 2;
				continue;
			}
		}
		if (header && c == '_')
			c = ' ';
		*p++ = (char)c;
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

char *qp_encode(const char *src, long n, long *outlen)
{
	char *out = malloc(n * 3 + n / 70 * 3 + 8), *p = out;
	long i;
	short col = 0;
	if (!out)
		return 0;
	for (i = 0; i < n; i++) {
		u8 c = (u8)src[i];
		short w;
		if (c == '\r')
			continue;
		if (c == '\n') {
			*p++ = '\r';
			*p++ = '\n';
			col = 0;
			continue;
		}
		/* encode =, 8-bit, controls, and spaces at the end of a line */
		w = (c == '=' || c >= 0x7F || (c < 32 && c != 9) ||
		     ((c == ' ' || c == 9) && (i + 1 == n || src[i + 1] == '\n' || src[i + 1] == '\r'))) ? 3 : 1;
		if (col + w > 75) {
			*p++ = '=';
			*p++ = '\r';
			*p++ = '\n';
			col = 0;
		}
		if (w == 3) {
			*p++ = '=';
			*p++ = "0123456789ABCDEF"[c >> 4];
			*p++ = "0123456789ABCDEF"[c & 15];
		} else {
			*p++ = (char)c;
		}
		col += w;
	}
	*p = 0;
	if (outlen)
		*outlen = p - out;
	return out;
}

/* ---------------- RFC 2047 header words ---------------- */

/* if s starts an encoded word, decode it, append to *p, return its length */
/* one RFC 2047 word at s: its decoded bytes and charset; 0 if it isn't one */
static long try_word(const char *s, char **raw, long *rawlen, char *cs)
{
	const char *cs_end, *enc_end, *end;
	int b;
	if (s[0] != '=' || s[1] != '?')
		return 0;
	cs_end = strchr(s + 2, '?');
	if (!cs_end || cs_end - (s + 2) >= 40 || !cs_end[1] || cs_end[2] != '?')
		return 0;
	b = toupper((u8)cs_end[1]) == 'B';
	if (!b && toupper((u8)cs_end[1]) != 'Q')
		return 0;
	enc_end = cs_end + 3;
	end = strstr(enc_end, "?=");
	if (!end)
		return 0;
	memcpy(cs, s + 2, cs_end - (s + 2));
	cs[cs_end - (s + 2)] = 0;
	if (strchr(cs, '*'))		/* RFC 2231 language tag */
		*strchr(cs, '*') = 0;
	*raw = b ? base64_decode(enc_end, end - enc_end, rawlen)
		 : qp_decode(enc_end, end - enc_end, 1, rawlen);
	if (!*raw)
		return 0;
	return end + 2 - s;
}

/* Adjacent words in the same charset are joined before converting: a
 * UTF-8 letter may be split between two of them. */
typedef struct {
	char *buf;
	long len;
	char cs[40];
} PENDING;

static void flush(PENDING *pd, char **p)
{
	char *conv;
	long convlen;
	if (!pd->len)
		return;
	conv = cs_to_atari(pd->buf, pd->len, cs_id(pd->cs), &convlen);
	if (conv) {
		memcpy(*p, conv, convlen);
		*p += convlen;
		free(conv);
	}
	pd->len = 0;
}

char *hdr_decode(const char *v)
{
	long n = strlen(v);
	char *out = malloc(n * 3 + 1), *p = out;
	const char *s = v;
	int last_word = 0;
	PENDING pd;
	if (!out)
		return 0;
	pd.buf = malloc(n + 1);
	pd.len = 0;
	pd.cs[0] = 0;
	if (!pd.buf) {
		free(out);
		return 0;
	}
	while (*s) {
		char cs[40], *raw;
		long rawlen, used;
		const char *t = s;
		if (last_word)		/* whitespace between two encoded words disappears */
			while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')
				t++;
		used = try_word(t, &raw, &rawlen, cs);
		if (used) {
			if (pd.len && strcasecmp(pd.cs, cs))
				flush(&pd, &p);
			strcpy(pd.cs, cs);
			memcpy(pd.buf + pd.len, raw, rawlen);	/* decoded words are shorter */
			pd.len += rawlen;
			free(raw);
			s = t + used;
			last_word = 1;
			continue;
		}
		flush(&pd, &p);
		last_word = 0;
		{
			/* raw text: 8-bit here is usually UTF-8 (or Latin-1) */
			char *conv;
			long cl;
			t = s;
			while (*t && !(t[0] == '=' && t[1] == '?'))
				t++;
			if (t == s)
				t++;
			conv = cs_to_atari(s, t - s, CS_UTF8, &cl);
			if (conv) {
				char *c;
				for (c = conv; c < conv + cl; c++)
					*p++ = (*c == '\r' || *c == '\n' || *c == '\t') ? ' ' : *c;
				free(conv);
			}
			s = t;
		}
	}
	flush(&pd, &p);
	free(pd.buf);
	*p = 0;
	return out;
}

/* Atari text -> RFC 2047 B words of UTF-8, folded with CRLF SPACE */
char *hdr_encode(const char *atari)
{
	long n = strlen(atari), ul, i, start;
	char *utf, *out, *p;
	if (!has_8bit(atari, n))
		return strdup(atari);
	utf = atari_to_utf8(atari, n, &ul);
	if (!utf)
		return 0;
	out = malloc(ul * 2 + ul / 30 * 16 + 32);
	if (!out) {
		free(utf);
		return 0;
	}
	p = out;
	for (start = 0; start < ul; start = i) {
		char *b;
		long bl;
		i = start + 42;
		if (i >= ul)
			i = ul;
		while (i < ul && ((u8)utf[i] & 0xC0) == 0x80)	/* don't split a character */
			i--;
		b = base64_encode(utf + start, i - start, 0, &bl);
		if (start)
			p += strlen(strcpy(p, "\r\n "));
		p += strlen(strcpy(p, "=?UTF-8?B?"));
		memcpy(p, b, bl);
		p += bl;
		p += strlen(strcpy(p, "?="));
		free(b);
	}
	*p = 0;
	free(utf);
	return out;
}

/* ---------------- IMAP modified UTF-7 ---------------- */

char *mutf7_to_atari(const char *name)
{
	long n = strlen(name);
	char *out = malloc(n * 3 + 1), *p = out;
	const char *s = name;
	if (!out)
		return 0;
	while (*s) {
		if (*s != '&') {
			*p++ = *s++;
			continue;
		}
		s++;
		if (*s == '-') {
			*p++ = '&';
			s++;
			continue;
		}
		{
			unsigned long acc = 0, hi = 0;
			short bits = 0;
			while (*s && *s != '-') {
				int v = b64val((u8)*s++);
				if (v < 0)
					break;
				acc = (acc << 6) | v;
				bits += 6;
				if (bits >= 16) {
					unsigned long u;
					bits -= 16;
					u = (acc >> bits) & 0xFFFF;
					if (u >= 0xD800 && u <= 0xDBFF) {
						hi = u;
						continue;
					}
					if (u >= 0xDC00 && u <= 0xDFFF && hi) {
						u = 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00);
						hi = 0;
					}
					p += uni_to_atari(u, p);
				}
			}
			if (*s == '-')
				s++;
		}
	}
	*p = 0;
	return out;
}

char *atari_to_mutf7(const char *name)
{
	long n = strlen(name);
	char *out = malloc(n * 6 + 4), *p = out;
	const u8 *s = (const u8 *)name;
	if (!out)
		return 0;
	while (*s) {
		if (*s >= 0x20 && *s < 0x7F) {
			*p++ = (char)*s;
			if (*s == '&')
				*p++ = '-';
			s++;
			continue;
		}
		{
			unsigned long acc = 0;
			short bits = 0;
			*p++ = '&';
			while (*s && !(*s >= 0x20 && *s < 0x7F)) {
				unsigned long u = atari_to_uni(*s++);
				acc = (acc << 16) | (u & 0xFFFF);
				bits += 16;
				while (bits >= 6) {
					bits -= 6;
					*p++ = (char)(((acc >> bits) & 63) == 63 ? ',' : b64[(acc >> bits) & 63]);
				}
			}
			if (bits)
				*p++ = (char)(((acc << (6 - bits)) & 63) == 63 ? ',' : b64[(acc << (6 - bits)) & 63]);
			*p++ = '-';
		}
	}
	*p = 0;
	return out;
}
