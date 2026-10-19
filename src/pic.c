/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pic.c - pictures (pic.h): JPEG, PNG and GIF decoded to the size they
 * are shown at, and the screen's own colours for them.
 *
 * Each decoder gives its rows in turn, at its own size, to the scaler,
 * which makes the picture's rows as they come: a shrinking picture is
 * averaged (each pixel shown is the mean of the pixels it covers,
 * weighted by how solid they are), an enlarged one repeats its pixels.
 * So only the shown picture is kept whole, never the file's: a 1000x750
 * photo shown at 320x240 takes 230 KB, not 3 MB. A JPEG is first
 * unpacked at 1/2, 1/4 or 1/8 when it is shown that small or smaller,
 * which skips most of the work.
 *
 * The scaler's columns come from a table made once a picture, and its
 * sums are 32-bit additions: the only divisions are one a component of
 * each pixel shown.
 */
#include <stdlib.h>
#include <string.h>

#include "pic.h"
#include "png.h"
#include "nsgif.h"
#ifdef PIC_LIBJPEG
#include <stdio.h>		/* jpeglib.h names FILE */
#include "jpeglib.h"
#include "jerror.h"
#else
#include "jpegdc.h"
#include "tjpgd.h"
#endif

int (*pic_poll)(void);

/* ---------------------------------------------------------------- kinds */

int pic_kind(const unsigned char *d, long n)
{
	long i;
	if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
		return PIC_JPEG;
	if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
		return PIC_PNG;
	if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
		return PIC_GIF;
	if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
		return PIC_WEBP;
	if (n >= 12 && memcmp(d + 4, "ftyp", 4) == 0 && (memcmp(d + 8, "avif", 4) == 0 || memcmp(d + 8, "avis", 4) == 0))
		return PIC_AVIF;
	if (n >= 2 && d[0] == 'B' && d[1] == 'M')
		return PIC_BMP;
	if (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 1 && d[3] == 0)
		return PIC_ICO;
	for (i = 0; i + 4 <= n && i < 1024; i++)
		if (memcmp(d + i, "<svg", 4) == 0)
			return PIC_SVG;
	return PIC_NONE;
}

const char *pic_kind_name(int kind)
{
	switch (kind) {
	case PIC_JPEG: return "JPEG";
	case PIC_PNG: return "PNG";
	case PIC_GIF: return "GIF";
	case PIC_BMP: return "BMP";
	case PIC_WEBP: return "WebP";
	case PIC_AVIF: return "AVIF";
	case PIC_SVG: return "SVG";
	case PIC_ICO: return "icon";
	}
	return "unknown";
}

const char *pic_why_text(int why)
{
	switch (why) {
	case PIC_OK: return "decoded";
	case PIC_DAMAGED: return "damaged";
	case PIC_UNSUPPORTED: return "a kind of picture that can't be shown";
	case PIC_MEMORY: return "not enough memory";
	case PIC_TOO_BIG: return "too big";
	case PIC_STOPPED: return "stopped";
	}
	return "not decoded";
}

/* a JPEG's size, from its frame header (SOF0..SOF15 but DHT, JPG, DAC) */
static int jpeg_size(const unsigned char *d, long n, long *w, long *h)
{
	long p = 2;
	while (p + 4 <= n) {
		int m;
		long len;
		if (d[p] != 0xFF) {
			p++;
			continue;
		}
		m = d[p + 1];
		if (m == 0xFF) {
			p++;
			continue;
		}
		if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
			p += 2;
			continue;
		}
		len = ((long) d[p + 2] << 8) | d[p + 3];
		if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
			if (p + 9 > n)
				return 0;
			*h = ((long) d[p + 5] << 8) | d[p + 6];
			*w = ((long) d[p + 7] << 8) | d[p + 8];
			return *w > 0 && *h > 0;
		}
		if (m == 0xD9 || m == 0xDA || len < 2)
			return 0;
		p += 2 + len;
	}
	return 0;
}

int pic_size(const unsigned char *d, long n, long *w, long *h)
{
	switch (pic_kind(d, n)) {
	case PIC_JPEG:
		return jpeg_size(d, n, w, h);
	case PIC_PNG:
		return png_size(d, n, w, h) && *w > 0 && *h > 0;
	case PIC_GIF:
		if (n < 10)
			return 0;
		*w = d[6] | ((long) d[7] << 8);
		*h = d[8] | ((long) d[9] << 8);
		return *w > 0 && *h > 0;
	}
	return 0;
}

/* ---------------------------------------------------------------- the scaler */

typedef struct {
	PIC *pic;
	long sw, sh;			/* the rows that come */
	int down_x, down_y;		/* shrinking (else the same or enlarging) */
	short *xmap;			/* down_x: each column's pixel shown; else each shown pixel's column */
	unsigned long *sum;		/* down: dw * 5 (r, g, b weighted by alpha; alpha; pixels) */
	long dy;			/* the row shown being made */
	long y;				/* rows taken */
	int any_alpha;
	int polls;
} SCALER;

/* n source pixels to m shown, by their centres: down, each source
   pixel's pixel shown; else each shown pixel's source */
static long centre(long i, long from, long to)
{
	return ((2 * i + 1) * to) / (2 * from);
}

static short *map(long n, long m, int down)
{
	long i, cnt = down ? n : m;
	short *t = malloc((size_t) cnt * sizeof *t);
	if (t == NULL)
		return NULL;
	for (i = 0; i < cnt; i++)
		t[i] = (short) (down ? centre(i, n, m) : centre(i, m, n));
	return t;
}

static int sc_init(SCALER *s, PIC *p, long sw, long sh)
{
	memset(s, 0, sizeof *s);
	s->pic = p;
	s->sw = sw;
	s->sh = sh;
	s->down_x = sw > p->w;
	s->down_y = sh > p->h;
	s->xmap = map(sw, p->w, s->down_x);
	s->sum = calloc((size_t) p->w * 5, sizeof *s->sum);
	return s->xmap != NULL && s->sum != NULL;
}

static void sc_free(SCALER *s)
{
	free(s->xmap);
	free(s->sum);
}

/* pixel i of the picture shown */
static void put(SCALER *s, long i, int r, int g, int b, int a)
{
	s->pic->px[i] = (unsigned short) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
	s->pic->alpha[i] = (unsigned char) a;
	if (a != 255)
		s->any_alpha = 1;
}

/* the sums made into row dy, and cleared */
static void flush(SCALER *s, long dy)
{
	long x, i = dy * s->pic->w;
	unsigned long *q = s->sum;
	for (x = 0; x < s->pic->w; x++, q += 5, i++) {
		unsigned long n = q[4], a = q[3];
		if (n == 0) {
			put(s, i, 0, 0, 0, 0);
		} else if (a == 255 * n) {
			put(s, i, (int) (q[0] / n), (int) (q[1] / n), (int) (q[2] / n), 255);
		} else if (a == 0) {
			put(s, i, 0, 0, 0, 0);
		} else {
			/* the colour as solid, weighted by alpha: sum (c * (a + 1) >> 8) / sum a */
			unsigned long r = q[0] * 255 / a, g = q[1] * 255 / a, b = q[2] * 255 / a;
			put(s, i, r > 255 ? 255 : (int) r, g > 255 ? 255 : (int) g, b > 255 ? 255 : (int) b,
			    (int) (a / n));
		}
		q[0] = q[1] = q[2] = q[3] = q[4] = 0;
	}
}

/* enlarging n rows to m: the first row shown whose source is row i or
   later, the least d with centre(d, m, n) >= i */
static long first_shown(long i, long n, long m)
{
	long d = (2 * i * m - m + 2 * n - 1) / (2 * n);
	if (d < 0)
		d = 0;
	while (d > 0 && centre(d - 1, m, n) >= i)
		d--;
	while (d < m && centre(d, m, n) < i)
		d++;
	return d;
}

/* a row of the decoded picture, R G B A */
static int sc_row(SCALER *s, const unsigned char *rgba)
{
	long x, w = s->pic->w;
	if (pic_poll != NULL && ++s->polls >= 16) {
		s->polls = 0;
		if (!pic_poll())
			return 0;
	}
	if (s->y >= s->sh)
		return 1;
	if (s->sw == w && s->sh == s->pic->h) {
		/* the size it is: as it is */
		const unsigned char *q = rgba;
		unsigned short *o = s->pic->px + s->y * w;
		unsigned char *a = s->pic->alpha + s->y * w;
		unsigned all = 255;
		for (x = 0; x < w; x++, q += 4) {
			o[x] = (unsigned short) (((q[0] & 0xF8) << 8) | ((q[1] & 0xFC) << 3) | (q[2] >> 3));
			a[x] = q[3];
			all &= q[3];
		}
		if (all != 255)
			s->any_alpha = 1;
		s->y++;
		return 1;
	}
	{
		/* the row's pixels to the columns shown */
		unsigned long *q;
		if (s->down_x) {
			const unsigned char *c = rgba;
			for (x = 0; x < s->sw; x++, c += 4) {
				q = s->sum + s->xmap[x] * 5;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		} else {
			for (x = 0, q = s->sum; x < w; x++, q += 5) {
				const unsigned char *c = rgba + s->xmap[x] * 4;
				if (c[3] == 255) {
					q[0] += c[0];
					q[1] += c[1];
					q[2] += c[2];
					q[3] += 255;
				} else if (c[3] != 0) {
					unsigned a1 = (unsigned) c[3] + 1;
					q[0] += (c[0] * a1) >> 8;
					q[1] += (c[1] * a1) >> 8;
					q[2] += (c[2] * a1) >> 8;
					q[3] += c[3];
				}
				q[4]++;
			}
		}
	}
	if (s->down_y) {
		/* rows shown are made when the next one's first row comes */
		long dy = s->y + 1 < s->sh ? centre(s->y + 1, s->sh, s->pic->h) : -1;
		if (dy != s->dy) {
			flush(s, s->dy);
			s->dy = dy;
		}
	} else {
		/* every row shown whose centre falls in this one: those from
		   dy0 to before dy1 */
		long dy0 = first_shown(s->y, s->sh, s->pic->h), dy1 = first_shown(s->y + 1, s->sh, s->pic->h), dy;
		if (dy1 > dy0) {
			flush(s, dy0);
			for (dy = dy0 + 1; dy < dy1; dy++)
				memcpy(s->pic->px + dy * w, s->pic->px + dy0 * w, (size_t) w * 2),
				memcpy(s->pic->alpha + dy * w, s->pic->alpha + dy0 * w, (size_t) w);
		} else {
			memset(s->sum, 0, (size_t) w * 5 * sizeof *s->sum);
		}
	}
	s->y++;
	return 1;
}

/* ---------------------------------------------------------------- JPEG */

#ifdef PIC_LIBJPEG
/* libjpeg-turbo (third_party/libjpeg-turbo), the library NetSurf takes:
   every kind of JPEG a page has (progressive ones too), unpacked by its
   IDCT straight at n/8 of the size (n from 1 to 8), the smallest still
   as big as shown; on a 68030 1.8 times as quick as TJpgDec at the same
   size, and six times on a big photo shown small */

typedef struct {
	struct jpeg_error_mgr pub;
	void *jmp[5];			/* __builtin_setjmp's */
} LJERR;

static void lj_exit(j_common_ptr c)
{
	__builtin_longjmp(((LJERR *) c->err)->jmp, 1);
}

static void lj_emit(j_common_ptr c, int level)
{
	(void) c;
	(void) level;
}

static void lj_output(j_common_ptr c)
{
	(void) c;
}

static void lj_format(j_common_ptr c, char *buf)
{
	(void) c;
	buf[0] = 0;
}

static void lj_reset(j_common_ptr c)
{
	c->err->num_warnings = 0;
	c->err->msg_code = 0;
}

/* libjpeg's error handling without its jerror.c (which prints to stderr
   and calls exit(), neither of which the Atari's runtime has): no
   messages, and a failure jumps back to decode_jpeg */
static struct jpeg_error_mgr *lj_errors(struct jpeg_error_mgr *e)
{
	memset(e, 0, sizeof *e);
	e->error_exit = lj_exit;
	e->emit_message = lj_emit;
	e->output_message = lj_output;
	e->format_message = lj_format;
	e->reset_error_mgr = lj_reset;
	return e;
}

/* the file, in memory: cut short, it ends there (libjpeg makes the rest
   grey, as browsers show it) */
static void src_init(j_decompress_ptr c)
{
	(void) c;
}

static boolean src_fill(j_decompress_ptr c)
{
	static const JOCTET eoi[2] = { 0xFF, JPEG_EOI };
	c->src->next_input_byte = eoi;
	c->src->bytes_in_buffer = 2;
	return TRUE;
}

static void src_skip(j_decompress_ptr c, long n)
{
	if (n <= 0)
		return;
	if ((size_t) n > c->src->bytes_in_buffer) {
		src_fill(c);
		return;
	}
	c->src->next_input_byte += n;
	c->src->bytes_in_buffer -= (size_t) n;
}

static void src_term(j_decompress_ptr c)
{
	(void) c;
}

/* what a decode holds, kept off the stack that __builtin_longjmp leaves */
typedef struct {
	struct jpeg_decompress_struct ci;
	LJERR err;
	struct jpeg_source_mgr src;
	unsigned char *row, *rgba;
	int stopped;
} LJ;

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	LJ *j = calloc(1, sizeof *j);
	long y, x;
	int k, ok = 0;
	if (j == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	j->ci.err = lj_errors(&j->err.pub);
	if (__builtin_setjmp(j->err.jmp)) {
		/* libjpeg gave up: damaged, or out of memory */
		*why = j->stopped ? PIC_STOPPED : j->err.pub.msg_code == JERR_OUT_OF_MEMORY ? PIC_MEMORY :
		       (j->err.pub.msg_code == JERR_BAD_PRECISION || j->err.pub.msg_code == JERR_NOT_COMPILED) ?
		       PIC_UNSUPPORTED : PIC_DAMAGED;
		goto done;
	}
	jpeg_create_decompress(&j->ci);
	j->src.init_source = src_init;
	j->src.fill_input_buffer = src_fill;
	j->src.skip_input_data = src_skip;
	j->src.resync_to_restart = jpeg_resync_to_restart;
	j->src.term_source = src_term;
	j->src.next_input_byte = d;
	j->src.bytes_in_buffer = (size_t) n;
	j->ci.src = &j->src;
	jpeg_read_header(&j->ci, TRUE);
	/* CMYK (Adobe's, inverted) is made RGB below; the rest by libjpeg */
	j->ci.out_color_space = j->ci.jpeg_color_space == JCS_CMYK || j->ci.jpeg_color_space == JCS_YCCK ?
				JCS_CMYK : JCS_RGB;
	/* n/8 of the size: the least n still as big as shown */
	for (k = 1; k < 8; k++)
		if (((long) j->ci.image_width * k + 7) / 8 >= p->w && ((long) j->ci.image_height * k + 7) / 8 >= p->h)
			break;
	j->ci.scale_num = (unsigned) k;
	j->ci.scale_denom = 8;
	j->ci.dct_method = JDCT_ISLOW;
	jpeg_start_decompress(&j->ci);
	p->how = PIC_JPEG;
	p->scale = k;
	j->row = malloc((size_t) j->ci.output_width * (size_t) j->ci.output_components);
	j->rgba = malloc((size_t) j->ci.output_width * 4);
	if (j->row == NULL || j->rgba == NULL || !sc_init(sc, p, j->ci.output_width, j->ci.output_height)) {
		*why = PIC_MEMORY;
		jpeg_abort_decompress(&j->ci);
		goto done;
	}
	for (y = 0; y < (long) j->ci.output_height; y++) {
		JSAMPROW r = j->row;
		const unsigned char *s = j->row;
		unsigned char *o = j->rgba;
		jpeg_read_scanlines(&j->ci, &r, 1);
		if (j->ci.out_color_space == JCS_CMYK) {
			/* Adobe writes CMYK inverted: each component times K */
			for (x = 0; x < (long) j->ci.output_width; x++, s += 4, o += 4) {
				o[0] = (unsigned char) (s[0] * s[3] / 255);
				o[1] = (unsigned char) (s[1] * s[3] / 255);
				o[2] = (unsigned char) (s[2] * s[3] / 255);
				o[3] = 255;
			}
		} else {
			for (x = 0; x < (long) j->ci.output_width; x++, s += 3, o += 4) {
				o[0] = s[0];
				o[1] = s[1];
				o[2] = s[2];
				o[3] = 255;
			}
		}
		if (!sc_row(sc, j->rgba)) {
			*why = PIC_STOPPED;
			jpeg_abort_decompress(&j->ci);
			goto done;
		}
	}
	jpeg_finish_decompress(&j->ci);
	ok = 1;
done:
	jpeg_destroy_decompress(&j->ci);
	free(j->row);
	free(j->rgba);
	free(j);
	return ok;
}

#else

/* TJpgDec (third_party/tjpgd), for the family's programs on a 68000:
   small, and quick at 1/8 (no IDCT); the kinds it won't take from their
   blocks' averages (jpegdc.c) */

typedef struct {
	const unsigned char *p;
	long left;
	SCALER *sc;
	unsigned char *strip;		/* an MCU row of the unpacked picture, R G B A */
	long sw;
	int rows;			/* in the strip */
	int stopped;
} JPG;

static size_t jin(JDEC *jd, uint8_t *buf, size_t n)
{
	JPG *j = jd->device;
	if ((long) n > j->left)
		n = (size_t) j->left;
	if (buf != NULL)
		memcpy(buf, j->p, n);
	j->p += n;
	j->left -= (long) n;
	return n;
}

static int jout(JDEC *jd, void *bitmap, JRECT *r)
{
	JPG *j = jd->device;
	const unsigned char *px = bitmap;
	long x, y, w = r->right - r->left + 1;
	for (y = 0; y <= r->bottom - r->top; y++) {
		unsigned char *o = j->strip + (y * j->sw + r->left) * 4;
		for (x = 0; x < w; x++, px += 3, o += 4) {
			o[0] = px[0];
			o[1] = px[1];
			o[2] = px[2];
			o[3] = 255;
		}
	}
	j->rows = r->bottom - r->top + 1;
	if (r->right + 1 >= j->sw) {
		/* the row of blocks is whole */
		for (y = 0; y < j->rows; y++)
			if (!sc_row(j->sc, j->strip + y * j->sw * 4)) {
				j->stopped = 1;
				return 0;
			}
	}
	return 1;
}

/* the unpacked size at a scale, as TJpgDec makes it (each block row and
   column rounded down) */
static long scaled(long size, int mcu, int scale)
{
	long whole = size / mcu;
	return whole * (mcu >> scale) + ((size - whole * mcu) >> scale);
}

/* jpegdc's polls, through pic_poll: whether it stopped */
static int jdc_stopped;
static int jdc_pic_poll(void)
{
	if (pic_poll != NULL && !pic_poll()) {
		jdc_stopped = 1;
		return 0;
	}
	return 1;
}

#define POOL 11000L			/* TJpgDec's workspace with its Huffman tables */

static int decode_jpeg(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	JDEC jd;
	JPG j;
	void *pool = malloc(POOL);
	int scale = 0, rc;
	long sw, sh;
	memset(&j, 0, sizeof j);
	j.p = d;
	j.left = n;
	if (pool == NULL) {
		*why = PIC_MEMORY;
		return 0;
	}
	rc = jd_prepare(&jd, jin, pool, (size_t) POOL, &j);
	if (rc == JDR_OK) {
		/* as small as it can be unpacked and still be as big as shown */
		while (scale < 3 && (jd.width >> (scale + 1)) >= p->w && (jd.height >> (scale + 1)) >= p->h)
			scale++;
		sw = scaled(jd.width, jd.msx * 8, scale);
		sh = scaled(jd.height, jd.msy * 8, scale);
		j.sw = sw;
		j.strip = malloc((size_t) sw * 16 * 4);
		if (j.strip == NULL || !sc_init(sc, p, sw, sh)) {
			free(j.strip);
			free(pool);
			*why = PIC_MEMORY;
			return 0;
		}
		j.sc = sc;
		p->how = PIC_JPEG;
		p->scale = scale;
		rc = jd_decomp(&jd, jout, (uint8_t) scale);
		free(j.strip);
		free(pool);
		if (rc == JDR_OK || (rc == JDR_INP && sc->y > 0)) {
			/* a picture cut short: the rest stays clear */
			unsigned char *row = sc->y < sh ? calloc((size_t) sw, 4) : NULL;
			if (row != NULL) {
				while (sc->y < sh)
					if (!sc_row(sc, row))
						break;
				free(row);
			}
			return 1;
		}
		if (j.stopped) {
			*why = PIC_STOPPED;
			return 0;
		}
		sc_free(sc);
		memset(sc, 0, sizeof *sc);
		/* damaged as TJpgDec sees it: the averages may still be read */
	} else {
		free(pool);
	}
	{
		/* progressive, SOF1, other samplings, CMYK: each 8x8 block's
		   average, an eighth of the size */
		int jw, jh, jwhy;
		int (*was)(void) = jdc_poll;
		long bw, bh, bx, by;
		unsigned char *row;
		JDC *c = jdc_open(d, n, &jw, &jh, &jwhy);
		if (c == NULL) {
			*why = jwhy == JW_MEMORY ? PIC_MEMORY : jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_stopped = 0;
		jdc_poll = jdc_pic_poll;
		if (!jdc_read(c)) {
			jdc_poll = was;
			jwhy = jdc_why(c);
			jdc_free(c);
			*why = jdc_stopped ? PIC_STOPPED : jwhy == JW_MEMORY ? PIC_MEMORY :
			       jwhy == JW_UNSUPPORTED ? PIC_UNSUPPORTED : PIC_DAMAGED;
			return 0;
		}
		jdc_poll = was;
		bw = (jw + 7) / 8;
		bh = (jh + 7) / 8;
		row = malloc((size_t) bw * 4);
		if (row == NULL || !sc_init(sc, p, bw, bh)) {
			free(row);
			jdc_free(c);
			*why = PIC_MEMORY;
			return 0;
		}
		p->how = PIC_JPEG_AVG;
		p->scale = 3;
		for (by = 0; by < bh; by++) {
			for (bx = 0; bx < bw; bx++) {
				int r, g, b;
				jdc_rgb(c, (int) bx, (int) by, &r, &g, &b);
				row[bx * 4] = (unsigned char) r;
				row[bx * 4 + 1] = (unsigned char) g;
				row[bx * 4 + 2] = (unsigned char) b;
				row[bx * 4 + 3] = 255;
			}
			if (!sc_row(sc, row)) {
				free(row);
				jdc_free(c);
				*why = PIC_STOPPED;
				return 0;
			}
		}
		free(row);
		jdc_free(c);
		return 1;
	}
}

#endif

/* ---------------------------------------------------------------- PNG */

static int png_row(void *ud, long y, const unsigned char *rgba)
{
	(void) y;
	return sc_row(ud, rgba);
}

static int decode_png(const unsigned char *d, long n, PIC *p, SCALER *sc, long max_px, int *why)
{
	long w, h;
	int r;
	if (!png_size(d, n, &w, &h) || w <= 0 || h <= 0) {
		*why = PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, w, h)) {
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_PNG;
	r = png_decode(d, n, max_px, png_row, sc);
	switch (r) {
	case PNG_OK: return 1;
	case PNG_MEMORY: *why = PIC_MEMORY; break;
	case PNG_TOO_BIG: *why = PIC_TOO_BIG; break;
	case PNG_STOPPED: *why = PIC_STOPPED; break;
	case PNG_UNSUPPORTED: *why = PIC_UNSUPPORTED; break;
	default: *why = PIC_DAMAGED;
	}
	return 0;
}

/* ---------------------------------------------------------------- GIF */

typedef struct {
	long w, h;
	unsigned char px[1];		/* w * h * 4 */
} GBMP;

static nsgif_bitmap_t *gb_create(int w, int h)
{
	GBMP *b = calloc(1, sizeof *b + (size_t) w * (size_t) h * 4);
	if (b != NULL) {
		b->w = w;
		b->h = h;
	}
	return b;
}

static void gb_destroy(nsgif_bitmap_t *b)
{
	free(b);
}

static uint8_t *gb_buffer(nsgif_bitmap_t *b)
{
	/* nsgif wants it on a 4-byte boundary: px follows two longs */
	return ((GBMP *) b)->px;
}

static int decode_gif(const unsigned char *d, long n, PIC *p, SCALER *sc, int *why)
{
	static const nsgif_bitmap_cb_vt vt = { gb_create, gb_destroy, gb_buffer, NULL, NULL, NULL, NULL };
	nsgif_t *g;
	nsgif_bitmap_t *bmp;
	const nsgif_info_t *info;
	nsgif_error e;
	long y;
	if (nsgif_create(&vt, NSGIF_BITMAP_FMT_R8G8B8A8, &g) != NSGIF_OK) {
		*why = PIC_MEMORY;
		return 0;
	}
	e = nsgif_data_scan(g, (size_t) n, d);
	nsgif_data_complete(g);
	info = nsgif_get_info(g);
	if (info == NULL || info->frame_count == 0) {
		nsgif_destroy(g);
		*why = e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	e = nsgif_frame_decode(g, 0, &bmp);
	if (e != NSGIF_OK || bmp == NULL) {
		nsgif_destroy(g);
		*why = e == NSGIF_ERR_OOM ? PIC_MEMORY : PIC_DAMAGED;
		return 0;
	}
	if (!sc_init(sc, p, ((GBMP *) bmp)->w, ((GBMP *) bmp)->h)) {
		nsgif_destroy(g);
		*why = PIC_MEMORY;
		return 0;
	}
	p->how = PIC_GIF;
	for (y = 0; y < ((GBMP *) bmp)->h; y++)
		if (!sc_row(sc, ((GBMP *) bmp)->px + y * ((GBMP *) bmp)->w * 4)) {
			nsgif_destroy(g);
			*why = PIC_STOPPED;
			return 0;
		}
	nsgif_destroy(g);
	return 1;
}

/* ---------------------------------------------------------------- decoding */

void pic_free(PIC *p)
{
	if (p == NULL)
		return;
	free(p->px);
	free(p->alpha);
	free(p);
}

PIC *pic_decode(const unsigned char *d, long n, int w, int h, long max_px, int *why)
{
	PIC *p;
	SCALER sc;
	long sw, sh;
	int kind = pic_kind(d, n), ok = 0, dummy;
	if (why == NULL)
		why = &dummy;
	*why = PIC_OK;
	if (kind != PIC_JPEG && kind != PIC_PNG && kind != PIC_GIF) {
		*why = PIC_UNSUPPORTED;
		return NULL;
	}
	if (!pic_size(d, n, &sw, &sh)) {
		*why = PIC_DAMAGED;
		return NULL;
	}
	if (sw > max_px / sh) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	if (w <= 0 || h <= 0) {
		w = (int) sw;
		h = (int) sh;
	}
	if (w > 0x7fff || h > 0x7fff || (long) w * h > max_px) {
		*why = PIC_TOO_BIG;
		return NULL;
	}
	p = calloc(1, sizeof *p);
	if (p == NULL) {
		*why = PIC_MEMORY;
		return NULL;
	}
	p->w = w;
	p->h = h;
	p->px = malloc((size_t) w * h * 2);
	p->alpha = malloc((size_t) w * h);
	memset(&sc, 0, sizeof sc);
	if (p->px == NULL || p->alpha == NULL) {
		*why = PIC_MEMORY;
	} else {
		memset(p->alpha, 0, (size_t) w * h);
		memset(p->px, 0, (size_t) w * h * 2);
		switch (kind) {
		case PIC_JPEG: ok = decode_jpeg(d, n, p, &sc, why); break;
		case PIC_PNG: ok = decode_png(d, n, p, &sc, max_px, why); break;
		default: ok = decode_gif(d, n, p, &sc, why); break;
		}
	}
	if (ok && sc.down_y && sc.y < sc.sh && sc.dy >= 0) {
		/* fewer rows came than the header said: the last one made */
		flush(&sc, sc.dy);
	}
	if (ok && !sc.any_alpha) {
		free(p->alpha);
		p->alpha = NULL;
	}
	sc_free(&sc);
	if (!ok) {
		pic_free(p);
		return NULL;
	}
	return p;
}

/* ---------------------------------------------------------------- the screen's colours */

void pic_pal_init(PIC_PAL *pal, int n, const unsigned char rgb[][3])
{
	int i, j;
	pal->n = n < 2 ? 2 : n > 256 ? 256 : n;
	memcpy(pal->rgb, rgb, (size_t) pal->n * 3);
	memset(pal->near, 0, sizeof pal->near);
	/* the pens by their red, for nearest_far */
	for (i = 0; i < pal->n; i++) {
		for (j = i; j > 0 && pal->rgb[pal->order[j - 1]][0] > pal->rgb[i][0]; j--)
			pal->order[j] = pal->order[j - 1];
		pal->order[j] = (unsigned char) i;
	}
}


/* a colour (each component -128..383) to its index in near[] */
#define NEAR_AT(r, g, b) ((((r) + 128) >> 4) * 1024 + (((g) + 128) >> 4) * 32 + (((b) + 128) >> 4))

static int nearest_far(PIC_PAL *pal, int r, int g, int b)
{
	int k = NEAR_AT(r, g, b), lo, hi, best = 0;
	long bd = 0x7fffffffL;
	if (pal->near[k])
		return pal->near[k] - 1;
	/* the step's middle */
	r = (r & ~15) + 8;
	g = (g & ~15) + 8;
	b = (b & ~15) + 8;
	/* outwards from the pens of about this red (they are sorted by it),
	   until red alone puts a pen farther than the best: a few pens of
	   256 instead of all (the 256-colour modes spent most of a picture's
	   time here) */
	lo = 0;
	hi = pal->n;
	while (lo < hi) {
		int mid = (lo + hi) >> 1;
		if (pal->rgb[pal->order[mid]][0] < r)
			lo = mid + 1;
		else
			hi = mid;
	}
	hi = lo;
	lo = lo - 1;
	while (lo >= 0 || hi < pal->n) {
		int i, side;
		long dr, dd;
		if (hi < pal->n && (lo < 0 || pal->rgb[pal->order[hi]][0] - r <= r - pal->rgb[pal->order[lo]][0]))
			i = pal->order[hi++], side = 1;
		else
			i = pal->order[lo--], side = 0;
		dr = r - pal->rgb[i][0];
		if (3 * dr * dr >= bd) {
			/* every pen past this one on its side is farther still */
			if (side)
				hi = pal->n;
			else
				lo = -1;
			continue;
		}
		dd = 3 * dr * dr + 4L * (g - pal->rgb[i][1]) * (g - pal->rgb[i][1]) +
		     2L * (b - pal->rgb[i][2]) * (b - pal->rgb[i][2]);
		if (dd < bd) {
			bd = dd;
			best = i;
		}
	}
	pal->near[k] = (unsigned char) (best + 1);
	return best;
}

int pic_pal_nearest(PIC_PAL *pal, int r, int g, int b)
{
	return nearest_far(pal, r, g, b);
}

void pic_dither(const PIC *p, PIC_PAL *pal, unsigned char *out)
{
	/* Floyd-Steinberg: each pixel's error (what it wanted less its pen)
	   carried 7/16 on, and 3/16, 5/16 and 1/16 to the row below,
	   serpentine. The colour wanted may lie past the cube's edge (light
	   blue on 16 colours wants more blue than there is): its pen is the
	   nearest to that, so the error is paid back by pens that lie that
	   way instead of being lost to a clamp (the colour came out grey) */
	long w = p->w, y;
	short *rows = calloc((size_t) (w + 2) * 6, sizeof *rows), *cur, *nxt;
	if (rows == NULL) {
		long i;
		for (i = 0; i < (long) p->h * w; i++) {
			unsigned v = p->px[i];
			out[i] = (unsigned char) nearest_far(pal, (v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8);
		}
		return;
	}
	cur = rows;
	nxt = rows + (w + 2) * 3;
	for (y = 0; y < p->h; y++) {
		const unsigned short *px = p->px + y * w;
		const unsigned char *al = p->alpha != NULL ? p->alpha + y * w : NULL;
		unsigned char *o = out + y * w;
		int dir = (y & 1) ? -1 : 1, step = 3 * dir;
		long x = dir > 0 ? 0 : w - 1, n;
		short *c = cur + (x + 1) * 3, *d = nxt + (x + 1) * 3;
		int er = 0, eg = 0, eb = 0;	/* 7/16 of the last pixel's, in 16ths */
		for (n = 0; n < w; n++, x += dir, c += step, d += step) {
			unsigned v = px[x];
			int r, g, b, k, qr, qg, qb;
			if (al != NULL && al[x] < 128) {
				o[x] = 0;
				er = eg = eb = 0;
				continue;
			}
			r = (int) ((v >> 8) & 0xF8) + ((er + c[0]) >> 4);
			g = (int) ((v >> 3) & 0xFC) + ((eg + c[1]) >> 4);
			b = (int) ((v << 3) & 0xF8) + ((eb + c[2]) >> 4);
			r = r < -128 ? -128 : r > 383 ? 383 : r;
			g = g < -128 ? -128 : g > 383 ? 383 : g;
			b = b < -128 ? -128 : b > 383 ? 383 : b;
			k = pal->near[NEAR_AT(r, g, b)];
			k = k ? k - 1 : nearest_far(pal, r, g, b);
			o[x] = (unsigned char) k;
			qr = r - pal->rgb[k][0];
			qg = g - pal->rgb[k][1];
			qb = b - pal->rgb[k][2];
			er = qr * 7;
			eg = qg * 7;
			eb = qb * 7;
			d[-step] += (short) (qr * 3);
			d[-step + 1] += (short) (qg * 3);
			d[-step + 2] += (short) (qb * 3);
			d[0] += (short) (qr * 5);
			d[1] += (short) (qg * 5);
			d[2] += (short) (qb * 5);
			d[step] = (short) qr;
			d[step + 1] = (short) qg;
			d[step + 2] = (short) qb;
		}
		{
			short *t = cur;
			cur = nxt;
			nxt = t;
			memset(nxt, 0, (size_t) (w + 2) * 6);
		}
	}
	free(rows);
}
