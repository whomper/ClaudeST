/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * PICSTEPS.PRG: PIC.OVL's two long stretches (atari/pic_ovl.c) timed a
 * call at a time, for run.py. C:\B\pic.ovl is the one before (a check
 * and a photo's planes in one go), C:\A\pic.ovl the one after (1.2: in
 * steps). For each: pic_ovl_step until it answers, each call's time on
 * the 200 Hz clock, no near.dat (its worst); then, on 8 planes, a 65 x 65
 * photo drawn by pic_ovl_chunky8 with a new key (its copies made in the
 * drawing call), and with the after one the same made beforehand by
 * pic_ovl_chunky8_prepare (each call's time, then the draw's), and once
 * after a single step; the screen read back each time and compared with
 * the first, byte for byte. Lines go to the emulator's console as "PST
 * ...", the last "PST done".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tos.h"
#include "gem.h"
#include "plat.h"
#include "conn.h"
#include "pic.h"
#include "pic_ovl.h"
#include "util.h"

long nf_on = 1;		/* a test program: NatFeats always (nf.S's is 0) */
char tls_ovl_path[200];

/* ---- plat.h on GEMDOS (ovl.c's reads; near.dat isn't asked for) ---- */

int pf_open(const char *path, int mode)
{
	long h = mode == PF_READ ? Fopen(path, 0) : Fcreate(path, 0);
	return h < 0 ? -1 : (int)h;
}
long pf_read(int h, void *buf, long n) { return Fread(h, n, buf); }
long pf_write(int h, const void *buf, long n) { return Fwrite(h, n, buf); }
void pf_close(int h) { Fclose(h); }
long pf_seek(int h, long off) { return Fseek(off, h, 0); }
long pf_size(const char *path) { (void)path; return -1; }
int pf_remove(const char *path) { (void)path; return -1; }
char *pf_load(const char *path, long *len) { (void)path; *len = 0; return 0; }
int pf_save(const char *path, const void *data, long len) { (void)path; (void)data; (void)len; return 0; }
void pf_now(PFTIME *t) { memset(t, 0, sizeof(*t)); }
unsigned long pf_ms(void) { return tos_ms(); }
void pf_entropy(unsigned char *buf, int n) { memset(buf, 0, (size_t)n); }
long net_write(int h, const void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long net_read(int h, void *buf, long max) { (void)h; (void)buf; (void)max; return -1; }
void (*pf_idle)(void);

static char line[200];

static void say(const char *s)
{
	nf_debug(s);
	nf_debug("\n");
}

void conn_log(const char *who, const char *dir, const char *text, long n)
{
	snprintf(line, sizeof(line), "PST log %s%s%.*s", who, dir, (int)n, text);
	say(line);
}

static short vdi, planes, scr_w, scr_h;
static PIC_PAL pal;
static MFDB scr, mem;
static unsigned char *grab_a, *grab_b, pix[65 * 65];
static long gbytes;

#define PX 64
#define PY 64

/* the screen's pens as pic_pal_init makes them (the near table cleared) */
static void palette(void)
{
	int i, j, n = planes >= 8 ? 256 : 1 << planes;
	short rgb[3];
	pal.n = n;
	for (i = 0; i < n; i++) {
		vq_color(vdi, (short)i, rgb);
		for (j = 0; j < 3; j++)
			pal.rgb[i][j] = (unsigned char)(((long)rgb[j] * 255 + 500) / 1000);
	}
	if (n > 4) {
		if (!pal.near)
			pal.near = calloc(32 * 32 * 32, 1);
		else
			memset(pal.near, 0, 32 * 32 * 32);
	}
	for (i = 0; i < n; i++) {
		for (j = i; j > 0 && pal.rgb[pal.order[j - 1]][0] > pal.rgb[i][0]; j--)
			pal.order[j] = pal.order[j - 1];
		pal.order[j] = (unsigned char)i;
	}
}

/* pic_ovl_step to its answer: each call timed */
static void check(const char *which)
{
	unsigned long t, most = 0, all = 0;
	int r, n = 0;
	char b[120];
	b[0] = 0;
	do {
		t = tos_ms();
		r = pic_ovl_step(&pal, "", 0x1234);
		t = tos_ms() - t;
		all += t;
		if (t > most)
			most = t;
		if (n < 24 && strlen(b) < sizeof(b) - 8)
			snprintf(b + strlen(b), sizeof(b) - strlen(b), " %lu", t);
		n++;
	} while (r < 0 && n < 200);
	snprintf(line, sizeof(line), "PST %s check: %d (%d calls, longest %lu ms, all %lu ms):%s", which, r, n,
		 most, all, b);
	say(line);
}

/* the ground under the photo: a pattern of two pens */
static void ground(void)
{
	short p[4], c[4] = { 0, 0, 0, 0 };
	p[0] = PX - 8;
	p[1] = PY - 8;
	p[2] = PX + 65 + 7;
	p[3] = PY + 65 + 7;
	vs_clip(vdi, 0, c);
	vswr_mode(vdi, 1);
	vsf_interior(vdi, 2);
	vsf_style(vdi, 4);
	vsf_color(vdi, 3);
	vsf_perimeter(vdi, 0);
	vr_recfl(vdi, p);
}

static void grab(unsigned char *to)
{
	short p[8];
	p[0] = PX - 8;
	p[1] = PY - 8;
	p[2] = PX + 65 + 7;
	p[3] = PY + 65 + 7;
	p[4] = p[5] = 0;
	p[6] = 65 + 15;
	p[7] = 65 + 15;
	mem.fd_w = 96;
	mem.fd_h = 81;
	mem.fd_wdwidth = 6;
	mem.fd_nplanes = planes;
	mem.fd_addr = to;
	memset(to, 0x55, gbytes);
	vro_cpyfm(vdi, 3, p, &scr, &mem);
}

/* the photo drawn with a new key, its copies made in the call */
static void draw_whole(const char *which, long key, unsigned char *to)
{
	unsigned long t;
	int r;
	ground();
	t = tos_ms();
	r = pic_ovl_chunky8(pix, 65, 65, key, vdi, 0, 0, 65, 65, PX, PY);
	t = tos_ms() - t;
	grab(to);
	snprintf(line, sizeof(line), "PST %s chunky8 new key: %d in %lu ms", which, r, t);
	say(line);
}

/* made beforehand (most steps, -1 all), then drawn */
static void draw_prepared(const char *which, long key, int most_steps, unsigned char *to)
{
	unsigned long t, most = 0;
	int r = -1, n = 0;
	char b[120];
	b[0] = 0;
	ground();
	while (r < 0 && (most_steps < 0 || n < most_steps) && n < 200) {
		t = tos_ms();
		r = pic_ovl_chunky8_prepare(pix, 65, 65, key, vdi);
		t = tos_ms() - t;
		if (t > most)
			most = t;
		if (strlen(b) < sizeof(b) - 8)
			snprintf(b + strlen(b), sizeof(b) - strlen(b), " %lu", t);
		n++;
	}
	t = tos_ms();
	pic_ovl_chunky8(pix, 65, 65, key, vdi, 0, 0, 65, 65, PX, PY);
	t = tos_ms() - t;
	grab(to);
	snprintf(line, sizeof(line), "PST %s chunky8_prepare: %d after %d calls (longest %lu ms:%s), the draw %lu ms",
		 which, r, n, most, b, t);
	say(line);
}

static void same(const char *what)
{
	snprintf(line, sizeof(line), "PST %s: %s", what, memcmp(grab_a, grab_b, gbytes) ? "DIFFER" : "the same");
	say(line);
}

int main(void)
{
	short wo[57], d, x, y;
	appl_init();
	vdi = v_opnvwk_(graf_handle(&d, &d, &d, &d), wo);
	scr_w = wo[0] + 1;
	scr_h = wo[1] + 1;
	vq_extnd(vdi, 1, wo);
	planes = wo[4];
	memset(&scr, 0, sizeof(scr));
	memset(&mem, 0, sizeof(mem));
	gbytes = 96L / 8 * 81 * planes;
	grab_a = malloc(gbytes);
	grab_b = malloc(gbytes);
	/* a round photo: pens 1-254 across it, 255 outside (not drawn) */
	for (y = 0; y < 65; y++)
		for (x = 0; x < 65; x++) {
			int dx = x - 32, dy = y - 32;
			pix[y * 65 + x] = dx * dx + dy * dy > 32 * 32 ? 255 : (unsigned char)(1 + (x * 7 + y * 3) % 254);
		}
	graf_mouse(256, 0);
	wind_update(1);
	snprintf(line, sizeof(line), "PST screen %dx%d, %d planes", scr_w, scr_h, planes);
	say(line);

	/* before: C:\B\pic.ovl */
	str_copy(tls_ovl_path, "C:\\B\\TLS.OVL", sizeof(tls_ovl_path));
	palette();
	check("before");
	if (planes == 8) {
		draw_whole("before", 1, grab_a);
		draw_whole("before again", 1, grab_b);
		same("before, its key again");
	}
	pic_ovl_unload();

	/* after: C:\A\pic.ovl */
	str_copy(tls_ovl_path, "C:\\A\\TLS.OVL", sizeof(tls_ovl_path));
	palette();
	check("after");
	if (planes == 8) {
		draw_whole("after", 1, grab_b);
		same("after's draw against before's");
		draw_prepared("after", 2, -1, grab_b);
		same("made beforehand against before's");
		draw_prepared("after one step", 3, 1, grab_b);
		same("one step made beforehand against before's");
	}
	pic_ovl_unload();
	wind_update(0);
	graf_mouse(257, 0);
	v_clsvwk(vdi);
	appl_exit();
	say("PST done");
	return 0;
}
