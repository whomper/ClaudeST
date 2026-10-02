/*
 * gem.h - minimal AES/VDI bindings for Claude ST (no GEMlib needed).
 */
#ifndef GEM_H
#define GEM_H

typedef struct {
	short ob_next, ob_head, ob_tail;
	unsigned short ob_type, ob_flags, ob_state;
	long ob_spec;
	short ob_x, ob_y, ob_width, ob_height;
} OBJECT;

/* object types / flags / states */
#define G_BOX     20
#define G_IBOX    25
#define G_STRING  28
#define G_TITLE   32
#define LASTOB    0x0020
#define CHECKED   0x0004
#define DISABLED  0x0008

/* events */
#define MU_KEYBD  0x0001
#define MU_BUTTON 0x0002
#define MU_MESAG  0x0010
#define MU_TIMER  0x0020

/* messages */
#define MN_SELECTED 10
#define WM_REDRAW   20
#define WM_TOPPED   21
#define WM_CLOSED   22
#define WM_FULLED   23
#define WM_ARROWED  24
#define WM_VSLID    26
#define WM_SIZED    27
#define WM_MOVED    28
#define AP_TERM     50

/* window parts */
#define NAME    0x0001
#define CLOSER  0x0002
#define FULLER  0x0004
#define MOVER   0x0008
#define SIZER   0x0020
#define UPARROW 0x0040
#define DNARROW 0x0080
#define VSLIDE  0x0100

/* wind_get / wind_set */
#define WF_NAME       2
#define WF_WORKXYWH   4
#define WF_CURRXYWH   5
#define WF_PREVXYWH   6
#define WF_FULLXYWH   7
#define WF_VSLIDE     9
#define WF_TOP        10
#define WF_FIRSTXYWH  11
#define WF_NEXTXYWH   12
#define WF_VSLSIZE    16

#define WC_BORDER 0
#define WC_WORK   1

#define BEG_UPDATE 1
#define END_UPDATE 0

#define ARROW     0
#define BUSYBEE   2
#define M_OFF     256
#define M_ON      257

#define K_RSHIFT 0x01
#define K_LSHIFT 0x02
#define K_CTRL   0x04

typedef struct {
	short which, mx, my, mbutton, kstate, kreturn, breturn;
} EVENT;

short appl_init(void);
short appl_exit(void);
short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox);
short graf_mouse(short num, void *form);
short menu_bar(OBJECT *tree, short show);
short menu_tnormal(OBJECT *tree, short title, short normal);
short menu_register(short apid, const char *name);
short rsrc_obfix(OBJECT *tree, short obj);
short form_alert(short def, const char *str);
short wind_create(short kind, short x, short y, short w, short h);
short wind_open(short h, short x, short y, short w, short ht);
short wind_close(short h);
short wind_delete(short h);
short wind_get(short h, short field, short *a, short *b, short *c, short *d);
short wind_set(short h, short field, short a, short b, short c, short d);
short wind_set_str(short h, short field, const char *s);
short wind_update(short mode);
short wind_calc(short type, short kind, short x, short y, short w, short h,
		short *ox, short *oy, short *ow, short *oh);
short evnt_multi_(short flags, short clicks, short mask, short state,
		  unsigned long timer, short *msg, EVENT *ev);

/* VDI */
short v_opnvwk_(short phys, short *work_out);
void v_clsvwk(short h);
void vs_clip(short h, short on, const short *pxy);
void vswr_mode(short h, short mode);
void vsf_interior(short h, short style);
void vsf_style(short h, short idx);
void vsf_color(short h, short color);
void vsf_perimeter(short h, short on);
void vr_recfl(short h, const short *pxy);
void vsl_color(short h, short color);
void v_pline(short h, short n, const short *pxy);
void vst_color(short h, short color);
void vst_effects(short h, short fx);
void vst_alignment(short h, short hor, short ver);
void v_gtext_n(short h, short x, short y, const char *s, short n);
typedef struct {
	void *fd_addr;
	short fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} MFDB;

void vro_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst);
void vq_extnd(short h, short owflag, short *work_out);

#endif
