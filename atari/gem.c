/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gem.c - minimal AES/VDI bindings.
 * From Claude ST (whomper/ClaudeST), extended for EMail's dialogs, ECal's
 * accessory and iconify, EContact's clipboard and EFTP's VDI cache.
 */
#include "tos.h"
#include "gem.h"

/* ---------------- AES ---------------- */

static short control[5];
static short global[15];
static short aintin[16];
static short aintout[7];
static void *addrin[3];
static void *addrout[1];

static void *aespb[6] = { control, global, aintin, aintout, addrin, addrout };

static short aes_raw(short op, short nin, short nout, short nain, short naout)
{
	control[0] = op;
	control[1] = nin;
	control[2] = nout;
	control[3] = nain;
	control[4] = naout;
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #200,%%d0\n\ttrap #2"
		: : "r"((long)aespb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return aintout[0];
}

#ifdef AES_PROFILE
/* Built with -DAES_PROFILE (or -DAES_PROFILE=ms): how long the program
   holds the machine between its event calls (evnt_*): the time the
   program in front, or for an accessory every other program, can't run
   (guideline 2.2). Each stretch of AES_PROFILE ms or more (50 when no
   number is given) goes to the emulator's console (NatFeats) as
   "AESGAP <ms> ms <tag>"; tests/aesgap.py reads them.
   Under plain TOS the AES hands the processor to the other programs in
   calls that do no work of their own (appl_write, appl_find, graf_mkstate,
   wind_update: EContact saw ECal's whole turn of 160 ms run inside its
   graf_mkstate, 21 Oct 2026). The time inside those calls, 5 ms or more a
   call, isn't ours: it is left out of the stretch and, when it reaches
   AES_PROFILE, goes out as "AESOTHER <ms> ms <tag>". */
#include <stdio.h>
#if AES_PROFILE < 2
#undef AES_PROFILE
#define AES_PROFILE 50
#endif
static const char *gap_tag = "start";
static u32 gap_from, gap_max, other;

void aes_profile_tag(const char *tag)
{
	gap_tag = tag;
}

unsigned long aes_profile_max(void)
{
	return gap_max;
}

static short aes(short op, short nin, short nout, short nain, short naout)
{
	short r;
	int ev = op >= 20 && op <= 25;		/* evnt_keybd ... evnt_multi */
	int idle = op == 12 || op == 13 || op == 79 || op == 107;	/* no work of ours */
	u32 t0 = 0;
	if (ev && gap_from) {
		u32 g = tos_ms() - gap_from;
		char b[96];
		g = g > other ? g - other : 0;
		if (g > gap_max)
			gap_max = g;
		if (g >= AES_PROFILE) {
			snprintf(b, sizeof(b), "AESGAP %lu ms %s\n", (unsigned long)g, gap_tag);
			nf_debug(b);
		}
		if (other >= AES_PROFILE) {
			snprintf(b, sizeof(b), "AESOTHER %lu ms %s\n", (unsigned long)other, gap_tag);
			nf_debug(b);
		}
	}
	if (idle && gap_from)
		t0 = tos_ms();
	r = aes_raw(op, nin, nout, nain, naout);
	if (t0) {
		u32 d = tos_ms() - t0;
		if (d >= 5)
			other += d;
	}
	if (ev) {
		gap_from = tos_ms();
		other = 0;
	}
	return r;
}
#else
#define aes aes_raw
#endif

short appl_init(void) { return aes(10, 0, 1, 0, 0); }
short appl_exit(void) { return aes(19, 0, 1, 0, 0); }

short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
	short r = aes(77, 0, 5, 0, 0);
	*wchar = aintout[1];
	*hchar = aintout[2];
	*wbox = aintout[3];
	*hbox = aintout[4];
	return r;
}

short graf_mouse(short num, void *form)
{
	aintin[0] = num;
	addrin[0] = form;
	return aes(78, 1, 1, 1, 0);
}

short graf_mkstate(short *mx, short *my, short *mb, short *ks)
{
	short r = aes(79, 0, 5, 0, 0);
	*mx = aintout[1];
	*my = aintout[2];
	*mb = aintout[3];
	*ks = aintout[4];
	return r;
}

/* drag a rubber box from x,y; its size when the button is released */
short graf_rubberbox(short x, short y, short minw, short minh, short *w, short *h)
{
	short r;
	aintin[0] = x;
	aintin[1] = y;
	aintin[2] = minw;
	aintin[3] = minh;
	r = aes(70, 4, 3, 0, 0);
	*w = aintout[1];
	*h = aintout[2];
	return r;
}

/* rectangle for MU_M1: leave=0 waits for entering it, 1 for leaving */
static short m1[5];

void evnt_set_m1(short leave, short x, short y, short w, short h)
{
	m1[0] = leave;
	m1[1] = x;
	m1[2] = y;
	m1[3] = w;
	m1[4] = h;
}

short menu_bar(OBJECT *tree, short show)
{
	aintin[0] = show;
	addrin[0] = tree;
	return aes(30, 1, 1, 1, 0);
}

short menu_tnormal(OBJECT *tree, short title, short normal)
{
	aintin[0] = title;
	aintin[1] = normal;
	addrin[0] = tree;
	return aes(33, 2, 1, 1, 0);
}

short menu_register(short apid, const char *name)
{
	aintin[0] = apid;
	addrin[0] = (void *)name;
	return aes(35, 1, 1, 1, 0);
}

short rsrc_obfix(OBJECT *tree, short obj)
{
	aintin[0] = obj;
	addrin[0] = tree;
	return aes(114, 1, 1, 1, 0);
}

short wind_find(short x, short y)
{
	aintin[0] = x;
	aintin[1] = y;
	return aes(106, 2, 1, 0, 0);
}

short appl_find(const char *name)
{
	addrin[0] = (void *)name;
	return aes(13, 0, 1, 1, 0);
}

/* the AES's programs one at a time (AES 4.0, MagiC: appl_getinfo(4)'s
   third answer says so): mode 0 the first, 1 the next; 0 when no more */
short appl_search(short mode, char *name, short *type, short *id)
{
	short r;
	aintin[0] = mode;
	addrin[0] = name;
	r = aes(18, 1, 3, 1, 0);
	*type = aintout[1];
	*id = aintout[2];
	return r;
}

short appl_write(short id, short len, const void *buf)
{
	aintin[0] = id;
	aintin[1] = len;
	addrin[0] = (void *)buf;
	return aes(12, 2, 1, 1, 0);
}

short appl_id(void)
{
	return global[2];
}

short form_do(OBJECT *tree, short start)
{
	aintin[0] = start;
	addrin[0] = tree;
	return aes(50, 1, 1, 1, 0);
}

short form_keybd(OBJECT *tree, short obj, short next, short ch, short *onext, short *och)
{
	short r;
	aintin[0] = obj;
	aintin[1] = ch;
	aintin[2] = next;
	addrin[0] = tree;
	r = aes(55, 3, 3, 1, 0);
	*onext = aintout[1];
	*och = aintout[2];
	return r;
}

short form_button(OBJECT *tree, short obj, short clicks, short *onext)
{
	short r;
	aintin[0] = obj;
	aintin[1] = clicks;
	addrin[0] = tree;
	r = aes(56, 2, 2, 1, 0);
	*onext = aintout[1];
	return r;
}

short objc_edit(OBJECT *tree, short obj, short ch, short *idx, short kind)
{
	short r;
	aintin[0] = obj;
	aintin[1] = ch;
	aintin[2] = *idx;
	aintin[3] = kind;
	addrin[0] = tree;
	r = aes(46, 4, 2, 1, 0);
	*idx = aintout[1];
	return r;
}

short form_center(OBJECT *tree, short *x, short *y, short *w, short *h)
{
	short r;
	addrin[0] = tree;
	r = aes(54, 0, 5, 1, 0);
	*x = aintout[1];
	*y = aintout[2];
	*w = aintout[3];
	*h = aintout[4];
	return r;
}

short objc_find(OBJECT *tree, short start, short depth, short x, short y)
{
	aintin[0] = start;
	aintin[1] = depth;
	aintin[2] = x;
	aintin[3] = y;
	addrin[0] = tree;
	return aes(43, 4, 1, 1, 0);
}

short objc_offset(OBJECT *tree, short obj, short *x, short *y)
{
	short r;
	aintin[0] = obj;
	addrin[0] = tree;
	r = aes(44, 1, 3, 1, 0);
	*x = aintout[1];
	*y = aintout[2];
	return r;
}

short menu_icheck(OBJECT *tree, short item, short check)
{
	aintin[0] = item;
	aintin[1] = check;
	addrin[0] = tree;
	return aes(31, 2, 1, 1, 0);
}

short menu_ienable(OBJECT *tree, short item, short enable)
{
	aintin[0] = item;
	aintin[1] = enable;
	addrin[0] = tree;
	return aes(32, 2, 1, 1, 0);
}

short evnt_timer_(unsigned long ms)
{
	aintin[0] = (short)(ms & 0xffff);
	aintin[1] = (short)(ms >> 16);
	return aes(24, 2, 1, 0, 0);
}

short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4)
{
	short r;
	aintin[0] = type;
	r = aes(130, 1, 5, 0, 0);
	*o1 = aintout[1];
	*o2 = aintout[2];
	*o3 = aintout[3];
	*o4 = aintout[4];
	return r;
}

/* the clipboard: its folder (SCRAP.TXT goes in it) */
short scrp_read(char *path)
{
	addrin[0] = path;
	return aes(80, 0, 1, 1, 0);
}

short scrp_write(const char *path)
{
	addrin[0] = (void *)path;
	return aes(81, 0, 1, 1, 0);
}

short shel_write(short mode, short wisgr, short wiscr, const void *cmd, const void *tail)
{
	aintin[0] = mode;
	aintin[1] = wisgr;
	aintin[2] = wiscr;
	addrin[0] = (void *)cmd;
	addrin[1] = (void *)tail;
	return aes(121, 3, 1, 2, 0);
}

short aes_version(void)
{
	return global[0];
}

/* the GEM file selector; the titled version needs AES 1.4 (TOS 1.04) */
short fsel_exinput(char *path, char *name, short *button, const char *title)
{
	short r;
	addrin[0] = path;
	addrin[1] = name;
	if (aes_version() >= 0x0104) {
		addrin[2] = (void *)title;
		r = aes(91, 0, 2, 3, 0);
	} else {
		r = aes(90, 0, 2, 2, 0);
	}
	*button = aintout[1];
	return r;
}

short rsrc_load(const char *name)
{
	addrin[0] = (void *)name;
	return aes(110, 0, 1, 1, 0);
}

OBJECT *rsrc_tree(short index)
{
	aintin[0] = 0;		/* R_TREE */
	aintin[1] = index;
	aes(112, 2, 1, 0, 1);
	return (OBJECT *)addrout[0];
}

short objc_draw(OBJECT *tree, short start, short depth, short x, short y, short w, short h)
{
	aintin[0] = start;
	aintin[1] = depth;
	aintin[2] = x;
	aintin[3] = y;
	aintin[4] = w;
	aintin[5] = h;
	addrin[0] = tree;
	return aes(42, 6, 1, 1, 0);
}

short form_alert(short def, const char *str)
{
	aintin[0] = def;
	addrin[0] = (void *)str;
	return aes(52, 1, 1, 1, 0);
}

short form_dial(short flag, short x, short y, short w, short h)
{
	short i;
	aintin[0] = flag;
	for (i = 0; i < 2; i++) {
		aintin[1 + i * 4] = x;
		aintin[2 + i * 4] = y;
		aintin[3 + i * 4] = w;
		aintin[4 + i * 4] = h;
	}
	return aes(51, 9, 1, 0, 0);
}

short wind_create(short kind, short x, short y, short w, short h)
{
	aintin[0] = kind;
	aintin[1] = x;
	aintin[2] = y;
	aintin[3] = w;
	aintin[4] = h;
	return aes(100, 5, 1, 0, 0);
}

short wind_open(short h, short x, short y, short w, short ht)
{
	aintin[0] = h;
	aintin[1] = x;
	aintin[2] = y;
	aintin[3] = w;
	aintin[4] = ht;
	return aes(101, 5, 1, 0, 0);
}

short wind_close(short h)
{
	aintin[0] = h;
	return aes(102, 1, 1, 0, 0);
}

short wind_delete(short h)
{
	aintin[0] = h;
	return aes(103, 1, 1, 0, 0);
}

short wind_get(short h, short field, short *a, short *b, short *c, short *d)
{
	short r;
	aintin[0] = h;
	aintin[1] = field;
	r = aes(104, 2, 5, 0, 0);
	*a = aintout[1];
	*b = aintout[2];
	*c = aintout[3];
	*d = aintout[4];
	return r;
}

short wind_set(short h, short field, short a, short b, short c, short d)
{
	aintin[0] = h;
	aintin[1] = field;
	aintin[2] = a;
	aintin[3] = b;
	aintin[4] = c;
	aintin[5] = d;
	return aes(105, 6, 1, 0, 0);
}

/* the AES keeps the pointer, so s must stay valid while the window lives */
short wind_set_str(short h, short field, const char *s)
{
	u32 p = (u32)s;
	return wind_set(h, field, (short)(p >> 16), (short)(p & 0xffff), 0, 0);
}

short wind_update(short mode)
{
	aintin[0] = mode;
	return aes(107, 1, 1, 0, 0);
}

short wind_calc(short type, short kind, short x, short y, short w, short h,
		short *ox, short *oy, short *ow, short *oh)
{
	short r;
	aintin[0] = type;
	aintin[1] = kind;
	aintin[2] = x;
	aintin[3] = y;
	aintin[4] = w;
	aintin[5] = h;
	r = aes(108, 6, 5, 0, 0);
	*ox = aintout[1];
	*oy = aintout[2];
	*ow = aintout[3];
	*oh = aintout[4];
	return r;
}

short evnt_multi_(short flags, short clicks, short mask, short state,
		  unsigned long timer, short *msg, AEVENT *ev)
{
	short i;
	aintin[0] = flags;
	aintin[1] = clicks;
	aintin[2] = mask;
	aintin[3] = state;
	for (i = 4; i < 14; i++)
		aintin[i] = 0;
	for (i = 0; i < 5; i++)
		aintin[4 + i] = m1[i];
	aintin[14] = (short)(timer & 0xffff);
	aintin[15] = (short)(timer >> 16);
	addrin[0] = msg;
	aes(25, 16, 7, 1, 0);
	ev->which = aintout[0];
	ev->mx = aintout[1];
	ev->my = aintout[2];
	ev->mbutton = aintout[3];
	ev->kstate = aintout[4];
	ev->kreturn = aintout[5];
	ev->breturn = aintout[6];
	return ev->which;
}

/* ---------------- VDI ---------------- */

static short contrl[12];
static short vintin[128];
static short ptsin[128];
static short vintout[128];
static short ptsout[128];

static void *vdipb[5] = { contrl, vintin, ptsin, vintout, ptsout };

static void vdi(short op, short nptsin, short nintin, short h)
{
	contrl[0] = op;
	contrl[1] = nptsin;
	contrl[3] = nintin;
	contrl[5] = 0;
	contrl[6] = h;
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
		: : "r"((long)vdipb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

enum { A_WR, A_FINT, A_FSTY, A_FCOL, A_FPER, A_LCOL, A_TCOL, A_TEFF, A_N };
static short attr_h = -1, attr[A_N];

short v_opnvwk_(short phys, short *work_out)
{
	short i;
	for (i = 0; i < 10; i++)
		vintin[i] = 1;
	vintin[10] = 2;		/* raster coordinates */
	attr_h = -1;		/* a new workstation: its attributes are the defaults */
	vdi(100, 0, 11, phys);
	for (i = 0; i < 45; i++)
		work_out[i] = vintout[i];
	for (i = 0; i < 12; i++)
		work_out[45 + i] = ptsout[i];
	return contrl[6];
}

void v_clsvwk(short h) { vdi(101, 0, 0, h); }

void vq_extnd(short h, short owflag, short *work_out)
{
	short i;
	vintin[0] = owflag;
	vdi(102, 0, 1, h);
	for (i = 0; i < 45; i++)
		work_out[i] = vintout[i];
}

/* NVDI's vq_scrninfo (vq_extnd, sub-function 1, mode 2): how pixels are
 * kept, in work_out[0..271] (format, planes, colours, where each colour's
 * bits go, the pen of each VDI colour); 0 when the VDI hasn't got it (TOS
 * and EmuTOS answer as vq_extnd does, with 45 words) */
static short scrninfo_out[272];

short vq_scrninfo(short h, short *work_out)
{
	short i;
	vdipb[3] = scrninfo_out;
	vintin[0] = 2;
	contrl[0] = 102;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[4] = 0;
	contrl[5] = 1;
	contrl[6] = h;
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
		: : "r"((long)vdipb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	vdipb[3] = vintout;
	if (contrl[4] < 272)
		return 0;
	for (i = 0; i < 272; i++)
		work_out[i] = scrninfo_out[i];
	return 1;
}

void vs_clip(short h, short on, const short *pxy)
{
	vintin[0] = on;
	ptsin[0] = pxy[0];
	ptsin[1] = pxy[1];
	ptsin[2] = pxy[2];
	ptsin[3] = pxy[3];
	vdi(129, 2, 1, h);
}

/* VDI attributes as last set (ECal's draw.c does this; here in the
   bindings, so every caller gets it and none can make it stale): each
   call is a trap, and on an 8 MHz ST setting them again for every piece
   of text cost more than the text. One workstation at a time: another
   handle starts again. */

static void vdi_attr(short h, int which, short v, short op)
{
	if (h != attr_h) {
		short i;
		for (i = 0; i < A_N; i++)
			attr[i] = -32768;
		attr_h = h;
	}
	if (attr[which] == v)
		return;
	attr[which] = v;
	vintin[0] = v;
	vdi(op, 0, 1, h);
}

void vswr_mode(short h, short mode) { vdi_attr(h, A_WR, mode, 32); }
void vsf_interior(short h, short s) { vdi_attr(h, A_FINT, s, 23); }
void vsf_style(short h, short s) { vdi_attr(h, A_FSTY, s, 24); }
void vsf_color(short h, short c) { vdi_attr(h, A_FCOL, c, 25); }
void vsf_perimeter(short h, short on) { vdi_attr(h, A_FPER, on, 104); }
void vsl_color(short h, short c) { vdi_attr(h, A_LCOL, c, 17); }
void vsl_type(short h, short t) { vintin[0] = t; vdi(15, 0, 1, h); }
void vst_color(short h, short c) { vdi_attr(h, A_TCOL, c, 22); }
void vst_effects(short h, short fx) { vdi_attr(h, A_TEFF, fx, 106); }

void vst_alignment(short h, short hor, short ver)
{
	vintin[0] = hor;
	vintin[1] = ver;
	vdi(39, 0, 2, h);
}

void vst_height(short h, short height, short *cw_, short *ch_)
{
	ptsin[0] = 0;
	ptsin[1] = height;
	vdi(12, 1, 0, h);
	*cw_ = ptsout[2];
	*ch_ = ptsout[3];
}

void vs_color(short h, short index, const short *rgb)
{
	vintin[0] = index;
	vintin[1] = rgb[0];
	vintin[2] = rgb[1];
	vintin[3] = rgb[2];
	vdi(14, 0, 4, h);
}

void vq_color(short h, short index, short *rgb)
{
	vintin[0] = index;
	vintin[1] = 0;		/* the value that was set */
	vdi(26, 0, 2, h);
	rgb[0] = vintout[1];
	rgb[1] = vintout[2];
	rgb[2] = vintout[3];
}

void vr_recfl(short h, const short *pxy)
{
	ptsin[0] = pxy[0];
	ptsin[1] = pxy[1];
	ptsin[2] = pxy[2];
	ptsin[3] = pxy[3];
	vdi(114, 2, 0, h);
}

void v_pline(short h, short n, const short *pxy)
{
	short i;
	for (i = 0; i < n * 2; i++)
		ptsin[i] = pxy[i];
	vdi(6, n, 0, h);
}

void vro_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst)
{
	short i;
	vintin[0] = mode;
	for (i = 0; i < 8; i++)
		ptsin[i] = pxy[i];
	contrl[7] = (short)((u32)src >> 16);
	contrl[8] = (short)((u32)src & 0xffff);
	contrl[9] = (short)((u32)dst >> 16);
	contrl[10] = (short)((u32)dst & 0xffff);
	vdi(109, 4, 1, h);
}

void vrt_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst, const short *colors)
{
	short i;
	vintin[0] = mode;
	vintin[1] = colors[0];
	vintin[2] = colors[1];
	for (i = 0; i < 8; i++)
		ptsin[i] = pxy[i];
	contrl[7] = (short)((u32)src >> 16);
	contrl[8] = (short)((u32)src & 0xffff);
	contrl[9] = (short)((u32)dst >> 16);
	contrl[10] = (short)((u32)dst & 0xffff);
	vdi(121, 4, 3, h);
}

short vq_gdos(void)
{
	register long r __asm__("d0");
	__asm__ volatile("moveq #-2,%%d0\n\ttrap #2"
		: "=r"(r) : : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return (short)r != -2;
}

short vst_load_fonts(short h, short select)
{
	vintin[0] = select;
	vdi(119, 0, 1, h);
	return vintout[0];
}

void vst_unload_fonts(short h, short select)
{
	vintin[0] = select;
	vdi(120, 0, 1, h);
}

short vqt_name(short h, short index, char *name)
{
	short i;
	vintin[0] = index;
	vdi(130, 0, 1, h);
	for (i = 0; i < 32; i++)
		name[i] = (char)vintout[1 + i];
	name[32] = 0;
	return vintout[0];
}

/* NVDI and SpeedoGDOS also say what kind of font it is (intout[34]):
 * 1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1; 0 when the VDI doesn't */
short vqt_font_format(short h, short index)
{
	vintin[0] = index;
	vintin[1] = 0;
	contrl[4] = 0;
	vdi(130, 0, 2, h);
	if (contrl[4] < 35)
		return 0;
	return vintout[34] & 0xff;
}

short vst_font(short h, short id)
{
	vintin[0] = id;
	vdi(21, 0, 1, h);
	return vintout[0];
}

short vst_point(short h, short point, short *cw_, short *ch_)
{
	vintin[0] = point;
	vdi(107, 0, 1, h);
	*cw_ = ptsout[2];
	*ch_ = ptsout[3];
	return vintout[0];
}

short vqt_width(short h, short c)
{
	vintin[0] = c;
	vdi(117, 0, 1, h);
	return ptsout[0];
}

/* NVDI 5: a character's index in another mapping (0 the font's own
 * index, 1 the Atari character set, 2 Unicode); -1 when the font hasn't
 * got it or the VDI can't say */
long vqt_char_index(short h, unsigned short c, short from, short to)
{
	vintin[0] = (short)c;
	vintin[1] = from;
	vintin[2] = to;
	contrl[4] = 0;
	vdi(190, 0, 3, h);
	if (contrl[4] < 1 || (unsigned short)vintout[0] == 0xffff)
		return -1;
	return (unsigned short)vintout[0];
}

/* NVDI 4 and later: how text codes are read (0 the font's own index,
 * 1 the Atari character set); the mode now in force, -1 without */
short vst_map_mode(short h, short mode)
{
	vintin[0] = mode;
	contrl[4] = 0;
	vdi(236, 0, 1, h);
	return contrl[4] < 1 ? -1 : vintout[0];
}

/* NVDI 3 and later, SpeedoGDOS 5: the font's names; 0 when the VDI
 * doesn't answer (TOS's, FONTGDOS) or has no such font */
short vqt_xfntinfo(short h, short flags, short id, short index, XFNT_INFO *info)
{
	info->size = sizeof(*info);
	info->format = 0;
	vintin[0] = flags;
	vintin[1] = id;
	vintin[2] = index;
	vintin[3] = (short)((u32)info >> 16);
	vintin[4] = (short)((u32)info & 0xffff);
	contrl[4] = 0;
	vdi(229, 0, 5, h);
	if (contrl[4] < 1 || !vintout[0])
		return 0;
	return vintout[0];
}

/* text given as 16-bit codes (font indices in map mode 0) */
void v_gtext_w(short h, short x, short y, const unsigned short *s, short n)
{
	short i;
	if (n > 127)
		n = 127;
	if (n <= 0)
		return;
	for (i = 0; i < n; i++)
		vintin[i] = (short)s[i];
	ptsin[0] = x;
	ptsin[1] = y;
	vdi(8, 1, n, h);
}

void v_hide_c(short h) { vdi(123, 0, 0, h); }

void v_show_c(short h, short reset)
{
	vintin[0] = reset;
	vdi(122, 0, 1, h);
}

void v_gtext_n(short h, short x, short y, const char *s, short n)
{
	short i;
	if (n > 127)
		n = 127;
	if (n <= 0)
		return;
	for (i = 0; i < n; i++)
		vintin[i] = (u8)s[i];
	ptsin[0] = x;
	ptsin[1] = y;
	vdi(8, 1, n, h);
}

/* ---------------- more VDI (from EHypView) ---------------- */

/* a GDP call (opcode 11) with its sub-function */
static void gdp(short sub, short nptsin, short h)
{
	contrl[0] = 11;
	contrl[1] = nptsin;
	contrl[3] = 0;
	contrl[5] = sub;
	contrl[6] = h;
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
		: : "r"((long)vdipb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

static void gdp_box(short sub, short h, const short *pxy)
{
	short i;
	for (i = 0; i < 4; i++)
		ptsin[i] = pxy[i];
	gdp(sub, 2, h);
}

void v_bar(short h, const short *pxy) { gdp_box(1, h, pxy); }
void v_rbox(short h, const short *pxy) { gdp_box(8, h, pxy); }
void v_rfbox(short h, const short *pxy) { gdp_box(9, h, pxy); }

void vsl_ends(short h, short beg, short end)
{
	vintin[0] = beg;
	vintin[1] = end;
	vdi(108, 0, 2, h);
}

void vr_trnfm(short h, MFDB *src, MFDB *dst)
{
	contrl[7] = (short)((u32)src >> 16);
	contrl[8] = (short)((u32)src & 0xffff);
	contrl[9] = (short)((u32)dst >> 16);
	contrl[10] = (short)((u32)dst & 0xffff);
	vdi(110, 0, 0, h);
}
