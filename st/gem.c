/*
 * gem.c - minimal AES/VDI bindings for Claude ST.
 */
#include "tos.h"
#include "gem.h"

/* ---------------- AES ---------------- */

static short control[5];
static short global[15];
static short aintin[16];
static short aintout[7];
static void *addrin[2];
static void *addrout[1];

static void *aespb[6] = { control, global, aintin, aintout, addrin, addrout };

static short aes(short op, short nin, short nout, short nain, short naout)
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
		  unsigned long timer, short *msg, EVENT *ev)
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

short v_opnvwk_(short phys, short *work_out)
{
	short i;
	for (i = 0; i < 10; i++)
		vintin[i] = 1;
	vintin[10] = 2;		/* raster coordinates */
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

void vs_clip(short h, short on, const short *pxy)
{
	vintin[0] = on;
	ptsin[0] = pxy[0];
	ptsin[1] = pxy[1];
	ptsin[2] = pxy[2];
	ptsin[3] = pxy[3];
	vdi(129, 2, 1, h);
}

void vswr_mode(short h, short mode) { vintin[0] = mode; vdi(32, 0, 1, h); }
void vsf_interior(short h, short s) { vintin[0] = s; vdi(23, 0, 1, h); }
void vsf_style(short h, short s) { vintin[0] = s; vdi(24, 0, 1, h); }
void vsf_color(short h, short c) { vintin[0] = c; vdi(25, 0, 1, h); }
void vsf_perimeter(short h, short on) { vintin[0] = on; vdi(104, 0, 1, h); }
void vsl_color(short h, short c) { vintin[0] = c; vdi(17, 0, 1, h); }
void vst_color(short h, short c) { vintin[0] = c; vdi(22, 0, 1, h); }
void vst_effects(short h, short fx) { vintin[0] = fx; vdi(106, 0, 1, h); }

void vst_alignment(short h, short hor, short ver)
{
	vintin[0] = hor;
	vintin[1] = ver;
	vdi(39, 0, 2, h);
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
