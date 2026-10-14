/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * gem.h - minimal AES/VDI bindings (no GEMlib needed).
 * From Claude ST (whomper/ClaudeST), extended for EMail's dialogs, ECal's
 * accessory and iconify, EContact's clipboard and EFTP's VDI cache.
 */
#ifndef GEM_H
#define GEM_H

typedef struct {
	short ob_next, ob_head, ob_tail;
	unsigned short ob_type, ob_flags, ob_state;
	long ob_spec;
	short ob_x, ob_y, ob_width, ob_height;
} OBJECT;

typedef struct {
	char *te_ptext, *te_ptmplt, *te_pvalid;
	short te_font, te_fontid, te_just, te_color, te_fontsize, te_thickness;
	short te_txtlen, te_tmplen;
} TEDINFO;

/* object types / flags / states */
#define G_BOX     20
#define G_TEXT    21
#define G_BOXTEXT 22
#define G_IBOX    25
#define G_BUTTON  26
#define G_BOXCHAR 27
#define G_STRING  28
#define G_FTEXT   29
#define G_FBOXTEXT 30
#define G_TITLE   32
#define SELECTABLE 0x0001
#define DEFAULT   0x0002
#define EXIT      0x0004
#define EDITABLE  0x0008
#define RBUTTON   0x0010
#define LASTOB    0x0020
#define TOUCHEXIT 0x0040
#define HIDETREE  0x0080
#define SELECTED  0x0001
#define CROSSED   0x0002
#define CHECKED   0x0004
#define DISABLED  0x0008
#define OUTLINED  0x0010
#define SHADOWED  0x0020

/* events */
#define MU_KEYBD  0x0001
#define MU_BUTTON 0x0002
#define MU_MESAG  0x0010
#define MU_M1     0x0004
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
#define WM_NEWTOP   29
#define WM_ONTOP    31
#define WM_BOTTOMED 33	/* AES 4.1 and newer, MagiC, XaAES: to the back */
#define WM_ICONIFY     34	/* AES 4.1 and newer, MagiC, XaAES */
#define WM_UNICONIFY   35
#define WM_ALLICONIFY  36
#define AP_TERM     50
#define AC_OPEN     40
#define AC_CLOSE    41
#define WM_HSLID    25

/* window parts */
#define NAME    0x0001
#define CLOSER  0x0002
#define FULLER  0x0004
#define MOVER   0x0008
#define SIZER   0x0020
#define SMALLER 0x4000	/* the iconifier */
#define UPARROW 0x0040
#define DNARROW 0x0080
#define VSLIDE  0x0100
#define LFARROW 0x0200
#define RTARROW 0x0400
#define HSLIDE  0x0800
#define INFO    0x0010

/* wind_get / wind_set */
#define WF_NAME       2
#define WF_INFO       3
#define WF_WORKXYWH   4
#define WF_CURRXYWH   5
#define WF_PREVXYWH   6
#define WF_FULLXYWH   7
#define WF_HSLIDE     8
#define WF_VSLIDE     9
#define WF_TOP        10
#define WF_FIRSTXYWH  11
#define WF_NEXTXYWH   12
#define WF_HSLSIZE    15
#define WF_VSLSIZE    16
#define WF_ICONIFY    26
#define WF_UNICONIFY  27
#define WF_BOTTOM     25	/* AES 4.1 and newer: send the window to the back */

#define WC_BORDER 0
#define WC_WORK   1

/* WM_ARROWED actions */
#define WA_UPPAGE 0
#define WA_DNPAGE 1
#define WA_UPLINE 2
#define WA_DNLINE 3
#define WA_LFPAGE 4
#define WA_RTPAGE 5
#define WA_LFLINE 6
#define WA_RTLINE 7

/* VDI writing modes and fill styles */
#define MD_REPLACE  1
#define MD_TRANS    2
#define MD_XOR      3
#define FIS_HOLLOW  0
#define FIS_SOLID   1
#define FIS_PATTERN 2
#define FIS_HATCH   3

#define BEG_UPDATE 1
#define END_UPDATE 0

#define ARROW     0
#define FLAT_HAND 4
#define BUSYBEE   2
#define M_OFF     256
#define M_ON      257

#define K_RSHIFT 0x01
#define K_LSHIFT 0x02
#define K_CTRL   0x04

typedef struct {
	short which, mx, my, mbutton, kstate, kreturn, breturn;
} AEVENT;

short appl_init(void);
short appl_exit(void);
short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox);
short graf_mouse(short num, void *form);
short graf_mkstate(short *mx, short *my, short *mb, short *ks);
short graf_rubberbox(short x, short y, short minw, short minh, short *w, short *h);
void evnt_set_m1(short leave, short x, short y, short w, short h);
short menu_bar(OBJECT *tree, short show);
short menu_tnormal(OBJECT *tree, short title, short normal);
short menu_register(short apid, const char *name);
short rsrc_obfix(OBJECT *tree, short obj);
short form_alert(short def, const char *str);
short rsrc_load(const char *name);
short aes_version(void);
short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4);	/* 0: not known */
short fsel_exinput(char *path, char *name, short *button, const char *title);
short form_do(OBJECT *tree, short start);
short form_keybd(OBJECT *tree, short obj, short next, short ch, short *onext, short *och);
short form_button(OBJECT *tree, short obj, short clicks, short *onext);
short objc_edit(OBJECT *tree, short obj, short ch, short *idx, short kind);
#define ED_INIT 1
#define ED_CHAR 2
#define ED_END  3

typedef struct {
	void *bi_pdata;
	short bi_wb, bi_hl, bi_x, bi_y, bi_color;
} BITBLK;
#define G_IMAGE 23
short form_center(OBJECT *tree, short *x, short *y, short *w, short *h);
short objc_find(OBJECT *tree, short start, short depth, short x, short y);
short objc_offset(OBJECT *tree, short obj, short *x, short *y);
short menu_icheck(OBJECT *tree, short item, short check);
short menu_ienable(OBJECT *tree, short item, short enable);
short evnt_timer_(unsigned long ms);
short appl_id(void);
short appl_find(const char *name);	/* 8 characters, blank-padded; -1 if none */
short appl_write(short id, short len, const void *buf);
short scrp_read(char *path);			/* the clipboard's folder */
short scrp_write(const char *path);
short wind_find(short x, short y);
OBJECT *rsrc_tree(short index);
short objc_draw(OBJECT *tree, short start, short depth, short x, short y, short w, short h);
short form_dial(short flag, short x, short y, short w, short h);
#define FMD_START  0
#define FMD_FINISH 3
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
		  unsigned long timer, short *msg, AEVENT *ev);

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
void vsl_type(short h, short type);	/* 1 solid, 3 dotted */
void v_pline(short h, short n, const short *pxy);
void vst_color(short h, short color);
void vst_effects(short h, short fx);
void vst_alignment(short h, short hor, short ver);
void vst_height(short h, short height, short *cw, short *ch);
void vs_color(short h, short index, const short *rgb);
void vq_color(short h, short index, short *rgb);
void v_gtext_n(short h, short x, short y, const char *s, short n);
typedef struct {
	void *fd_addr;
	short fd_w, fd_h, fd_wdwidth, fd_stand, fd_nplanes, fd_r1, fd_r2, fd_r3;
} MFDB;

void vro_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst);
void vrt_cpyfm(short h, short mode, const short *pxy, MFDB *src, MFDB *dst, const short *colors);
void vq_extnd(short h, short owflag, short *work_out);
short vq_gdos(void);			/* 1 if a GDOS (NVDI, SpeedoGDOS...) is loaded */
short vst_load_fonts(short h, short select);
void vst_unload_fonts(short h, short select);
short vqt_name(short h, short index, char *name);	/* -> font id; name of 33 chars */
long  vqt_char_index(short h, unsigned short c, short from, short to);
short vst_map_mode(short h, short mode);
void  v_gtext_w(short h, short x, short y, const unsigned short *s, short n);
short vqt_font_format(short h, short index);	/* 1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1, 0 unknown */
short vst_font(short h, short id);
short vst_point(short h, short point, short *cw, short *ch);
short vqt_width(short h, short c);	/* cell width of character c */
void v_hide_c(short h);
void v_show_c(short h, short reset);

/* more VDI (from EHypView) */
void v_bar(short h, const short *pxy);
void v_rbox(short h, const short *pxy);
void v_rfbox(short h, const short *pxy);
void vsl_ends(short h, short beg, short end);	/* 0 square, 1 arrow, 2 round */
void vr_trnfm(short h, MFDB *src, MFDB *dst);

#endif
