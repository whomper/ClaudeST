/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ClaudeST - a Claude.ai client for the Atari ST, STE, TT and Falcon.
 *
 * A single GEM window: the sidebar on the left (New chat, Search, Chats,
 * Projects, Artifacts and the current list), the active conversation on
 * the right with an input line underneath. The machine talks a small
 * line protocol over the serial port (AUX:) to claude_bridge.py running
 * on any modern computer, which does the TLS/HTTP work against claude.ai.
 * See ../PROTOCOL.md.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tos.h"
#include "gem.h"
#include "sting.h"
#include "bidi.h"
#include "crash.h"
#include "help.h"
#include "av.h"
#include "alert.h"
#include "mbar.h"
#include "grect.h"
#include "popup.h"
#include "aboutbox.h"
#include "iconify.h"
#include "clip.h"
#include "dd.h"
#include "bigbuf.h"
#include "icon.h"
#include "icon16.h"

/* copy at most n-1 characters and always end the string (ClaudeST's own;
   the shared C library has none) */
static void strlcpy_(char *d, const char *s, int n)
{
	while (n > 1 && *s) {
		*d++ = *s++;
		n--;
	}
	*d = 0;
}

/* the last release (tools/bump.py raises it); every build shows it */
#define VERSION "1.49"
#define APP_VERSION VERSION

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static short apid, phys;
short vh;				/* the VDI workstation (and the shared blocks', st/ui.h) */
short cw, ch;				/* character cell of the text font */
short sys_cw, sys_ch;			/* the system font's, as the AES uses it */

/* the text font (Options > Font), as in EMail and ECal: font_id 1 is the
 * system font (font_pt 8 or 16: its 8x8 or 8x16 size; 0 the screen's
 * own), else a GDOS font at font_pt points */
static short font_id = 1, font_pt;
static short cur_font_id = 1, cur_font_pt;	/* the font set now (set_font) */
static short set_font(short id, short pt);
static short gdos_fonts = -1;		/* GDOS fonts loaded; -1 not yet */
/* where the font keeps its Hebrew letters (for system and bitmap fonts):
 * the Atari character set, or where ISO-8859-8 or DOS 862 put them */
#define HEB_ATARI 0
#define HEB_ISO   1
#define HEB_DOS   2
static short hebfont;
/* how the current font gets Hebrew: */
#define HM_MAP     0			/* where hebfont says (system and bitmap fonts) */
#define HM_UNICODE 1			/* a vector font's own, found by Unicode (NVDI 5) */
#define HM_SYSTEM  2			/* a vector font without them: the system font's */
static short heb_mode;
#define HW_NONE     0
#define HW_NOHEB    1			/* the font has no Hebrew letters */
#define HW_NOLOOKUP 2			/* the VDI can't look letters up (no NVDI 5) */
static short heb_why;
static unsigned short heb_idx[27];	/* HM_UNICODE: the font's index of each letter */
static void font_reselect(void);
static short ncolors;
short scr_w, scr_h;
short desk_x, desk_y, desk_w, desk_h, planes;	/* for the shared popup and About box */
static short win = -1;
static short wx, wy, ww, wh;		/* window work area */
static short quit;

/* layout */
static short sb_w;			/* sidebar width */
static short sb_user;			/* width set by dragging the divider, 0 = auto */
static short over_divider;		/* mouse is over the divider (MU_M1) */
static short px, pw;			/* chat pane x / width */
static short title_h, input_h, row_h;
static short tx, ty, tw, th;		/* text area */
static short rows, cols;
static short nav_y;			/* first nav row */
#define NNAV 6				/* New chat, Search, Chats, Code, Projects, Artifacts */
static short list_y, list_rows;		/* sidebar list */
static short status_y;

/* conversation text: paragraphs separated by '\n'.
 * "\x10R" starts a message header line (R = role), "\x11" a bold line. */
#define MK_HEADER 0x10
#define MK_BOLD   0x11
#define MK_GREY   0x12		/* a tool step, in grey as on claude.ai */
#define MK_CODE   0x13		/* a line of a code block, in blue */
#define MK_INL    0x14		/* starts or ends inline code, in red */

static char *tb;
static long tlen, tcap;

typedef struct {
	long off;
	short len;
	u8 kind;
	u8 role;
} LINE;

#define L_TEXT   0
#define L_HEADER 1
#define L_BOLD   2
#define L_GREY   3
#define L_CODE   4
#define L_TYPE   0x0f
#define L_INL    0x20		/* the line starts inside inline code */
#define L_RTL    0x40		/* line of a right-to-left (Hebrew) paragraph */

static LINE *lines;
static short nlines, maxlines;
static short top_line;
static short last_slider_pos = -1, last_slider_size = -1;

/* sidebar list */
typedef struct {
	char id[40];
	char label[72];
	char pinned;
	char unread;		/* updated since last read: a dot in the margin */
	char state;		/* Code sessions: 'W' Claude works, 'A' it waits for you */
	char heading;		/* a date heading ("Today"), not an item */
} ITEM;

#define MAXITEMS 300
static short dot_pulse = 1;	/* the working dot's on or off half */
static ITEM items[MAXITEMS];
static short nitems, list_top;
static short lcur = -1;			/* keyboard cursor in the list */
static short menu_target = -1;		/* item the open popup menu acts on */
static short nav_pressed = -1;		/* top menu entry under a held mouse button */
static short list_loading;		/* a list was asked for and hasn't arrived */
static char notice[64];			/* a passing message from the bridge (N) */
static long notice_until;
static short title_cut;			/* the chat title didn't fit */
static short title_xr;			/* where the title area ends */
static short nav_pending = -1;		/* area chosen, its list not arrived yet */
static char list_kind[12] = "CHATS";
static char list_title[48] = "Recents";
static char cur_id[40];
static short fresh = 1;			/* just started: nothing opened yet */
static char chat_title[80] = "New chat";
static char status[64] = "Waiting for bridge...";
static short busy;
static short online;
static short win_icon;			/* shown as ClaudeST's icon (WM_ICONIFY) */
static short usage_pct = -1;		/* session usage from the bridge's u line; -1: none */
static char usage_rst[12];		/* and the time until it resets, "2h 10m" */

/* input line */
#define INMAX 1000
static char input[INMAX + 1];
static short inlen;
static short in_cur, in_start;		/* cursor and scroll position in input */
static short search_mode;
static short drop_list;		/* a late list that would refill the search pane */
static short hebrew_kbd;		/* typing in Hebrew (SI-1452 layout) */

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
 * Atari character codes; 0 = key not remapped */
static const u8 hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

/* "move to project" picker, filled by the bridge (Q/J/W) */
#define PICKMAX 20
static char pick_id[PICKMAX][40];
static char pick_label[PICKMAX][44];
static short npick;
static char pick_chat[40];
static short pick_x;

/* saving an artifact: SAVE -> F <name> -> file selector -> FETCH -> D.. G */
static char save_id[40];
static short save_fd = -1;
static long save_bytes;

/* disk work in big pieces (the guidelines, 6.5): Save to disk gathers the
   file in memory and writes it in one go, Attach reads 16 KB (68000) or
   32 KB (68030 and up) at a time. The memory is big_alloc's, TT RAM when
   the machine has it; with too little free, this buffer. */
#define DISK_BUF (16L * 1024)
static char disk_buf[DISK_BUF];
static char *save_buf;			/* what is gathered, save_len of save_cap */
static long save_cap, save_len;
static char save_path[160];

/* dirty regions */
#define D_SIDEBAR 1
#define D_TITLE   2
#define D_TEXT    4
#define D_INPUT   8
#define D_STATUS  16
#define D_LIST    32		/* the list only, not the menu above it */
#define D_ALL     15
static short dirty;
static short text_dirty_row = 0x7fff;	/* first text row needing redraw */
static short pending_scroll;		/* lines auto-scrolled since last redraw */

/* serial */
static IOREC *iorec;
static char *old_ibuf;
static short old_ibufsiz, old_low, old_high;
#define RXBUF 16384
static char rxring[RXBUF];
static short baud = BAUD_19200;
static short serial_is_open;

/* link to the bridge: serial port, or TCP through STinG */
#define LINK_SERIAL 0
#define LINK_TCP    1
static short link = LINK_SERIAL;
static u32 tcp_ip;
static u16 tcp_port = 2323;
static short tcp_cn = -1;
static long next_connect;
static short tcp_opening = -1;		/* a connection being opened */
static long opening_until;
static char txbuf[1200];
static short txlen;

/* files attached to the next message (the bridge holds their data);
   as many as claude.ai takes in a chat */
#define MAXATT 20
static char att_name[MAXATT][14];
static short natt;

static char rxline[1200];
static short rxlen;
static long ticks, last_hello;
/* the heartbeat: with the gateway quiet for a while, PING; no answer
   in time and it is taken as gone. Only with a bridge that answers
   (it says so with a p line after HELLO) */
#define PING_QUIET 30000L		/* ms without a line either way */
#define PING_WAIT  20000L		/* ms for the answer */
static char can_ping;
static u32 last_rx, last_tx, ping_at;

/* Claude model and effort, offered by the bridge (O/V/U/K lines) */
#define NMODEL 12
#define NEFFORT 6
static char mod_id[NMODEL][28], mod_lab[NMODEL][20];
static char eff_id[NEFFORT][8], eff_lab[NEFFORT][14];
static short nmodel, neffort;
static char cur_model[28], cur_effort[8];
static char pref_model[28], pref_effort[8];	/* the user's choice, kept in CLAUDEST.INF */
static short pref_sent;				/* sent to the bridge since HELLO */
static short chip_x0, chip_x1;			/* the model chip in the reply line */

static void tx_cmd(const char *a, const char *b, const char *c);
static void show_picker(void);
static void choose_save_file(const char *name);
static void save_chunk(const char *hex, long size);
static void finish_save(void);
static void save_config(void);
static void say_notice(const char *s);
static char *put_num(char *p, u16 v);
static char home_dir[128];		/* where CLAUDEST.PRG was started */

/* ------------------------------------------------------------------ */
/* menu bar (built in code, so no .RSC file is needed)                 */
/* ------------------------------------------------------------------ */

/* the commands of the menu bar */
enum {
	C_NONE, C_ABOUT, C_NEW, C_FIND, C_REF, C_QUIT, C_CUT, C_COPY, C_PASTE, C_ANSWER,
	C_B48, C_B96, C_B192, C_NET, C_SER, C_HEB, C_ACCT, C_FONT, C_HELP, C_KEYS, C_SET, C_ATTACH
};

/* the menu bar, built by the AERA programs' shared builder
   (common/ui/mbar.c), so no .RSC file is needed */
static const MBAR_ITEM m_desk[] = {
	{ "About ClaudeST...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "New chat", "^N", C_NEW },
	{ "Search chats", "^F", C_FIND },
	{ "Attach file...", "^O", C_ATTACH },
	{ "-", 0, 0 },
	{ "Reconnect", "^R", C_REF },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Cut", "^X", C_CUT },
	{ "Copy", "^C", C_COPY },
	{ "Paste", "^V", C_PASTE },
	{ "-", 0, 0 },
	{ "Copy Claude's answer", 0, C_ANSWER },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_opts[] = {
	{ "Settings...", "^,", C_SET },
	{ "-", 0, 0 },
	{ "4800 baud", 0, C_B48 },
	{ "9600 baud", 0, C_B96 },
	{ "19200 baud", 0, C_B192 },
	{ "-", 0, 0 },
	{ "Network (STinG)", 0, C_NET },
	{ "Serial port", 0, C_SER },
	{ "-", 0, 0 },
	{ "Hebrew keys", "F10", C_HEB },
	{ "Account...", "F8", C_ACCT },
	{ "Font...", 0, C_FONT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "ClaudeST help", "Help", C_HELP },
	{ "Keys...", 0, C_KEYS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " Desk ", m_desk }, { " File ", m_file }, { " Edit ", m_edit }, { " Options ", m_opts },
	{ " Help ", m_help }, { 0, 0 }
};
static MBAR mb;

static void check(int cmd, int on)
{
	short o = mbar_object(&mb, cmd);
	if (o > 0)
		mb.tree[o].ob_state = on ? CHECKED : 0;
}

static void menu_check_baud(void)
{
	check(C_B48, baud == BAUD_4800);
	check(C_B96, baud == BAUD_9600);
	check(C_B192, baud == BAUD_19200);
	check(C_NET, link == LINK_TCP);
	check(C_SER, link == LINK_SERIAL);
	check(C_HEB, hebrew_kbd);
}

/* ------------------------------------------------------------------ */
/* serial port                                                         */
/* ------------------------------------------------------------------ */

/* run in supervisor mode via Supexec, with interrupts masked */
static short ints_off(void)
{
	short sr;
	__asm__ volatile("move.w %%sr,%0\n\tor.w #0x0700,%%sr" : "=d"(sr) : : "memory", "cc");
	return sr;
}

static void ints_restore(short sr)
{
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory", "cc");
}

/* In supervisor mode: under MiNT the IOREC is the system's memory, which
   memory protection keeps a program from reading. MiNT may have none to
   give (Iorec() says 0): then TOS's own buffer stays */
static void sup_set_iorec(void)
{
	short sr;
	if (!iorec)
		return;
	sr = ints_off();
	old_ibuf = iorec->ibuf;
	old_ibufsiz = iorec->ibufsiz;
	old_low = iorec->ibuflow;
	old_high = iorec->ibufhi;
	iorec->ibuf = rxring;
	iorec->ibufsiz = RXBUF;
	iorec->ibufhd = 0;
	iorec->ibuftl = 0;
	iorec->ibuflow = RXBUF / 4;
	iorec->ibufhi = RXBUF * 3 / 4;
	ints_restore(sr);
}

static void sup_restore_iorec(void)
{
	short sr;
	if (!iorec)
		return;
	sr = ints_off();
	iorec->ibuf = old_ibuf;
	iorec->ibufsiz = old_ibufsiz;
	iorec->ibufhd = 0;
	iorec->ibuftl = 0;
	iorec->ibuflow = old_low;
	iorec->ibufhi = old_high;
	ints_restore(sr);
}

static short aux_port;		/* CLAUDEST.INF "port N": Bconmap device, 0 = TOS's choice */
static short old_port;

static void serial_open(void)
{
	if (serial_is_open)
		return;
	serial_is_open = 1;
	/* on a TT or Falcon, "port N" picks which serial port AUX means; TOS
	 * versions without Bconmap return the opcode (44) and change nothing */
	old_port = 0;
	if (aux_port) {
		long prev = Bconmap(-1);
		if (prev != 44 && prev > 0) {
			old_port = (short)prev;
			Bconmap(aux_port);
		}
	}
	Rsconf(baud, 0, -1, -1, -1, -1);
	iorec = Iorec(0);
	Supexec(sup_set_iorec);
}

static void serial_close(void)
{
	if (!serial_is_open)
		return;
	serial_is_open = 0;
	Supexec(sup_restore_iorec);
	if (old_port)
		Bconmap(old_port);
}

static void set_status(const char *s)
{
	crash_note(s);			/* for CRASH.LOG, should ClaudeST crash */
	strlcpy_(status, s, sizeof(status));
	dirty |= D_STATUS;
}

static void tcp_lost(short code)
{
	char msg[64];
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	tcp_cn = -1;
	online = 0;
	strcpy(msg, "Offline: ");
	strlcpy_(msg + 9, sting_error(code), sizeof(msg) - 9);
	set_status(msg);
	next_connect = ticks + 125;	/* retry in ~5 s */
}

static void link_flush(void)
{
	short i, tries = 0;

	if (link == LINK_SERIAL) {
		for (i = 0; i < txlen; i++)
			Bconout(DEV_AUX, (u8)txbuf[i]);
	} else if (tcp_cn >= 0) {
		for (;;) {
			short r = sting_send(tcp_cn, txbuf, txlen);
			if (r == E_NORMAL)
				break;
			if (r != E_OBUFFULL || ++tries > 250) {
				tcp_lost(r);
				break;
			}
			{
				short m[8];
				AEVENT e;
				evnt_multi_(MU_TIMER, 0, 0, 0, 20, m, &e);
			}
		}
	}
	txlen = 0;
}

static void tx_byte(u8 c)
{
	if (txlen < (short)sizeof(txbuf))
		txbuf[txlen++] = c;
}

static void tx_str(const char *s)
{
	while (*s)
		tx_byte((u8)*s++);
}

/* send "A\tB\tC\n" (b and c may be NULL) */
static void tx_cmd(const char *a, const char *b, const char *c)
{
	tx_str(a);
	if (b) {
		tx_byte('\t');
		tx_str(b);
	}
	if (c) {
		tx_byte('\t');
		tx_str(c);
	}
	tx_byte('\n');
	link_flush();
	last_tx = tos_ms();
}

/* send "A\tB\tC\tD\n" */
static void tx_cmd4(const char *a, const char *b, const char *c, const char *d)
{
	tx_str(a);
	tx_byte('\t');
	tx_str(b);
	tx_byte('\t');
	tx_str(c);
	tx_byte('\t');
	tx_str(d);
	tx_byte('\n');
	link_flush();
	last_tx = tos_ms();
}

static void send_hello(void)
{
	pref_sent = 0;
	can_ping = 0;			/* until this bridge says it answers */
	ping_at = 0;
	if (natt) {		/* a new start: the bridge has no files now */
		natt = 0;
		dirty |= D_INPUT;
	}
	tx_cmd("HELLO", "1", APP_VERSION);
	last_hello = ticks;
}

/* ------------------------------------------------------------------ */
/* text buffer and word wrap                                           */
/* ------------------------------------------------------------------ */

static void add_line(long off, short len, u8 kind, u8 role)
{
	if (nlines >= maxlines)
		return;
	lines[nlines].off = off;
	lines[nlines].len = len;
	lines[nlines].kind = kind;
	lines[nlines].role = role;
	nlines++;
}

static short wrap_width(u8 role)
{
	short w = role == 'U' ? cols - 3 : cols;
	return w < 8 ? 8 : w;
}

static void wrap_from(long p, u8 role)
{
	long e, s;
	short w;
	u8 kind;

	while (p < tlen) {
		for (e = p; e < tlen && tb[e] != '\n'; e++)
			;
		if (tb[p] == MK_HEADER && e - p >= 2) {
			role = (u8)tb[p + 1];
			add_line(p, 0, L_HEADER, role);
		} else if (e == p) {
			add_line(p, 0, L_TEXT, role);
		} else {
			kind = L_TEXT;
			s = p;
			if (tb[s] == MK_BOLD) {
				kind = L_BOLD;
				s++;
			} else if (tb[s] == MK_GREY) {
				kind = L_GREY;
				s++;
			} else if (tb[s] == MK_CODE) {
				kind = L_CODE;
				s++;
			}
			/* the paragraph's direction comes from its first strong letter */
			if (bidi_is_rtl(tb + s, e - s > 2000 ? 2000 : (short)(e - s)))
				kind |= L_RTL;
			w = wrap_width(role);
			while (s < e) {
				long b, m;
				short k;
				/* w characters on, not counting inline code marks */
				for (m = s, k = 0; m < e && (k < w || tb[m] == MK_INL); m++)
					if (tb[m] != MK_INL)
						k++;
				if (m >= e) {
					add_line(s, (short)(e - s), kind, role);
					break;
				}
				for (b = m; b > s && tb[b] != ' '; b--)
					;
				if (b == s)
					b = m;
				add_line(s, (short)(b - s), kind, role);
				for (m = s; m < b; m++)	/* inline code going on */
					if (tb[m] == MK_INL)
						kind ^= L_INL;
				s = tb[b] == ' ' ? b + 1 : b;	/* a space breaks the line */
			}
		}
		if (e >= tlen)
			break;
		p = e + 1;
	}
}

static short is_para_start(long off)
{
	return off == 0 || tb[off - 1] == '\n' || tb[off - 1] == MK_BOLD || tb[off - 1] == MK_GREY ||
	       tb[off - 1] == MK_CODE;
}

/* rewrap the last paragraph after text was appended; returns first changed line */
static short wrap_tail(void)
{
	short li = nlines;
	long p = 0;
	u8 role = 0;

	while (li > 0) {
		li--;
		if (is_para_start(lines[li].off))
			break;
	}
	if (nlines) {
		p = lines[li].off;
		if (li < nlines && p > 0 && (((lines[li].kind & L_TYPE) == L_BOLD && tb[p - 1] == MK_BOLD) ||
					     ((lines[li].kind & L_TYPE) == L_GREY && tb[p - 1] == MK_GREY) ||
					     ((lines[li].kind & L_TYPE) == L_CODE && tb[p - 1] == MK_CODE)))
			p--;
		role = li > 0 ? lines[li - 1].role : 0;
	}
	nlines = li;
	wrap_from(p, role);
	return li;
}

static void rewrap_all(void)
{
	nlines = 0;
	wrap_from(0, 0);
}

static short at_bottom(void)
{
	return top_line + rows >= nlines;
}

static void clamp_top(void)
{
	if (top_line > nlines - rows)
		top_line = nlines - rows;
	if (top_line < 0)
		top_line = 0;
}

static void mark_text_from_line(short li)
{
	short r = li - top_line;
	if (r < 0)
		r = 0;
	if (r < text_dirty_row)
		text_dirty_row = r;
	dirty |= D_TEXT;
}

/* drop the oldest half of the conversation when the buffer fills up */
static void trim_buffer(void)
{
	long cut = tlen / 2;
	while (cut < tlen && tb[cut] != '\n')
		cut++;
	if (cut >= tlen)
		cut = tlen;
	else
		cut++;
	memmove(tb, tb + cut, tlen - cut);
	tlen -= cut;
	rewrap_all();
	top_line = nlines;
	clamp_top();
	text_dirty_row = 0;
	dirty |= D_TEXT;
}

static void append(const char *s, short n)
{
	short follow = at_bottom(), li;

	if (tlen + n + 4 >= tcap || nlines >= maxlines - 50)
		trim_buffer();
	memcpy(tb + tlen, s, n);
	tlen += n;
	li = wrap_tail();
	if (follow) {
		short old = top_line;
		top_line = nlines - rows;
		clamp_top();
		if (top_line > old) {
			/* rows already marked dirty moved up with the scroll */
			short d = top_line - old;
			pending_scroll += d;
			if (text_dirty_row != 0x7fff)
				text_dirty_row = text_dirty_row > d ? text_dirty_row - d : 0;
		}
	}
	mark_text_from_line(li);
}

static void clear_text(void)
{
	tlen = 0;
	nlines = 0;
	top_line = 0;
	text_dirty_row = 0;
	dirty |= D_TEXT;
}

static void begin_message(char role)
{
	char hdr[4];
	if (tlen > 0) {
		if (tb[tlen - 1] != '\n')
			append("\n", 1);
		append("\n", 1);
	}
	if (role == 'X')	/* a page (Account): its own headings, no label */
		return;
	hdr[0] = MK_HEADER;
	hdr[1] = role;
	hdr[2] = '\n';
	append(hdr, 3);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static short clip[4];			/* current clip rect x1,y1,x2,y2 */

static short row_visible(short y1, short y2)
{
	return y2 >= clip[1] && y1 <= clip[3];
}

static void fill(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vsf_interior(vh, 1);
	vsf_color(vh, color);
	vr_recfl(vh, p);
}

static void line(short x1, short y1, short x2, short y2, short color)
{
	short p[4];
	p[0] = x1;
	p[1] = y1;
	p[2] = x2;
	p[3] = y2;
	vsl_color(vh, color);
	v_pline(vh, 2, p);
}

/* The Atari character set has the Hebrew letters at 0xC2-0xDC in this
 * order (as offsets from alef in Unicode, and in ISO-8859-8 and DOS 862,
 * which keep Unicode's order). */
static const unsigned char heb_order[27] = {
	/* alef..nun (0xC2-0xCF) */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	/* samekh..tav (0xD0-0xD7) */ 17, 18, 20, 22, 23, 24, 25, 26,
	/* final nun, kaf, mem, pe, tsadi (0xD8-0xDC) */ 15, 10, 13, 19, 21
};

static short is_heb(unsigned char c)
{
	return c >= 0xC2 && c <= 0xDC;
}

static short has_heb(const char *s, short n)
{
	short i;
	for (i = 0; i < n; i++)
		if (is_heb((unsigned char)s[i]))
			return 1;
	return 0;
}

/* move Hebrew letters to where an ISO-8859-8 or DOS 862 font has them */
static void heb_font_map(char *s, short n, short where)
{
	short i;
	unsigned char base = where == HEB_ISO ? 0xE0 : 0x80;
	if (where != HEB_ISO && where != HEB_DOS)
		return;
	for (i = 0; i < n; i++)
		if (is_heb((unsigned char)s[i]))
			s[i] = (char)(base + heb_order[(unsigned char)s[i] - 0xC2]);
}

/* Hebrew in a vector font: its own letters by index (NVDI's map mode 0),
 * or the system font's, one centred in each cell; the rest as usual */
static void heb_runs(short x, short y, const char *s, short n)
{
	short i = 0;
	while (i < n) {
		short j = i, heb = is_heb((unsigned char)s[i]);
		while (j < n && is_heb((unsigned char)s[j]) == heb)
			j++;
		if (!heb) {
			v_gtext_n(vh, x + i * cw, y, s + i, j - i);
		} else if (heb_mode == HM_UNICODE) {
			unsigned short w[128];
			short k;
			for (k = i; k < j && k - i < 128; k++)
				w[k - i] = heb_idx[(unsigned char)s[k] - 0xC2];
			vst_map_mode(vh, 0);
			v_gtext_w(vh, x + i * cw, y, w, k - i);
			vst_map_mode(vh, 1);
		} else {
			short keep_cw = cw, keep_ch = ch, dw, dh, k;
			char c[1];
			vst_font(vh, 1);
			vst_height(vh, keep_ch >= 16 ? 13 : 6, &dw, &dh);
			for (k = i; k < j; k++) {
				c[0] = s[k];
				heb_font_map(c, 1, hebfont);
				v_gtext_n(vh, x + k * keep_cw + (keep_cw - dw) / 2,
					  y + (keep_ch - dh) / 2, c, 1);
			}
			font_reselect();
		}
		i = j;
	}
}

static void text(short x, short y, const char *s, short n, short fx, short color)
{
	vst_effects(vh, fx);
	vst_color(vh, color);
	if (n > 0 && (heb_mode != HM_MAP || hebfont != HEB_ATARI) && has_heb(s, n)) {
		char m[128];
		if (n > (short)sizeof(m))
			n = sizeof(m);
		if (heb_mode != HM_MAP) {
			heb_runs(x, y, s, n);
			return;
		}
		memcpy(m, s, n);
		heb_font_map(m, n, hebfont);
		v_gtext_n(vh, x, y, m, n);
		return;
	}
	v_gtext_n(vh, x, y, s, n);
}

/* draw a string that may contain Hebrew in visual order; a right-to-left
 * string is right-aligned to `right` when that's given. Returns the x used. */
static short text_bidi(short x, short y, const char *s, short n, short fx, short color, short right)
{
	static char vis[BIDI_MAX];
	short rtl;
	if (n <= 0 || !bidi_has_rtl(s, n)) {
		text(x, y, s, n, fx, color);
		return x;
	}
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	rtl = bidi_is_rtl(s, n);
	bidi_visual(s, n, rtl, vis);
	if (rtl && right > 0 && right - n * cw > x)
		x = right - n * cw;
	text(x, y, vis, n, fx, color);
	return x;
}

static short accent(void)
{
	return ncolors >= 4 ? 2 : 1;	/* red where we have colour, else black */
}

/* code blocks in blue, as on claude.ai; four colours have no blue */
static short code_colour(void)
{
	return ncolors >= 16 ? 4 : ncolors >= 4 ? 3 : 1;
}

/* inline code: red, the standard colours' nearest to claude.ai's
   burgundy, or underlined in black and white */
static short inl_colour(void)
{
	return ncolors >= 4 ? 2 : 1;
}

static short inl_fx(void)
{
	return ncolors >= 4 ? 0 : 8;
}

/* degrees clockwise from 12 o'clock to the point (x, y), y pointing up;
 * a close integer approximation of atan2, good to about a degree */
static short cw_angle(long x, long y)
{
	long ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
	short a;
	if (!ax && !ay)
		return 0;
	if (ax <= ay)
		a = (short)((45L * ax * ay + 16L * ax * (ay - ax)) / (ay * ay));
	else
		a = 90 - (short)((45L * ay * ax + 16L * ay * (ax - ay)) / (ax * ax));
	if (x >= 0)
		return y >= 0 ? a : 180 - a;
	return y < 0 ? 180 + a : 360 - a;
}

/* bitmaps for ring(): the filled part and the track, one bit per pixel */
#define MK_W 4				/* words per row: up to 64 pixels */
#define MK_H 64
static unsigned short mk_fill[MK_W * MK_H], mk_track[MK_W * MK_H];

/* copy a one-bit bitmap to the screen in `color`: mode 1 (replace) also
 * paints its 0 bits white, mode 2 (transparent) leaves them alone */
static void blit_mask(unsigned short *m, short w, short h, short x, short y,
		      short mode, short color)
{
	MFDB src, dst;
	short pxy[8], colors[2];
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = m;
	src.fd_w = MK_W * 16;
	src.fd_h = h;
	src.fd_wdwidth = MK_W;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	colors[0] = color;
	colors[1] = 0;
	vrt_cpyfm(vh, mode, pxy, &src, &dst, colors);
}

/* a ring (or with thick >= rx, a disc) centred on cx,cy, rx pixels wide
 * and ry tall (pixels aren't square in medium resolution). The first pct
 * percent, clockwise from the top, is filled with colour fc; the rest is
 * the track: solid colour tc, or with `outline` a dotted pattern that
 * looks grey, for screens with too few colours for a real grey. It is worked out off screen
 * and copied over in one go; `opaque` also repaints the background of its
 * box white, so it replaces an older ring without first being erased. */
static void ring(short cx, short cy, short rx, short ry, short thick, short pct,
		 short fc, short tc, short outline, short opaque)
{
	long ri = rx - thick;
	long ro2 = (long)rx * rx + rx, ri2 = ri > 0 ? ri * ri - ri : -1;
	short dy, x, w = 2 * rx + 1, h = 2 * ry + 1, track = 0;
	if (rx < 1 || ry < 1 || w > MK_W * 16 || h > MK_H)
		return;
	memset(mk_fill, 0, sizeof(mk_fill));
	memset(mk_track, 0, sizeof(mk_track));
	for (dy = -ry; dy <= ry; dy++) {
		long y = (long)dy * rx / ry;	/* in the units of x */
		unsigned short *rf = mk_fill + (dy + ry) * MK_W;
		unsigned short *rt = mk_track + (dy + ry) * MK_W;
		for (x = -rx; x <= rx; x++) {
			long d2 = (long)x * x + y * y;
			short bx = x + rx;
			if (d2 > ro2 || d2 <= ri2)
				continue;
			if (pct > 0 && cw_angle(x, -y) * 10L < pct * 36L)
				rf[bx >> 4] |= 0x8000 >> (bx & 15);
			else if (!outline || ((x + dy) & 1)) {	/* outline: a dotted grey */
				rt[bx >> 4] |= 0x8000 >> (bx & 15);
				track = 1;
			}
		}
	}
	if (opaque && track) {
		blit_mask(mk_track, w, h, cx - rx, cy - ry, 1, tc);
		blit_mask(mk_fill, w, h, cx - rx, cy - ry, 2, fc);
	} else if (opaque) {
		blit_mask(mk_fill, w, h, cx - rx, cy - ry, 1, fc);
	} else {
		if (track)
			blit_mask(mk_track, w, h, cx - rx, cy - ry, 2, tc);
		blit_mask(mk_fill, w, h, cx - rx, cy - ry, 2, fc);
	}
}

/* the Claude "spark": eight rays around a centre */
static void spark(short x, short y, short r)
{
	short d = r * 7 / 10;
	short c = accent();
	line(x - r, y, x + r, y, c);
	line(x, y - r, x, y + r, c);
	line(x - d, y - d, x + d, y + d, c);
	line(x - d, y + d, x + d, y - d, c);
	line(x - r, y + 1, x + r, y + 1, c);
}

static void layout(void)
{
	short min_sb = 18 * cw, max_sb = 30 * cw;

	if (sb_user) {
		sb_w = sb_user;
		if (sb_w > ww - 34 * cw)
			sb_w = ww - 34 * cw;
		if (sb_w < 14 * cw)
			sb_w = 14 * cw;
	} else {
		sb_w = ww * 3 / 10;
		if (sb_w < min_sb)
			sb_w = min_sb;
		if (sb_w > max_sb)
			sb_w = max_sb;
		if (ww < 56 * cw)
			sb_w = 16 * cw;
		sb_w -= sb_w % cw;
	}

	row_h = ch + (ch >= 16 ? 4 : 2);
	title_h = ch + (ch >= 16 ? 8 : 4);
	input_h = ch + (ch >= 16 ? 10 : 6);

	px = wx + sb_w + 1;
	pw = ww - sb_w - 1;

	tx = px + cw;
	tw = pw - 2 * cw;
	ty = wy + title_h + 2;
	th = wh - title_h - input_h - 4;
	cols = tw / cw;
	rows = th / ch;
	if (cols < 10)
		cols = 10;
	if (rows < 1)
		rows = 1;

	nav_y = wy + title_h + 2;
	list_y = nav_y + (NNAV + 1) * row_h + 4;
	status_y = wy + wh - 2 * row_h;	/* two lines: connection and usage */
	list_rows = (status_y - 2 - (list_y + row_h)) / row_h;
	if (list_rows < 1)
		list_rows = 1;
}

static const char *nav_labels[NNAV] = { "+ New chat", "  Search", "  Chats", "  Code",
					 "  Projects", "  Artifacts" };

static short nav_active(short i)
{
	if (nav_pressed >= 0)	/* pressed: show it at once, on mouse-down */
		return i == nav_pressed;
	if (search_mode)	/* typing a search: Search is the active area */
		return i == 1;
	if (nav_pending >= 0)	/* chosen, waiting for the bridge's list */
		return i == nav_pending;
	if (i == 2)
		return !strcmp(list_kind, "CHATS");
	if (i == 3)
		return !strcmp(list_kind, "CODE");
	if (i == 4)
		return !strcmp(list_kind, "PROJECTS") || !strcmp(list_kind, "PROJECT");
	if (i == 5)
		return !strcmp(list_kind, "ARTIFACTS");
	if (i == 1)
		return !strcmp(list_kind, "SEARCH");
	return 0;
}

/* The status area at the bottom of the sidebar: the session usage ring,
 * then a dot for the connection (green or red; filled or hollow in
 * monochrome) with its text, and the usage below it. With `full` the
 * area has just been cleared; otherwise it is updated in place, without
 * erasing it first: the ring and dot are copied over their old pictures
 * and the text is written in replace mode, padded to its full width. */
static void draw_status(short full)
{
	static short last_tx = -1, last_sb = -1;
	short x = wx + cw, tx = x, n, room;
	/* pixels are twice as tall as wide where the system font is 8x8 */
	short sq = sys_ch >= 16 ? 1 : 2;
	short dry = ch / 3 / sq, drx = ch / 3;
	short colour = ncolors >= 16;
	/* the usage ring: two thirds of a line high, between the two lines */
	short rry = row_h * 2 / 3 > 4 ? row_h * 2 / 3 : 4, rrx = rry * sq;
	char t[80];

	if (dry < 1)
		dry = 1;
	if (usage_pct >= 0)
		tx = x + 2 * rrx + cw;
	if (!full && (tx != last_tx || sb_w != last_sb))	/* the layout moved */
		fill(wx, status_y - 1, wx + sb_w - 1, wy + wh - 1, 0);
	last_tx = tx;
	last_sb = sb_w;
	if (!full) {
		/* the strips the drawing below doesn't paint: between the ring
		   and the dot, and around the dot. A redraw of the list alone
		   doesn't reach them, so they could keep old text */
		short bottom = wy + wh - 1, gx = usage_pct >= 0 ? x + 2 * rrx + 1 : wx + 1;
		if (gx < tx)
			fill(gx, status_y - 1, tx - 1, bottom, 0);
		fill(tx, status_y - 1, tx + 2 * drx + cw / 2 - 1, status_y + row_h - 1, 0);
	}
	if (usage_pct >= 0) {
		short ry = rry, rx = rrx;
		short fc = usage_pct >= 90 ? accent() : colour ? 4 : 1;
		ring(x + rx, status_y + row_h - 1, rx, ry, rx / 6 + 1, usage_pct,
		     fc, colour ? 8 : 1, !colour, 1);
	}
	room = (wx + sb_w - 4 - tx) / cw;
	if (room > (short)sizeof(t) - 1)
		room = sizeof(t) - 1;
	if (ncolors >= 4)
		ring(tx + drx, status_y + row_h / 2, drx, dry, drx, 100,
		     online ? 3 : 2, 0, 0, 1);
	else
		ring(tx + drx, status_y + row_h / 2, drx, dry, online ? drx : 2, 100,
		     1, 0, 0, 1);

	vswr_mode(vh, 1);		/* replace: each character paints its cell */
	n = room - 2;
	if (n > 0) {
		short k = strlen(status);
		if (k > n)
			k = n;
		memcpy(t, status, k);
		memset(t + k, ' ', n - k);
		text(tx + 2 * drx + cw / 2, status_y + (row_h - ch) / 2, t, n, 0, 1);
	}
	if (usage_pct >= 0 && room > 0) {
		char *p = t;
		short k;
		strcpy(p, "Session ");
		p += 8;
		if (usage_pct >= 100)
			*p++ = '1';
		if (usage_pct >= 10)
			*p++ = '0' + usage_pct / 10 % 10;
		*p++ = '0' + usage_pct % 10;
		*p++ = '%';
		*p = 0;
		if (usage_rst[0] && strlen(t) + 3 + strlen(usage_rst) <= room) {
			strcat(t, ", ");
			strcat(t, usage_rst);
		}
		k = strlen(t);
		if (k > room)
			k = room;
		memset(t + k, ' ', room - k);
		text(tx, status_y + row_h + (row_h - ch) / 2, t, room, 0, 1);
	}
	vswr_mode(vh, 2);
}

static void draw_sidebar(void)
{
	short i, y, maxc = sb_w / cw - 2;
	short x = wx + cw;

	fill(wx, wy, wx + sb_w - 1, wy + wh - 1, 0);
	line(wx + sb_w, wy, wx + sb_w, wy + wh - 1, 1);

	/* brand */
	if (row_visible(wy, wy + title_h)) {
		spark(x + cw / 2, wy + title_h / 2, ch / 2 - 1);
		text(x + 2 * cw, wy + (title_h - ch) / 2, "ClaudeST", 8, 1, 1);
		line(wx, wy + title_h, wx + sb_w - 1, wy + title_h, 1);
	}

	/* navigation */
	for (i = 0; i < NNAV; i++) {
		y = nav_y + i * row_h;
		if (!row_visible(y, y + row_h))
			continue;
		if (nav_active(i)) {
			fill(wx + 4, y, wx + sb_w - 5, y + row_h - 2, 1);
			text(x, y + (row_h - ch) / 2 - 1, nav_labels[i], strlen(nav_labels[i]), 1, 0);
		} else {
			text(x, y + (row_h - ch) / 2 - 1, nav_labels[i], strlen(nav_labels[i]), i == 0 ? 1 : 0, 1);
		}
	}
	line(wx + 4, list_y - 3, wx + sb_w - 5, list_y - 3, 1);

	/* list header with paging arrows */
	if (row_visible(list_y, list_y + row_h)) {
		short n = strlen(list_title);
		if (n > maxc - 4)
			n = maxc - 4;
		text(x, list_y, list_title, n, 1, 1);
		if (list_top > 0)
			text(wx + sb_w - 4 * cw, list_y, "\001", 1, 0, 1);
		if (list_top + list_rows < nitems)
			text(wx + sb_w - 2 * cw, list_y, "\002", 1, 0, 1);
	}

	/* items */
	for (i = 0; i < list_rows && list_top + i < nitems; i++) {
		ITEM *it = &items[list_top + i];
		short n = strlen(it->label);
		y = list_y + (i + 1) * row_h;
		if (!row_visible(y, y + row_h))
			continue;
		if (n > maxc - (it->pinned ? 1 : 0))
			n = maxc - (it->pinned ? 1 : 0);
		if (it->heading) {
			/* a date heading, in grey like claude.ai's */
			text(x, y + (row_h - ch) / 2 - 1, it->label, n, 2, 1);
			continue;
		}
		{
			short cur = cur_id[0] && !strcmp(it->id, cur_id);
			short target = list_top + i == menu_target;
			short sel = menu_target >= 0 ? target : cur;
			if (sel)
				fill(wx + 4, y - 1, wx + sb_w - 5, y + row_h - 3, 1);
			else if (cur) {
				/* the open chat, while another item is targeted */
				short x1 = wx + 4, x2 = wx + sb_w - 5, y1 = y - 1, y2 = y + row_h - 3;
				line(x1, y1, x2, y1, 1);
				line(x2, y1, x2, y2, 1);
				line(x2, y2, x1, y2, 1);
				line(x1, y2, x1, y1, 1);
			}
			text_bidi(x, y + (row_h - ch) / 2 - 1, it->label, n, 0, sel ? 0 : 1,
				  wx + sb_w - 6 - (it->pinned ? cw + 4 : 0));
			/* a dot in the left margin, as claude.ai's: working (grey,
			   pulsing), waiting for you (yellow, a little bigger), or
			   updated since last read (blue) */
			if (it->state == 'W' || it->state == 'A' || it->unread) {
				short ry = sys_ch >= 16 ? 2 : 1, dx = wx + (cw + 2) / 2, dy = y + row_h / 2 - 1;
				short colour = ncolors >= 16;
				if (it->state == 'W') {
					if (dot_pulse)
						ring(dx, dy, 2, ry, 2, 100, sel ? 0 : colour ? 9 : 1, 0, 0, 0);
				} else if (it->state == 'A')
					ring(dx, dy, 3, ry + 1, 3, 100, sel ? 0 : colour ? 6 : 1, 0, 0, 0);
				else
					ring(dx, dy, 2, ry, 2, 100, sel ? 0 : colour ? 4 : accent(), 0, 0, 0);
			}
			if (it->pinned) {
				/* a small filled diamond: pinned / starred */
				short cx = wx + sb_w - cw - 2, cy = y + row_h / 2 - 1, k;
				for (k = 0; k <= 3; k++) {
					line(cx - 3 + k, cy - k, cx + 3 - k, cy - k, sel ? 0 : accent());
					line(cx - 3 + k, cy + k, cx + 3 - k, cy + k, sel ? 0 : accent());
				}
			}
		}
		if (list_top + i == lcur) {
			short x1 = wx + 3, x2 = wx + sb_w - 4, y1 = y - 2, y2 = y + row_h - 2;
			line(x1, y1, x2, y1, 1);
			line(x2, y1, x2, y2, 1);
			line(x2, y2, x1, y2, 1);
			line(x1, y2, x1, y1, 1);
		}
	}
	if (nitems == 0) {
		/* while loading, the bridge's progress ("Looking in chat 3 of 40") */
		const char *e = !list_loading ? "(empty)" : notice[0] ? notice : "Loading...";
		short n = strlen(e);
		if (n > maxc)
			n = maxc;
		y = list_y + row_h;
		text(x, y + (row_h - ch) / 2 - 1, e, n, 2, 1);
	}

	line(wx, status_y - 2, wx + sb_w - 1, status_y - 2, 1);
	draw_status(1);
}

static const char *model_label(void)
{
	short i;
	for (i = 0; i < nmodel; i++)
		if (!strcmp(mod_id[i], cur_model))
			return mod_lab[i];
	return cur_model;
}

static const char *effort_label(void)
{
	short i;
	for (i = 0; i < neffort; i++)
		if (!strcmp(eff_id[i], cur_effort))
			return eff_lab[i];
	return cur_effort;
}

/* the text of the model chip, e.g. "Opus 5.5 \xfa Medium \x02"; its length */
static short model_chip(char *out)
{
	if (!nmodel || !cur_model[0])
		return 0;
	strlcpy_(out, model_label(), 20);
	if (!strcmp(out, "Default model"))
		out[7] = 0;			/* "Default" */
	if (cur_effort[0] && neffort) {
		/* short names on the chip; the menu spells them out */
		const char *e = !strcmp(cur_effort, "medium") ? "Med" :
				!strcmp(cur_effort, "xhigh") ? "XHigh" : effort_label();
		strcat(out, " \xfa ");
		strcat(out, e);
	}
	strcat(out, " \x02");
	return strlen(out);
}

static void draw_title(void)
{
	short n = strlen(chat_title), maxc = pw / cw - 2, xr = px + pw;
	short ty0 = wy + (title_h - ch) / 2;
	char chip[40];
	short cl = model_chip(chip), scw, sch, big = sys_ch >= 16;

	fill(px, wy, px + pw - 1, wy + title_h, 0);
	/* the model chip ("Opus 5.5 · Med") at the right, in the small system
	 * font (8x8 in ST high, 6x6 in ST medium); the title keeps at least 16
	 * characters, or the chip goes */
	vst_font(vh, 1);
	vst_height(vh, big ? 6 : 4, &scw, &sch);
	chip_x0 = chip_x1 = 0;
	if (cl && maxc - ((cl + 1) * scw + cw) / cw < 16)
		cl = 0;
	if (cl) {
		short y1 = wy + (title_h - sch) / 2 - 2, y2 = y1 + sch + 3;
		chip_x1 = px + pw - cw / 2 - 1;
		chip_x0 = chip_x1 - (cl + 1) * scw;
		line(chip_x0, y1, chip_x1, y1, 1);
		line(chip_x1, y1, chip_x1, y2, 1);
		line(chip_x1, y2, chip_x0, y2, 1);
		line(chip_x0, y2, chip_x0, y1, 1);
		text(chip_x0 + scw / 2, y1 + 2, chip, cl, 0, 1);
		xr = chip_x0 - cw;
		maxc -= ((cl + 1) * scw + cw + cw - 1) / cw;
	}
	font_reselect();
	title_cut = n > maxc;
	title_xr = xr;
	if (n > maxc)
		n = maxc;
	text_bidi(px + cw, ty0, chat_title, n, 1, 1, 0);
	{
		/* a notice from the bridge, or "thinking...", right-aligned */
		const char *t = notice[0] && !list_loading ? notice : busy ? "thinking..." : 0;
		short tl = t ? strlen(t) : 0;
		if (t && tl <= maxc - n - 1)	/* whole, or not at all */
			text(xr - (tl + 1) * cw, ty0, t, tl, 2, 1);
	}
	line(px, wy + title_h, px + pw - 1, wy + title_h, 1);
}

static void draw_text(short from_row)
{
	short r, y, x;

	if (from_row < 0)
		from_row = 0;
	if (from_row == 0)
		fill(px, wy + title_h + 1, px + pw - 1, ty - 1, 0);
	/* clear and draw row by row so streaming text doesn't flicker */
	for (r = from_row; r < rows; r++) {
		short li = top_line + r;
		LINE *l;
		y = ty + r * ch;
		if (!row_visible(y, y + ch))
			continue;
		fill(px, y, px + pw - 1, y + ch - 1, 0);
		if (li >= nlines)
			continue;
		l = &lines[li];
		x = tx;
		if ((l->kind & L_TYPE) == L_HEADER) {
			const char *name = "Claude";
			short n = 6;
			switch (l->role) {
			case 'U': name = "You"; n = 3; break;
			case 'K': name = "Artifact"; n = 8; break;
			case 'E': name = "Error"; n = 5; break;
			case 'I': name = "Info"; n = 4; break;
			}
			if (l->role == 'A') {
				spark(x + cw / 2, y + ch / 2, ch / 2 - 1);
				x += 2 * cw;
			}
			text(x, y, name, n, 1, 1);
			continue;
		}
		if (l->role == 'U') {
			line(x + cw / 2, y, x + cw / 2, y + ch - 1, accent());
			line(x + cw / 2 + 1, y, x + cw / 2 + 1, y + ch - 1, accent());
			x += 3 * cw;
		} else if (l->role == 'K') {
			line(x + cw / 2, y, x + cw / 2, y + ch - 1, 1);
		}
		{
			short fx = (l->kind & L_TYPE) == L_BOLD ? 1 : (l->kind & L_TYPE) == L_GREY ? 2 : 0;
			short rtl = (l->kind & L_RTL) != 0;
			short colour = (l->kind & L_TYPE) == L_CODE ? code_colour() : 1;
			const char *t = tb + l->off;
			short n = l->len, i, k, inl = (l->kind & L_INL) != 0;
			static char plain[BIDI_MAX];
			if (inl || memchr(t, MK_INL, n)) {
				if (rtl || bidi_has_rtl(t, n)) {	/* Hebrew: no marks */
					for (i = k = 0; i < n && k < BIDI_MAX; i++)
						if (t[i] != MK_INL)
							plain[k++] = t[i];
					t = plain;
					n = k;
				} else {			/* runs, inline code in red */
					for (i = 0; i <= n; i++) {
						if (i < n && t[i] != MK_INL)
							continue;
						if (i > 0)
							text(x, y, t, i, inl ? inl_fx() : fx, inl ? inl_colour() : colour);
						x += i * cw;
						if (i < n) {
							inl = !inl;
							t += i + 1;
							n -= i + 1;
							i = -1;
						}
					}
					continue;
				}
			}
			if (rtl || bidi_has_rtl(t, n)) {
				static char vis[BIDI_MAX];
				if (n > BIDI_MAX)
					n = BIDI_MAX;
				bidi_visual(t, n, rtl, vis);
				if (rtl)	/* right-aligned, like dir="rtl" */
					x = tx + cols * cw - n * cw;
				text(x, y, vis, n, fx, colour);
			} else {
				text(x, y, t, n, fx, colour);
			}
		}
	}
	y = ty + rows * ch;
	if (row_visible(y, wy + wh - input_h - 1))
		fill(px, y, px + pw - 1, wy + wh - input_h - 1, 0);
}

/* move the text area up by n lines on screen, if the window is fully visible */
static short blit_scroll(short n)
{
	short r[4], pxy[8];
	MFDB scr;

	if (n <= 0 || n >= rows)
		return 0;
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	if (r[0] != wx || r[1] != wy || r[2] != ww || r[3] != wh ||
	    wx < 0 || wy < 0 || wx + ww > scr_w || wy + wh > scr_h)
		return 0;
	memset(&scr, 0, sizeof(scr));
	pxy[0] = px;
	pxy[1] = ty + n * ch;
	pxy[2] = px + pw - 1;
	pxy[3] = ty + rows * ch - 1;
	pxy[4] = px;
	pxy[5] = ty;
	pxy[6] = px + pw - 1;
	pxy[7] = ty + (rows - n) * ch - 1;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	vs_clip(vh, 0, pxy);
	vro_cpyfm(vh, 3, pxy, &scr, &scr);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	return 1;
}

/* ---- a one-line text editor, shared by the reply line and dialogs ---- */

static u8 key_char(short kstate, short kr);

static short is_space(char c)
{
	return c == ' ';
}

/* Handles a key for the text in buf (len characters, cursor at *cur).
 * Returns 1 if the key was used. Arrows move the cursor (with Shift: to
 * the start/end; with Control: by word), in screen direction when the
 * text is Hebrew. Backspace/Delete remove, other keys insert. */
static short field_clip(int what, char *buf, short *len, short *cur, short max);
static void reply_clip(int what);

static short edit_key(char *buf, short *len, short *cur, short max, short kstate, short kr)
{
	u8 ascii = kr & 0xff, scan = (kr >> 8) & 0xff, c;
	short shift = kstate & (K_LSHIFT | K_RSHIFT);
	short ctrl = key_ctrl(kstate, kr) || scan == 0x73 || scan == 0x74;
	short dir = 0;

	if (scan == 0x4b || scan == 0x73)
		dir = -1;
	else if (scan == 0x4d || scan == 0x74)
		dir = 1;
	if (dir) {
		if (bidi_is_rtl(buf, *len))	/* Hebrew: left goes forward */
			dir = -dir;
		if (shift)
			*cur = dir < 0 ? 0 : *len;
		else if (ctrl) {
			short p = *cur;
			if (dir < 0) {
				while (p > 0 && is_space(buf[p - 1]))
					p--;
				while (p > 0 && !is_space(buf[p - 1]))
					p--;
			} else {
				while (p < *len && !is_space(buf[p]))
					p++;
				while (p < *len && is_space(buf[p]))
					p++;
			}
			*cur = p;
		} else {
			*cur += dir;
			if (*cur < 0)
				*cur = 0;
			if (*cur > *len)
				*cur = *len;
		}
		return 1;
	}
	if (ascii == 0x08) {			/* Backspace */
		if (*cur > 0) {
			memmove(buf + *cur - 1, buf + *cur, *len - *cur);
			(*len)--;
			(*cur)--;
		}
		return 1;
	}
	if (ascii == 0x7f || scan == 0x53) {	/* Delete */
		if (*cur < *len) {
			memmove(buf + *cur, buf + *cur + 1, *len - *cur - 1);
			(*len)--;
		}
		return 1;
	}
	c = key_char(kstate, kr);
	if (c) {
		if (*len < max) {
			memmove(buf + *cur + 1, buf + *cur, *len - *cur);
			buf[*cur] = c;
			(*len)++;
			(*cur)++;
		}
		return 1;
	}
	return 0;
}

/* Draws the editable text in the area from x (cols characters wide) and
 * the cursor; scrolls *start to keep the cursor visible. Hebrew text is
 * shown right to left and right-aligned. */
static void edit_draw(const char *buf, short len, short cur, short *start,
		      short x, short y, short cols)
{
	static char vis[BIDI_MAX];
	static short pos[BIDI_MAX];
	static u8 odd[BIDI_MAX];
	short n, c, rtl, x0, xc;

	if (cols > BIDI_MAX)
		cols = BIDI_MAX;
	if (cur < *start)
		*start = cur;
	if (cur > *start + cols - 1)
		*start = cur - (cols - 1);
	if (*start > 0 && len - *start < cols - 1)
		*start = len - (cols - 1) > 0 ? len - (cols - 1) : 0;
	n = len - *start;
	if (n > cols)
		n = cols;
	c = cur - *start;

	if (n == 0) {
		fill(x, y, x + 1, y + ch - 1, 1);
		return;
	}
	rtl = bidi_is_rtl(buf, len);
	if (!rtl && !bidi_has_rtl(buf + *start, n)) {
		text(x, y, buf + *start, n, 0, 1);
		xc = x + c * cw;
	} else {
		bidi_visual_map(buf + *start, n, rtl, vis, pos, odd);
		x0 = rtl ? x + cols * cw - n * cw : x;
		text(x0, y, vis, n, 0, 1);
		/* the cursor sits before character c, on its leading side */
		if (c < n)
			xc = x0 + (odd[c] ? pos[c] + 1 : pos[c]) * cw;
		else
			xc = x0 + (odd[n - 1] ? pos[n - 1] : pos[n - 1] + 1) * cw;
	}
	if (xc > x)
		xc--;
	fill(xc, y, xc + 1, y + ch - 1, 1);
}

static void draw_input(void)
{
	short badge = hebrew_kbd;
	short y = wy + wh - input_h;
	short x1 = px + cw / 2, x2 = px + pw - cw / 2 - 1;
	short y1 = y + 2, y2 = wy + wh - 3;
	const char *prompt = search_mode ? "Find: " : "> ";
	short pl = strlen(prompt);
	short avail = (x2 - x1) / cw - pl - 2 - (hebrew_kbd ? 3 : 0);
	short tyy = y1 + (y2 - y1 - ch) / 2 + 1;
	short tx0 = x1 + cw / 2;

	fill(px, y, px + pw - 1, wy + wh - 1, 0);
	line(px, y, px + pw - 1, y, 1);
	line(x1, y1, x2, y1, 1);
	line(x2, y1, x2, y2, 1);
	line(x2, y2, x1, y2, 1);
	line(x1, y2, x1, y1, 1);
	text(tx0, tyy, prompt, pl, 1, 1);
	if (natt && !search_mode) {
		/* what is attached: [NAME.TXT] or [3 files], before the text */
		char a[20];
		short n;
		if (natt == 1) {
			strcpy(a, "[");
			strcat(a, att_name[0]);
			strcat(a, "] ");
		} else {
			*put_num(a + 1, natt) = 0;
			a[0] = '[';
			strcat(a, " files] ");
		}
		n = strlen(a);
		if (n < avail - 8) {
			text(tx0 + pl * cw, tyy, a, n, 1, 1);
			tx0 += n * cw;
			avail -= n;
		}
	}
	if (inlen == 0 && !search_mode) {
		const char *ph = busy ? "Claude is replying..." : natt ? "Return sends it" : "Reply to Claude...";
		short n = strlen(ph);
		if (n > avail)
			n = avail;
		text(tx0 + pl * cw, tyy, ph, n, 2, 1);
		fill(tx0 + pl * cw, tyy, tx0 + pl * cw + 1, tyy + ch - 1, 1);
	} else {
		edit_draw(input, inlen, in_cur, &in_start, tx0 + pl * cw, tyy,
			  avail + 1);
	}
	if (badge) {
		/* "HE": the keyboard types Hebrew */
		short bx = x2 - 3 * cw - cw / 2;
		fill(bx - 2, y1 + 2, bx + 2 * cw + 1, y2 - 2, 1);
		text(bx, tyy, "HE", 2, 1, 0);
	}
}

static short rc_intersect(const short *a, short *b)
{
	/* a, b: x,y,w,h. result in b */
	short x1 = a[0] > b[0] ? a[0] : b[0];
	short y1 = a[1] > b[1] ? a[1] : b[1];
	short x2 = (a[0] + a[2] < b[0] + b[2]) ? a[0] + a[2] : b[0] + b[2];
	short y2 = (a[1] + a[3] < b[1] + b[3]) ? a[1] + a[3] : b[1] + b[3];
	b[0] = x1;
	b[1] = y1;
	b[2] = x2 - x1;
	b[3] = y2 - y1;
	return x2 > x1 && y2 > y1;
}

/* redraw the parts in mask that intersect area, honouring the rect list */
static void redraw(short mask, short ax, short ay, short aw, short ah)
{
	short r[4], area[4], scr[4];
	short text_from = mask & D_TEXT ? text_dirty_row : 0;
	short keep_id = cur_font_id, keep_pt = cur_font_pt, keep_cw = cw, keep_ch = ch;

	if (win < 0 || win_icon)
		return;
	area[0] = ax;
	area[1] = ay;
	area[2] = aw;
	area[3] = ah;
	scr[0] = 0;
	scr[1] = 0;
	scr[2] = scr_w;
	scr[3] = scr_h;
	if (!rc_intersect(scr, area))
		return;

	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	/* the window's own font, whatever the VDI was left with: the font
	   selector's sample, or the font being measured. Under NVDI another
	   id is another font (it drew the status line in Symbol) */
	set_font(font_id, font_pt);
	vswr_mode(vh, 2);	/* transparent text over our own fills */
	vsf_perimeter(vh, 0);
	vst_alignment(vh, 0, 5);
	wind_get(win, WF_FIRSTXYWH, &r[0], &r[1], &r[2], &r[3]);
	while (r[2] && r[3]) {
		if (rc_intersect(area, r)) {
			clip[0] = r[0];
			clip[1] = r[1];
			clip[2] = r[0] + r[2] - 1;
			clip[3] = r[1] + r[3] - 1;
			vs_clip(vh, 1, clip);
			if (mask & (D_SIDEBAR | D_LIST))
				draw_sidebar();
			else if (mask & D_STATUS)
				draw_status(0);
			if (mask & D_TITLE)
				draw_title();
			if (mask & D_TEXT)
				draw_text(text_from);
			if (mask & D_INPUT)
				draw_input();
		}
		wind_get(win, WF_NEXTXYWH, &r[0], &r[1], &r[2], &r[3]);
	}
	vs_clip(vh, 0, clip);
	set_font(keep_id, keep_pt);	/* and back to what the caller had */
	cw = keep_cw;
	ch = keep_ch;
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
}

static void update_slider(void)
{
	short size = 1000, pos = 0;
	if (nlines > rows) {
		size = (short)((long)rows * 1000 / nlines);
		pos = (short)((long)top_line * 1000 / (nlines - rows));
	}
	if (size < 30)
		size = 30;
	if (size != last_slider_size) {
		wind_set(win, WF_VSLSIZE, size, 0, 0, 0);
		last_slider_size = size;
	}
	if (pos != last_slider_pos) {
		wind_set(win, WF_VSLIDE, pos, 0, 0, 0);
		last_slider_pos = pos;
	}
}

static void icon_window(GRECT *area);

static void flush_dirty(void)
{
	if (win_icon) {
		/* nothing of it shows: all of it is drawn when it comes back */
		pending_scroll = 0;
		dirty = 0;
		text_dirty_row = 0x7fff;
		icon_window(0);
		return;
	}
	if (pending_scroll) {
		if (text_dirty_row > 0 && blit_scroll(pending_scroll)) {
			short keep = rows - pending_scroll - 1;
			if (keep < text_dirty_row)
				text_dirty_row = keep < 0 ? 0 : keep;
		} else {
			text_dirty_row = 0;
		}
		pending_scroll = 0;
		dirty |= D_TEXT;
	}
	if (!dirty)
		return;
	if ((dirty & D_ALL) == D_ALL) {
		text_dirty_row = 0;
		redraw(D_ALL, wx, wy, ww, wh);
	} else {
		/* the menu and list only: the header never changes, and the
		 * status area has its own update below */
		if (dirty & D_SIDEBAR)
			redraw(D_SIDEBAR, wx, wy + title_h + 1, sb_w, status_y - 2 - (wy + title_h + 1));
		else if (dirty & D_LIST)
			redraw(D_LIST, wx, list_y, sb_w, status_y - 2 - list_y);
		if (dirty & D_STATUS)
			redraw(D_STATUS, wx, status_y - 1, sb_w, wy + wh - status_y + 1);
		if (dirty & D_TITLE)
			redraw(D_TITLE, px, wy, pw, title_h + 1);
		if (dirty & D_TEXT) {
			short r0 = text_dirty_row < rows ? text_dirty_row : rows;
			short y0 = ty + r0 * ch;
			redraw(D_TEXT, px, y0, pw, wy + wh - input_h - y0);
		}
		if (dirty & D_INPUT)
			redraw(D_INPUT, px, wy + wh - input_h, pw, input_h);
	}
	if (dirty & D_TEXT)
		update_slider();
	dirty = 0;
	text_dirty_row = 0x7fff;
}

/* ------------------------------------------------------------------ */
/* protocol: lines from the bridge                                     */
/* ------------------------------------------------------------------ */

static short split(char *s, char **f, short max)
{
	short n = 0;
	f[n++] = s;
	while (*s && n < max) {
		if (*s == '\t') {
			*s = 0;
			f[n++] = s + 1;
		}
		s++;
	}
	return n;
}

static void item_flags(ITEM *it, const char *q)
{
	for (; *q; q++) {
		if (*q == 'P')
			it->pinned = 1;
		else if (*q == 'U')
			it->unread = 1;
		else if (*q == 'W' || *q == 'A')
			it->state = *q;
	}
}

static void redraw_dot(short i);

static void handle_line(char *s)
{
	char *f[4];
	short n = split(s, f, 4);
	char c = f[0][0];

	if (!online) {
		online = 1;
		dirty |= D_STATUS;
	}
	last_rx = tos_ms();
	ping_at = 0;		/* the gateway is there */
	if (f[0][1] != 0)
		return;		/* all bridge commands are one letter */

	switch (c) {
	case 'S':			/* S <status> : the connection to claude.ai */
		if (n > 1 && strcmp(status, f[1])) {
			strlcpy_(status, f[1], sizeof(status));
			dirty |= D_STATUS;
		}
		list_loading = 0;
		break;
	case 'u':			/* u [<percent> <reset>] : session usage; bare: none */
		{
			short v = -1;
			const char *q = n > 1 ? f[1] : "";
			if (*q) {
				for (v = 0; *q >= '0' && *q <= '9'; q++)
					v = v * 10 + (*q - '0');
				if (v > 100)
					v = 100;
			}
			if (v != usage_pct || strcmp(usage_rst, n > 2 ? f[2] : "")) {
				usage_pct = v;
				strlcpy_(usage_rst, n > 2 ? f[2] : "", sizeof(usage_rst));
				dirty |= D_STATUS;
			}
		}
		break;
	case 'N':			/* N <text> : a passing notice */
		strlcpy_(notice, n > 1 ? f[1] : "", sizeof(notice));
		notice_until = ticks + 125;	/* ~5 s at most; "" clears it */
		dirty |= D_TITLE | (list_loading ? D_SIDEBAR : 0);
		break;
	case 'L':			/* L <kind> <title> : start a list */
		/* a list asked for before Search was picked: keep the pane empty */
		drop_list = search_mode && !(n > 1 && !strcmp(f[1], "SEARCH"));
		if (drop_list)
			break;
		if (n > 1)
			strlcpy_(list_kind, f[1], sizeof(list_kind));
		strlcpy_(list_title, n > 2 ? f[2] : "", sizeof(list_title));
		nitems = 0;
		list_top = 0;
		lcur = -1;
		menu_target = -1;
		nav_pending = -1;
		list_loading = 0;
		dirty |= D_SIDEBAR;
		break;
	case 'I':			/* I <id> <label> */
		if (!drop_list && nitems < MAXITEMS && n > 2) {
			strlcpy_(items[nitems].id, f[1], sizeof(items[0].id));
			strlcpy_(items[nitems].label, f[2], sizeof(items[0].label));
			items[nitems].pinned = items[nitems].unread = items[nitems].state = 0;
			if (n > 3)		/* flags: P pinned, U unread, W working, A waits */
				item_flags(&items[nitems], f[3]);
			items[nitems].heading = 0;
			nitems++;
		}
		break;
	case 'd':			/* d <id> <flags> : an item's dot changed */
		if (n > 1) {
			short i;
			for (i = 0; i < nitems; i++)
				if (!items[i].heading && !strcmp(items[i].id, f[1])) {
					items[i].pinned = items[i].unread = items[i].state = 0;
					item_flags(&items[i], n > 2 ? f[2] : "");
					redraw_dot(i);
					break;
				}
		}
		break;
	case 'X':			/* X <label> : a heading in the list ("Today") */
		if (!drop_list && nitems < MAXITEMS && n > 1) {
			items[nitems].id[0] = 0;
			strlcpy_(items[nitems].label, f[1], sizeof(items[0].label));
			items[nitems].pinned = items[nitems].unread = items[nitems].state = 0;
			items[nitems].heading = 1;
			nitems++;
		}
		break;
	case 'F':			/* F <name>: where to save the artifact? */
		if (n > 1 && save_id[0])
			choose_save_file(f[1]);
		break;
	case 'D':			/* D <hex> [<size>]: a piece of the file being saved;
				   the first one says the whole file's size (1.49's bridge) */
		if (save_fd >= 0 && n > 1)
			save_chunk(f[1], n > 2 ? atol(f[2]) : 0);
		break;
	case 'G':			/* G <size>: the file is complete */
		finish_save();
		break;
	case 'Q':			/* start a picker list */
		npick = 0;
		break;
	case 'J':			/* J <id> <label> : picker entry */
		if (npick < PICKMAX && n > 2) {
			strlcpy_(pick_id[npick], f[1], sizeof(pick_id[0]));
			strlcpy_(pick_label[npick], f[2], sizeof(pick_label[0]));
			npick++;
		}
		break;
	case 'W':			/* show the picker */
		show_picker();
		break;
	case 'E':			/* end of list */
		dirty |= D_LIST;
		break;
	case 'C':			/* C <id> : current chat (for highlight) */
		/* just started, the pane is empty: don't highlight the chat
		 * the bridge kept from our last run */
		if (fresh)
			break;
		if (strcmp(cur_id, n > 1 ? f[1] : "")) {
			strlcpy_(cur_id, n > 1 ? f[1] : "", sizeof(cur_id));
			dirty |= D_LIST;
		}
		break;
	case 'T':			/* T <title> */
		if (strcmp(chat_title, n > 1 ? f[1] : "")) {
			strlcpy_(chat_title, n > 1 ? f[1] : "", sizeof(chat_title));
			dirty |= D_TITLE;
		}
		break;
	case 'R':			/* reset conversation pane */
		clear_text();
		break;
	case 'M':			/* M <role> : begin message */
		begin_message(n > 1 && f[1][0] ? f[1][0] : 'A');
		break;
	case 'P':			/* P <text> : append text */
		if (n > 1)
			append(f[1], strlen(f[1]));
		break;
	case 'H': {			/* H <text> : bold heading paragraph */
		char m = MK_BOLD;
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		append(&m, 1);
		if (n > 1)
			append(f[1], strlen(f[1]));
		append("\n", 1);
		break;
	}
	case 'g': {			/* g <text> : a grey line (a tool step) */
		char m = MK_GREY;
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		append(&m, 1);
		if (n > 1)
			append(f[1], strlen(f[1]));
		append("\n", 1);
		break;
	}
	case 'c': {			/* c <text> : a line of a code block */
		char m = MK_CODE;
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		append(&m, 1);
		if (n > 1)
			append(f[1], strlen(f[1]));
		append("\n", 1);
		break;
	}
	case 'B':			/* line break */
		append("\n", 1);
		break;
	case 'Z':			/* end of message */
		if (tlen > 0 && tb[tlen - 1] != '\n')
			append("\n", 1);
		break;
	case 'O':			/* start of the model and effort lists */
		nmodel = neffort = 0;
		break;
	case 'V':			/* V <id> <label> : a model */
		if (nmodel < NMODEL && n > 2) {
			strlcpy_(mod_id[nmodel], f[1], sizeof(mod_id[0]));
			strlcpy_(mod_lab[nmodel], f[2], sizeof(mod_lab[0]));
			nmodel++;
		}
		break;
	case 'U':			/* U <id> <label> : an effort level */
		if (neffort < NEFFORT && n > 2) {
			strlcpy_(eff_id[neffort], f[1], sizeof(eff_id[0]));
			strlcpy_(eff_lab[neffort], f[2], sizeof(eff_lab[0]));
			neffort++;
		}
		break;
	case 'K':			/* K <model> <effort> : the current choice */
		strlcpy_(cur_model, n > 1 ? f[1] : "", sizeof(cur_model));
		strlcpy_(cur_effort, n > 2 ? f[2] : "", sizeof(cur_effort));
		dirty |= D_TITLE;
		/* after (re)connecting, ask for the model saved in CLAUDEST.INF */
		if (!pref_sent) {
			pref_sent = 1;
			if (pref_model[0] && (strcmp(pref_model, cur_model) ||
					      strcmp(pref_effort, cur_effort)))
				tx_cmd("CHOOSE", pref_model, pref_effort);
		}
		break;
	case 'p':			/* p : the bridge answers PING */
		can_ping = 1;
		break;
	case 'a':			/* a <name> : the bridge turned this attachment down */
		if (n > 1) {
			short i;
			for (i = 0; i < natt; i++)
				if (!strcmp(att_name[i], f[1])) {
					for (; i < natt - 1; i++)
						strcpy(att_name[i], att_name[i + 1]);
					natt--;
					dirty |= D_INPUT;
					break;
				}
		}
		break;
	case 'Y':			/* Y 0|1 : busy */
		busy = n > 1 && f[1][0] == '1';
		dirty |= D_TITLE | D_INPUT;
		break;
	case 'A':			/* A <text> : alert box */
		flush_dirty();
		alert(1, "[1][%s][ OK ]", n > 1 ? f[1] : "");	/* wrapped, Hebrew in order */
		break;
	}
}

static void rx_byte(u8 c)
{
	if (c == '\n') {
		rxline[rxlen] = 0;
		handle_line(rxline);
		rxlen = 0;
	} else if (c != '\r') {
		if (rxlen < (short)sizeof(rxline) - 1)
			rxline[rxlen++] = c;
	}
}

static void ip_to_str(u32 ip, char *p)
{
	short i;
	for (i = 3; i >= 0; i--) {
		short v = (ip >> (i * 8)) & 0xff, d = 100;
		short started = 0;
		for (; d; d /= 10) {
			if (v / d || started || d == 1) {
				*p++ = '0' + v / d;
				started = 1;
			}
			v %= d;
		}
		if (i)
			*p++ = '.';
	}
	*p = 0;
}

static void tcp_failed(short code)
{
	char msg[64];
	strcpy(msg, "Offline: ");
	strlcpy_(msg + 9, sting_error(code), sizeof(msg) - 9);
	set_status(msg);
}

static void tcp_connect(void)
{
	char msg[64];
	short cn, r;

	next_connect = ticks + 375;	/* if this fails, try again in ~15 s */
	if (!sting_init()) {
		set_status("Offline: STinG not loaded");
		return;
	}
	if (!tcp_ip) {
		set_status("Type /connect <gateway IP>");
		return;
	}
	strcpy(msg, "Connecting to ");
	ip_to_str(tcp_ip, msg + 14);
	set_status(msg);
	cn = sting_open(tcp_ip, tcp_port);
	(void)r;
	if (cn < 0) {
		tcp_failed(cn);
		return;
	}
	/* the connection opens in the background: poll_link() asks STinG
	   (without waiting) each time round, so menus, the About box and
	   everything else keep working meanwhile */
	tcp_opening = cn;
	opening_until = ticks + 8 * 25;		/* ~8 s */
}

/* is the connection being opened established yet? */
static void tcp_check_open(void)
{
	short cn = tcp_opening, r = sting_wait_established(cn, 0);
	if (r == E_NORMAL) {
		tcp_opening = -1;
		tcp_cn = cn;
		rxlen = 0;
		set_status("Connected, loading...");
		send_hello();
		return;
	}
	/* refused or reset: at once; otherwise until the time is up */
	if (r == E_REFUSE || r == E_RRESET || r == E_EOF || ticks >= opening_until) {
		tcp_opening = -1;
		sting_close(cn);
		tcp_failed(r == E_NORMAL ? E_CNTIMEOUT : r);
	}
}

static void poll_link(void)
{
	static char buf[512];
	short budget;

	if (link == LINK_SERIAL) {
		budget = 3000;
		while (budget-- > 0 && Bconstat(DEV_AUX))
			rx_byte((u8)Bconin(DEV_AUX));
		return;
	}
	if (tcp_cn < 0) {
		if (tcp_opening >= 0)
			tcp_check_open();
		else if (ticks >= next_connect)
			tcp_connect();
		return;
	}
	for (budget = 8; budget > 0 && tcp_cn >= 0; budget--) {
		short i, n = sting_count(tcp_cn);
		if (n == E_NODATA || n == 0)
			break;
		if (n < 0) {
			tcp_lost(n);
			break;
		}
		if (n > (short)sizeof(buf))
			n = sizeof(buf);
		n = sting_read(tcp_cn, buf, n);
		if (n < 0) {
			tcp_lost(n);
			break;
		}
		for (i = 0; i < n; i++)
			rx_byte((u8)buf[i]);
	}
}

/* ---- settings: CLAUDEST.INF next to the program ---- */

static short parse_ip(const char *s, u32 *ip, u16 *port)
{
	u32 v = 0;
	short parts = 0, n = -1;
	for (;; s++) {
		if (*s >= '0' && *s <= '9') {
			n = (n < 0 ? 0 : n * 10) + (*s - '0');
			if (n > 255)
				return 0;
		} else if (*s == '.' || *s == ':' || *s == ' ' || *s == 0 || *s == '\r' || *s == '\n') {
			if (n < 0)
				return 0;
			v = (v << 8) | n;
			n = -1;
			if (++parts == 4)
				break;
			if (*s != '.')
				return 0;
		} else {
			return 0;
		}
	}
	*ip = v;
	if ((*s == ':' || *s == ' ') && port) {
		u16 p = 0;
		for (s++; *s >= '0' && *s <= '9'; s++)
			p = p * 10 + (*s - '0');
		if (p)
			*port = p;
	}
	return 1;
}

static void parse_config_line(const char *l)
{
	if (!memcmp(l, "tcp ", 4) && parse_ip(l + 4, &tcp_ip, &tcp_port))
		link = LINK_TCP;
	else if (!memcmp(l, "serial", 6))
		link = LINK_SERIAL;
	else if (!memcmp(l, "baud 4800", 9))
		baud = BAUD_4800;
	else if (!memcmp(l, "baud 9600", 9))
		baud = BAUD_9600;
	else if (!memcmp(l, "baud 19200", 10))
		baud = BAUD_19200;
	else if (!memcmp(l, "keyboard hebrew", 15))
		hebrew_kbd = 1;
	else if (!memcmp(l, "font ", 5)) {
		/* font <id> <points> <hebrew: 0 Atari, 1 ISO, 2 DOS> */
		short v[3] = { 1, 0, 0 }, k = 0;
		for (l += 5; *l && k < 3; k++) {
			while (*l == ' ')
				l++;
			v[k] = 0;
			while (*l >= '0' && *l <= '9')
				v[k] = v[k] * 10 + (*l++ - '0');
		}
		font_id = v[0] > 0 ? v[0] : 1;
		font_pt = v[1];
		hebfont = v[2] <= HEB_DOS ? v[2] : HEB_ATARI;
	}
	else if (!memcmp(l, "port ", 5) && l[5] >= '6' && l[5] <= '9')
		aux_port = l[5] - '0';
	else if (!memcmp(l, "model ", 6)) {
		/* model <id> [<effort>] */
		const char *e = l + 6;
		short k = 0;
		while (*e && *e != ' ' && k < (short)sizeof(pref_model) - 1)
			pref_model[k++] = *e++;
		pref_model[k] = 0;
		while (*e == ' ')
			e++;
		strlcpy_(pref_effort, e, sizeof(pref_effort));
	}
	else if (!memcmp(l, "sidebar ", 8)) {
		short v = 0;
		for (l += 8; *l >= '0' && *l <= '9'; l++)
			v = v * 10 + (*l - '0');
		sb_user = v;
	}
}

/* the AERA INF format (the guidelines, 6.1): "; notes", [SECTION] lines
 * and NAME=value, names in capitals but read in any case. Lines it
 * doesn't know are kept and written back as they were. */
#define INF_KEEP 1024
static char inf_keep[INF_KEEP];
static short inf_keep_len;

static void inf_path(char *out, short size, const char *name)
{
	strlcpy_(out, home_dir, size);
	strlcpy_(out + strlen(out), name, size - strlen(out));
}

/* NAME=... in l: 1, with *v at the value */
static short inf_key(const char *l, const char *name, const char **v)
{
	while (*name) {
		char c = *l++;
		if (c >= 'a' && c <= 'z')
			c -= 32;
		if (c != *name++)
			return 0;
	}
	while (*l == ' ' || *l == '\t')
		l++;
	if (*l++ != '=')
		return 0;
	while (*l == ' ' || *l == '\t')
		l++;
	*v = l;
	return 1;
}

static u16 inf_num(const char **v)
{
	u16 n = 0;
	while (**v == ' ' || **v == ',')
		(*v)++;
	while (**v >= '0' && **v <= '9')
		n = n * 10 + (*(*v)++ - '0');
	return n;
}

/* one NAME=value line: 1 if ClaudeST knows the name */
static short inf_setting(const char *l)
{
	const char *v;
	if (inf_key(l, "LINK", &v))
		link = (*v == 's' || *v == 'S') ? LINK_SERIAL : LINK_TCP;
	else if (inf_key(l, "HOST", &v)) {
		if (!parse_ip(v, &tcp_ip, 0))
			tcp_ip = 0;
	} else if (inf_key(l, "PORT", &v)) {
		u16 n = inf_num(&v);
		if (n)
			tcp_port = n;
	} else if (inf_key(l, "BAUD", &v)) {
		u16 n = inf_num(&v);
		baud = n == 4800 ? BAUD_4800 : n == 9600 ? BAUD_9600 : BAUD_19200;
	} else if (inf_key(l, "SERIALPORT", &v)) {
		u16 n = inf_num(&v);
		aux_port = n >= 6 && n <= 9 ? n : 0;
	} else if (inf_key(l, "HEBREW", &v))
		hebrew_kbd = *v == '1';
	else if (inf_key(l, "FONT", &v)) {
		u16 id = inf_num(&v);
		font_id = id ? id : 1;
		font_pt = inf_num(&v);
	} else if (inf_key(l, "HEBFONT", &v)) {
		u16 n = inf_num(&v);
		hebfont = n <= HEB_DOS ? n : HEB_ATARI;
	} else if (inf_key(l, "SIDEBAR", &v))
		sb_user = inf_num(&v);
	else if (inf_key(l, "MODEL", &v)) {
		short k = 0;
		while (*v && *v != ' ' && k < (short)sizeof(pref_model) - 1)
			pref_model[k++] = *v++;
		pref_model[k] = 0;
	} else if (inf_key(l, "EFFORT", &v))
		strlcpy_(pref_effort, v, sizeof(pref_effort));
	else
		return 0;
	return 1;
}

static void inf_line(const char *l)
{
	short n;
	if (*l == ';' || *l == '[')
		return;			/* notes and sections are written afresh */
	if (inf_setting(l))
		return;
	if (strchr(l, '=')) {		/* someone else's setting: keep it */
		n = strlen(l);
		if (inf_keep_len + n + 2 < INF_KEEP) {
			memcpy(inf_keep + inf_keep_len, l, n);
			inf_keep_len += n;
			inf_keep[inf_keep_len++] = '\r';
			inf_keep[inf_keep_len++] = '\n';
		}
		return;
	}
	parse_config_line(l);		/* the format before 1.38: "tcp 1.2.3.4 2323" */
}

static long inf_open(void)
{
	char path[160];
	long fd;
	inf_path(path, sizeof(path), "CLAUDEST.INF");
	fd = Fopen(path, 0);
	if (fd < 0) {			/* a save cut short after the old file went */
		inf_path(path, sizeof(path), "CLAUDEST.IN$");
		fd = Fopen(path, 0);
	}
	if (fd < 0)			/* the current folder, as before 1.38 */
		fd = Fopen("CLAUDEST.INF", 0);
	if (fd < 0)
		fd = Fopen("ECLAUDE.INF", 0);	/* settings from before the renames: */
	if (fd < 0)
		fd = Fopen("CLAUDE.INF", 0);	/* EClaude's, then Claude ST's */
	return fd;
}

static void load_config(void)
{
	char buf[2048], *p, *l;
	long fd = inf_open();
	long n;
	if (fd < 0)
		return;
	n = Fread((short)fd, sizeof(buf) - 1, buf);
	Fclose((short)fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	inf_keep_len = 0;
	for (l = p = buf; ; p++) {
		if (*p == '\r' || *p == '\n' || *p == 0) {
			char end = *p;
			*p = 0;
			while (*l == ' ' || *l == '\t')
				l++;
			if (*l)
				inf_line(l);
			if (!end)
				break;
			l = p + 1;
		}
	}
}

static char *put_num(char *p, u16 v)
{
	short d = 10000, started = 0;
	for (; d; d /= 10) {
		if (v / d || started || d == 1) {
			*p++ = '0' + v / d;
			started = 1;
		}
		v %= d;
	}
	return p;
}

static char *inf_put(char *p, const char *note, const char *name, const char *val)
{
	strcpy(p, "; ");
	strcat(p, note);
	strcat(p, "\r\n");
	strcat(p, name);
	strcat(p, "=");
	strcat(p, val);
	strcat(p, "\r\n\r\n");
	return p + strlen(p);
}

static char *inf_put_num(char *p, const char *note, const char *name, u16 v)
{
	char n[8];
	*put_num(n, v) = 0;
	return inf_put(p, note, name, n);
}

/* name (next to the program) in one write: NAME.IN$ first, then renamed,
   so a reset in between never leaves half a file (the guidelines, 2.3) */
static void safe_save(const char *name, const char *tmpname, const char *data, long len)
{
	char path[160], tmp[160];
	long fd;
	inf_path(path, sizeof(path), name);
	inf_path(tmp, sizeof(tmp), tmpname);
	fd = Fcreate(tmp, 0);
	if (fd >= 0) {
		long n = Fwrite((short)fd, len, data);
		Fclose((short)fd);
		if (n == len) {
			Fdelete(path);
			if (Frename(tmp, path) == 0)
				return;
		}
		Fdelete(tmp);
	}
	fd = Fcreate(path, 0);		/* no room for both: as before */
	if (fd >= 0) {
		Fwrite((short)fd, len, data);
		Fclose((short)fd);
	}
}

static void save_config(void)
{
	static char text[2048 + INF_KEEP];
	char v[24], *p = text;

	strcpy(p, "; ClaudeST settings, written by CLAUDEST.PRG. Lines starting with ; are notes.\r\n"
		  "; Most of these are set in Options > Settings; see the guide.\r\n"
		  "; 1 means on, 0 means off.\r\n\r\n[OPTIONS]\r\n\r\n");
	p += strlen(p);
	p = inf_put(p, "how ClaudeST reaches its gateway: network (STinG) or serial", "LINK",
		    link == LINK_TCP ? "network" : "serial");
	v[0] = 0;
	if (tcp_ip)
		ip_to_str(tcp_ip, v);
	p = inf_put(p, "network: the gateway's IP address (the Pi running the ClaudeST bridge)", "HOST", v);
	p = inf_put_num(p, "network: the gateway's port, 2323 unless you changed it on the Pi", "PORT", tcp_port);
	p = inf_put_num(p, "serial: the speed, 4800, 9600 or 19200 (the bridge's --baud)", "BAUD",
			baud == BAUD_4800 ? 4800 : baud == BAUD_9600 ? 9600 : 19200);
	p = inf_put_num(p, "serial: 0 = the Modem port TOS picks; 6-9 = that serial device (Bconmap)",
			"SERIALPORT", aux_port);
	p = inf_put_num(p, "1 = start with the Hebrew keyboard (F10 switches)", "HEBREW", hebrew_kbd);
	{
		char *q = put_num(v, font_id);
		*q++ = ',';
		*put_num(q, font_pt) = 0;
	}
	p = inf_put(p, "text font: GDOS font id (1 = system font), size (system font: 0, 8 small, 16 large)",
		    "FONT", v);
	p = inf_put_num(p, "the font's Hebrew letters: 0 = Atari places, 1 = ISO-8859-8, 2 = DOS 862 (Options > Font)",
			"HEBFONT", hebfont);
	p = inf_put_num(p, "the sidebar's width in pixels, 0 = ClaudeST's choice (drag its edge)", "SIDEBAR", sb_user);
	p = inf_put(p, "the model for new chats, empty = claude.ai's default (F9)", "MODEL", pref_model);
	p = inf_put(p, "its effort level, empty = the model's default", "EFFORT", pref_effort);
	if (inf_keep_len) {
		strcpy(p, "; settings this version of ClaudeST doesn't use, kept as they were\r\n");
		p += strlen(p);
		memcpy(p, inf_keep, inf_keep_len);
		p += inf_keep_len;
	}
	*p = 0;

	safe_save("CLAUDEST.INF", "CLAUDEST.IN$", text, p - text);
}

static void use_link(short l)
{
	if (link == LINK_TCP && tcp_cn >= 0)
		sting_close(tcp_cn);
	if (tcp_opening >= 0)
		sting_close(tcp_opening);
	tcp_cn = tcp_opening = -1;
	link = l;
	online = 0;
	rxlen = 0;
	if (link == LINK_SERIAL) {
		serial_open();
		set_status("Waiting for bridge...");
		send_hello();
	} else {
		serial_close();
		next_connect = 0;	/* connect on the next tick */
	}
	menu_check_baud();
	save_config();
}

/* ------------------------------------------------------------------ */
/* user actions                                                        */
/* ------------------------------------------------------------------ */

static void set_window_name(void)
{
	static char name[] = " ClaudeST ";
	wind_set_str(win, WF_NAME, name);
}

static void do_new_chat(void)
{
	search_mode = 0;
	cur_id[0] = 0;
	strcpy(chat_title, "New chat");
	clear_text();
	begin_message('I');
	append("How can I help you today?", 25);
	tx_cmd("NEW", 0, 0);
	fresh = 0;
	dirty |= D_ALL;
}

/* switching to another area of the sidebar: empty the conversation pane
 * and start afresh, so nothing typed next lands in the previous chat */
static void clear_context(const char *title, const char *hint)
{
	cur_id[0] = 0;
	strlcpy_(chat_title, title, sizeof(chat_title));
	clear_text();
	begin_message('I');
	append(hint, strlen(hint));
	tx_cmd("NEW", "QUIET", 0);
	fresh = 0;
	dirty |= D_ALL;
}

/* the Account page: plan, usage and when it resets, account details */
static void do_account(void)
{
	search_mode = 0;
	clear_context("Account", "Asking claude.ai about your account...");
	tx_cmd("ACCOUNT", 0, 0);
}

static void do_search(void)
{
	search_mode = 1;
	nav_pending = -1;
	inlen = in_cur = in_start = 0;
	input[0] = 0;
	/* no results yet: empty the list until a search is run */
	strlcpy_(list_kind, "SEARCH", sizeof(list_kind));
	strlcpy_(list_title, "Results", sizeof(list_title));
	nitems = 0;
	list_top = 0;
	lcur = -1;
	menu_target = -1;
	dirty |= D_SIDEBAR;
	clear_context("Search", "Type words from a chat's title in the box below "
		      "and press Return. Matching chats appear on the left.");
}

static void do_list(const char *kind)
{
	search_mode = 0;
	nav_pending = !strcmp(kind, "CODE") ? 3 : !strcmp(kind, "PROJECTS") ? 4 :
		      !strcmp(kind, "ARTIFACTS") ? 5 : 2;
	/* empty the list until the new one arrives */
	strlcpy_(list_title, nav_pending == 3 ? "Code" : nav_pending == 4 ? "Projects" :
		 nav_pending == 5 ? "Artifacts" : "Recents", sizeof(list_title));
	nitems = 0;
	list_top = 0;
	lcur = -1;
	menu_target = -1;
	list_loading = 1;
	dirty |= D_SIDEBAR;
	tx_cmd("LIST", kind, 0);
	if (!strcmp(kind, "PROJECTS"))
		clear_context("Projects", "Pick a project on the left to see its chats.");
	else if (!strcmp(kind, "ARTIFACTS"))
		clear_context("Artifacts", "Pick an artifact on the left to view it.");
	else if (!strcmp(kind, "CODE"))
		clear_context("Code", "Pick a Claude Code session on the left to read it. "
			      "What you type then goes to that session.");
	else
		clear_context("New chat", "Pick a chat on the left, or type below "
			      "to start a new one.");
}

/* a file attached to the next message: its data goes to the bridge now,
 * in hex lines (ATTACH <name> <size>, ADATA <hex>..., AEND), and the
 * bridge sends it with the message */
#define ATT_MAX (500L << 20)		/* claude.ai's limit for a file */
#define ATT_LINE 480			/* bytes in an ADATA line */
static void attach_file(const char *path)
{
	char hex[2 * ATT_LINE + 1], size[12], *buf;
	const char *name = path, *p;
	long fd, len, sent = 0, n, piece, got, off;
	short i, pct = -1;
	int tt;

	for (p = path; *p; p++)
		if (*p == '\\' || *p == ':')
			name = p + 1;
	if (!online) {
		alert(1, "[1][ClaudeST isn't connected|to its gateway, so it can't|attach a file now.][ OK ]");
		return;
	}
	if (busy) {
		alert(1, "[1][Claude is answering: attach|the file when it's done.][ OK ]");
		return;
	}
	if (natt >= MAXATT) {
		alert(1, "[1][claude.ai takes up to 20|files in a chat.][ OK ]");
		return;
	}
	if (save_fd >= 0) {
		alert(1, "[1][An artifact is being saved:|attach the file when it's|done.][ OK ]");
		return;
	}
	fd = Fopen(path, 0);
	if (fd < 0) {
		alert(1, "[1][Can't read %s.][ OK ]", name);
		return;
	}
	len = Fseek(0, (short)fd, 2);
	Fseek(0, (short)fd, 0);
	if (len <= 0 || len > ATT_MAX) {
		Fclose((short)fd);
		alert(1, len <= 0 ? "[1][%s is empty.][ OK ]" : "[1][%s is bigger than|claude.ai takes (500 MB).][ OK ]", name);
		return;
	}
	{				/* the size in decimal (put_num takes 16 bits) */
		long v = len;
		short k = 0, j;
		char t[12];
		while (v) {
			t[k++] = '0' + (char)(v % 10);
			v /= 10;
		}
		for (j = 0; j < k; j++)
			size[j] = t[k - 1 - j];
		size[k] = 0;
	}
	tx_cmd("ATTACH", name, size);
	graf_mouse(BUSYBEE, 0);
	/* read in big pieces (the guidelines, 6.5), sent a line at a time */
	piece = get_cookie(0x5F435055L) >= 30 ? 32L * 1024 : 16L * 1024;	/* _CPU */
	buf = big_alloc(len < piece ? len : piece, &got, &tt);
	if (!buf) {
		buf = disk_buf;
		got = DISK_BUF;
	}
	piece = got < piece ? got : piece;
	while ((n = Fread((short)fd, piece, buf)) > 0) {
		for (off = 0; off < n; off += ATT_LINE) {
			static const char hx[] = "0123456789ABCDEF";
			short k = (short)(n - off < ATT_LINE ? n - off : ATT_LINE);
			const char *b = buf + off;
			for (i = 0; i < k; i++) {
				hex[2 * i] = hx[((u8)b[i]) >> 4];
				hex[2 * i + 1] = hx[b[i] & 15];
			}
			hex[2 * k] = 0;
			tx_cmd("ADATA", hex, 0);
			sent += k;
			/* "Attaching 40%" (in tenths, without overflowing a long) */
			if ((sent == len ? 10 : sent / (len / 10 + 1)) != pct) {
				pct = (short)(sent == len ? 10 : sent / (len / 10 + 1));
				strcpy(notice, "Attaching ");
				*put_num(notice + 10, pct * 10) = 0;
				strcat(notice, "%");
				notice_until = ticks + 250;
				dirty |= D_TITLE;
				flush_dirty();
			}
			if (link == LINK_TCP && tcp_cn < 0)
				break;		/* the connection went */
		}
		if (off < n)
			break;
	}
	Fclose((short)fd);
	if (buf != disk_buf)
		big_free(buf);
	graf_mouse(ARROW, 0);
	if (sent != len) {
		tx_cmd("UNATTACH", 0, 0);
		natt = 0;
		alert(1, "[1][%s couldn't be sent|to the gateway.][ OK ]", name);
		dirty |= D_INPUT;
		return;
	}
	tx_cmd("AEND", 0, 0);
	strlcpy_(att_name[natt++], name, sizeof(att_name[0]));
	dirty |= D_INPUT;
}

/* File > Attach file...: the GEM file selector, in the current folder */
static void choose_attach(void)
{
	static char path[160], file[16];
	short drv = (short)Dgetdrv(), button = 0, i;
	char full[180], *p;
	if (!path[0]) {
		path[0] = 'A' + drv;
		path[1] = ':';
		Dgetpath(path + 2, 0);
		p = path + strlen(path);
		if (p[-1] != '\\')
			*p++ = '\\';
		strcpy(p, "*.*");
	}
	file[0] = 0;
	flush_dirty();
	fsel_exinput(path, file, &button, "Attach a file");
	dirty |= D_ALL;		/* the selector covered the window */
	if (!button || !file[0])
		return;
	strcpy(full, path);
	p = full;
	for (i = 0; full[i]; i++)
		if (full[i] == '\\')
			p = full + i + 1;
	strcpy(p, file);
	attach_file(full);
}

static void submit(void)
{
	input[inlen] = 0;
	if (!search_mode && !memcmp(input, "/connect ", 9)) {
		u32 ip;
		u16 port = tcp_port;
		if (parse_ip(input + 9, &ip, &port)) {
			tcp_ip = ip;
			tcp_port = port;
			use_link(LINK_TCP);
		} else {
			alert(1, "[1][Use: /connect 192.168.1.10|or /connect 192.168.1.10:2323|(your gateway's address)][ OK ]");
			return;
		}
	} else if (!search_mode && !strcmp(input, "/serial")) {
		use_link(LINK_SERIAL);
	} else if (search_mode) {
		tx_cmd("FIND", input, 0);
		search_mode = 0;
		dirty |= D_SIDEBAR;
	} else if (inlen > 0 || natt) {
		if (fresh) {		/* not into the chat from our last run */
			tx_cmd("NEW", "QUIET", 0);
			fresh = 0;
		}
		tx_cmd("SEND", input, 0);
		natt = 0;		/* the bridge sends them with this message */
	} else {
		return;
	}
	inlen = in_cur = in_start = 0;
	input[0] = 0;
	dirty |= D_INPUT;
}

static void open_item(short i)
{
	ITEM *it = &items[i];
	if (it->heading)
		return;
	const char *kind = "CHAT";
	if (!strcmp(list_kind, "PROJECTS"))
		kind = "PROJECT";
	else if (!strcmp(list_kind, "ARTIFACTS"))
		kind = "ARTIFACT";
	else if (!strcmp(list_kind, "CODE"))
		kind = "CODE";
	if (!strcmp(it->id, "..")) {
		do_list("PROJECTS");
		return;
	}
	tx_cmd("OPEN", kind, it->id);
	fresh = 0;
	if (!strcmp(kind, "PROJECT")) {
		static char hint[120];
		strcpy(hint, "Pick a chat of this project on the left, or type below to "
		       "start a new chat in it.");
		clear_context(it->label, hint);
		/* the bridge stays in the project, so a new chat is created there */
	}
	if (!strcmp(kind, "CHAT") || !strcmp(kind, "CODE")) {
		strlcpy_(cur_id, it->id, sizeof(cur_id));
		strlcpy_(chat_title, it->label, sizeof(chat_title));
		clear_text();
		begin_message('I');
		append("Loading...", 10);
		dirty |= D_LIST | D_TITLE;
	}
}

static void scroll_text(short delta)
{
	short old = top_line;
	top_line += delta;
	clamp_top();
	if (top_line != old) {
		text_dirty_row = 0;
		dirty |= D_TEXT;
	}
}

static void scroll_list(short delta)
{
	short old = list_top;
	list_top += delta;
	if (list_top > nitems - list_rows)
		list_top = nitems - list_rows;
	if (list_top < 0)
		list_top = 0;
	if (list_top != old)
		dirty |= D_LIST;
}

static void about(void);
static void keys_dialog(void);

static void set_baud(short b)
{
	baud = b;
	if (link == LINK_SERIAL) {
		Rsconf(baud, 0, -1, -1, -1, -1);
		send_hello();
	}
	menu_check_baud();
	save_config();
}

/* is the gateway still there? Asked when nothing has passed either way
   for PING_QUIET and Claude isn't answering; a gateway that doesn't
   answer within PING_WAIT is taken as gone: offline, and connecting
   again as when it drops (a serial cable has no other way to tell) */
static void heartbeat(void)
{
	u32 now;
	if (!online || !can_ping || busy)
		return;
	now = tos_ms();
	if (!ping_at) {
		if (now - last_rx > PING_QUIET && now - last_tx > PING_QUIET) {
			tx_cmd("PING", 0, 0);
			ping_at = now;
		}
		return;
	}
	if (now - ping_at < PING_WAIT)
		return;
	ping_at = 0;
	can_ping = 0;
	online = 0;
	if (link == LINK_TCP) {
		if (tcp_cn >= 0)
			sting_close(tcp_cn);
		tcp_cn = -1;
		next_connect = ticks + 125;	/* again in ~5 s */
	}
	set_status("Gateway silent");	/* short: the sidebar is narrow */
}

static void reconnect(void)
{
	if (link == LINK_TCP) {
		if (tcp_cn >= 0)
			sting_close(tcp_cn);
		if (tcp_opening >= 0)
			sting_close(tcp_opening);
		tcp_cn = tcp_opening = -1;
		online = 0;
		next_connect = 0;
	} else {
		send_hello();
	}
}

/* ------------------------------------------------------------------ */
/* popup menus                                                         */
/* ------------------------------------------------------------------ */

static void wait_release(void)
{
	short mx, my, mb, ks, m[8];
	AEVENT e;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_multi_(MU_TIMER, 0, 0, 0, 10, m, &e);
	}
}

/* list and right-click menus: the AERA programs' shared popup
 * (common/ui/popup.c), above the row it is for when there is no room
 * below; what it covered is repainted at once */
static short menu_popup(short x, short y, short alt_y, const char *const *lab, short n)
{
	short r = popup_alt(x, y, alt_y, lab, n), saved = text_dirty_row;
	text_dirty_row = 0;
	redraw(D_ALL, popup_area.x, popup_area.y, popup_area.w, popup_area.h);
	text_dirty_row = saved;
	return r;
}

/* ------------------------------------------------------------------ */
/* a small GEM-style dialog with one text field                        */
/* ------------------------------------------------------------------ */

static void toggle_hebrew(void);

static short dlg_x, dlg_y, dlg_w, dlg_h, fld_x, fld_y, fld_w, fld_cols;
static short btn_y, btn_h, ok_x, cancel_x, btn_w;

static void dlg_button(short x, const char *label, short is_default)
{
	short n = strlen(label);
	fill(x, btn_y, x + btn_w - 1, btn_y + btn_h - 1, 0);
	line(x, btn_y, x + btn_w - 1, btn_y, 1);
	line(x + btn_w - 1, btn_y, x + btn_w - 1, btn_y + btn_h - 1, 1);
	line(x + btn_w - 1, btn_y + btn_h - 1, x, btn_y + btn_h - 1, 1);
	line(x, btn_y + btn_h - 1, x, btn_y, 1);
	if (is_default) {	/* the default button gets the thick GEM border */
		line(x - 1, btn_y - 1, x + btn_w, btn_y - 1, 1);
		line(x + btn_w, btn_y - 1, x + btn_w, btn_y + btn_h, 1);
		line(x + btn_w, btn_y + btn_h, x - 1, btn_y + btn_h, 1);
		line(x - 1, btn_y + btn_h, x - 1, btn_y - 1, 1);
	}
	text(x + (btn_w - n * cw) / 2, btn_y + (btn_h - ch) / 2, label, n, is_default ? 1 : 0, 1);
}

static void dlg_field(const char *buf, short len, short cur, short *start)
{
	short ty_ = fld_y + 3;

	fill(fld_x + 1, fld_y + 1, fld_x + fld_w - 2, fld_y + ch + 4, 0);
	edit_draw(buf, len, cur, start, fld_x + cw / 2, ty_, fld_cols - 1);
	if (hebrew_kbd) {
		short bx = fld_x + fld_w + cw / 2;
		fill(bx - 2, fld_y + 1, bx + 2 * cw + 1, fld_y + ch + 4, 1);
		text(bx, ty_, "HE", 2, 1, 0);
	} else {
		fill(fld_x + fld_w + cw / 2 - 2, fld_y + 1, fld_x + fld_w + 3 * cw, fld_y + ch + 4, 0);
	}
}

/* a dialog box with a shadow and a double border, like a GEM alert */
static void dlg_frame(short x, short y, short w, short h)
{
	fill(x + 3, y + 3, x + w + 2, y + h + 2, 1);
	fill(x, y, x + w - 1, y + h - 1, 0);
	line(x, y, x + w - 1, y, 1);
	line(x + w - 1, y, x + w - 1, y + h - 1, 1);
	line(x + w - 1, y + h - 1, x, y + h - 1, 1);
	line(x, y + h - 1, x, y, 1);
	line(x + 2, y + 2, x + w - 3, y + 2, 1);
	line(x + w - 3, y + 2, x + w - 3, y + h - 3, 1);
	line(x + w - 3, y + h - 3, x + 2, y + h - 3, 1);
	line(x + 2, y + h - 3, x + 2, y + 2, 1);
}

/* Asks for a line of text. buf holds the starting text and receives the
 * result (up to max-1 characters). Returns 1 for OK, 0 for Cancel.
 * Keys: typing, Backspace, Clr/Home clears, F10 Hebrew, Return OK, Esc/Undo
 * cancel. Mouse: the buttons. */
static short text_dialog(const char *title, const char *ok_label, char *buf, short max)
{
	char edit[128];
	short len, cur, start = 0, res = -1, clipr[4], m[8], tl = strlen(title);
	AEVENT e;

	if (max > (short)sizeof(edit))
		max = sizeof(edit);
	strlcpy_(edit, buf, max);
	len = strlen(edit);
	cur = len;

	fld_cols = 44;
	if (fld_cols > ww / cw - 10)
		fld_cols = ww / cw - 10;
	fld_w = fld_cols * cw;
	btn_w = 10 * cw;
	btn_h = ch + 6;
	dlg_w = fld_w + 8 * cw;
	dlg_h = 3 * ch + btn_h + ch * 3;
	/* over the conversation pane, so the sidebar item stays in view */
	dlg_x = dlg_w + 8 <= pw ? px + (pw - dlg_w) / 2 : wx + (ww - dlg_w) / 2;
	dlg_y = wy + (wh - dlg_h) / 3;
	fld_x = dlg_x + 2 * cw;
	fld_y = dlg_y + 2 * ch + 2;
	btn_y = dlg_y + dlg_h - btn_h - ch;
	ok_x = dlg_x + dlg_w - 2 * cw - btn_w;
	cancel_x = ok_x - btn_w - 2 * cw;

	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL */
	form_dial(FMD_START, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
	graf_mouse(M_OFF, 0);
	vswr_mode(vh, 2);
	vsf_perimeter(vh, 0);
	vst_alignment(vh, 0, 5);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vh, 1, clipr);
	dlg_frame(dlg_x, dlg_y, dlg_w, dlg_h);
	text_bidi(fld_x, dlg_y + ch / 2 + 2, title, tl, 1, 1, 0);
	/* the field */
	line(fld_x, fld_y, fld_x + fld_w - 1, fld_y, 1);
	line(fld_x + fld_w - 1, fld_y, fld_x + fld_w - 1, fld_y + ch + 5, 1);
	line(fld_x + fld_w - 1, fld_y + ch + 5, fld_x, fld_y + ch + 5, 1);
	line(fld_x, fld_y + ch + 5, fld_x, fld_y, 1);
	dlg_field(edit, len, cur, &start);
	dlg_button(cancel_x, "Cancel", 0);
	dlg_button(ok_x, ok_label, 1);
	graf_mouse(M_ON, 0);
	wait_release();

	while (res < 0) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			short changed = 1;
			if (as == 0x0d)
				res = 1;
			else if (as == 0x1b || sc == 0x61)
				res = 0;
			else if (sc == 0x47)		/* Clr/Home clears */
				len = cur = start = 0;
			else if (sc == 0x44)
				toggle_hebrew();
			else if (clip_key(e.kreturn))
				changed = field_clip(clip_key(e.kreturn), edit, &len, &cur, max - 1);
			else
				changed = edit_key(edit, &len, &cur, max - 1, e.kstate, e.kreturn);
			if (changed && res < 0) {
				graf_mouse(M_OFF, 0);
				dlg_field(edit, len, cur, &start);
				graf_mouse(M_ON, 0);
			}
		}
		if (e.which & MU_BUTTON) {
			short mx = e.mx, my = e.my;
			if (my >= btn_y && my < btn_y + btn_h) {
				if (mx >= ok_x && mx < ok_x + btn_w)
					res = 1;
				else if (mx >= cancel_x && mx < cancel_x + btn_w)
					res = 0;
			}
			wait_release();
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	{
		short saved = text_dirty_row;
		text_dirty_row = 0;
		redraw(D_ALL, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
		text_dirty_row = saved;
	}
	form_dial(FMD_FINISH, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
	dirty |= D_INPUT;	/* the HE badge may have changed */
	edit[len] = 0;
	if (res == 1)
		strlcpy_(buf, edit, max);
	return res;
}

/* Help > Keys..., and the Help key's button when no help viewer runs:
 * ClaudeST's keys, as in ECal and EContact. Any key or click closes it. */
static void keys_dialog(void)
{
	static const char *const k[][2] = {
		{ "Return", "send what you typed" },
		{ "F1  ^N", "a new chat" },
		{ "F2 F6 F3 F4", "Chats, Code, Projects, Artifacts" },
		{ "F5  ^F", "search the chat titles" },
		{ "Tab, Return", "pick and open a sidebar item" },
		{ "Insert", "the item's menu (or right-click)" },
		{ "F8  F9", "account; model and effort" },
		{ "F10", "Hebrew keyboard on or off" },
		{ "\x01 \x02", "scroll the chat; Shift: a page" },
		{ "Clr/Home", "the top; Shift: the end" },
		{ "Shift+Del", "clear the reply line" },
		{ "^O", "attach a file; Bksp removes it" },
		{ "^C ^X ^V", "copy, cut, paste" },
		{ "^R", "reconnect to the gateway" },
		{ "^,", "settings: the gateway, serial" },
		{ "Help", "the user guide" },
		{ "^W  ^Q", "quit" },
	};
	enum { N = sizeof(k) / sizeof(k[0]), KEYCOL = 13 };
	short lh = ch + (ch >= 16 ? 2 : 0), m[8], clipr[4], i, done = 0, cols, x0, y;
	AEVENT e;

	btn_w = 10 * cw;
	btn_h = ch + 6;
	dlg_w = (KEYCOL + 36) * cw;
	if (dlg_w > ww - 2 * cw)
		dlg_w = ww - 2 * cw;
	dlg_h = ch + lh * (N + 2) + btn_h + ch;
	if (dlg_h > wh - 4) {		/* a low window: no space between rows */
		lh = ch;
		dlg_h = ch + lh * (N + 2) + btn_h + ch;
	}
	if (dlg_h > wh - 4)
		dlg_h = wh - 4;
	dlg_x = wx + (ww - dlg_w) / 2;
	dlg_y = wy + (wh - dlg_h) / 2;
	btn_y = dlg_y + dlg_h - btn_h - ch / 2 - 2;
	ok_x = dlg_x + (dlg_w - btn_w) / 2;
	cols = dlg_w / cw - 4 - KEYCOL;		/* room for the words */
	x0 = dlg_x + 2 * cw;

	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL */
	form_dial(FMD_START, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
	graf_mouse(M_OFF, 0);
	vswr_mode(vh, 2);
	vsf_perimeter(vh, 0);
	vst_alignment(vh, 0, 5);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vh, 1, clipr);
	dlg_frame(dlg_x, dlg_y, dlg_w, dlg_h);
	y = dlg_y + ch / 2 + 2;
	text(x0, y, "ClaudeST's keys", 15, 1, 1);
	y += lh + lh / 2;
	for (i = 0; i < N && y + lh <= btn_y; i++, y += lh) {
		short n = strlen(k[i][1]);
		text(x0, y, k[i][0], strlen(k[i][0]), 1, 1);
		text(x0 + KEYCOL * cw, y, k[i][1], n < cols ? n : cols, 0, 1);
	}
	dlg_button(ok_x, "OK", 1);
	graf_mouse(M_ON, 0);
	wait_release();

	while (!done) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD)
			done = 1;
		if (e.which & MU_BUTTON) {
			wait_release();
			done = 1;
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	{
		short saved = text_dirty_row;
		text_dirty_row = 0;
		redraw(D_ALL, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
		text_dirty_row = saved;
	}
	form_dial(FMD_FINISH, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
}

/* ------------------------------------------------------------------ */
/* the text font (Options > Font), as in EMail and ECal                */
/* ------------------------------------------------------------------ */
/*
 * The font selector looks like the ones in other GEM programs: the font
 * families in a list on the left, the sizes of the chosen one next to
 * it, what kind of font it is, and a sample. The system font comes in
 * two sizes; with a GDOS (NVDI, SpeedoGDOS, FontGDOS...) its fonts are
 * listed too. ClaudeST lays text out in character cells, so it needs a
 * monospaced font: only those can be picked. Each font is measured once
 * and the answer kept in CLAUDEST.FNT, so the selector opens quickly the
 * next time even with hundreds of fonts installed.
 *
 * The dialog also says where the system and bitmap fonts keep their
 * Hebrew letters: the Atari character set (TOS, EmuTOS) or the
 * ISO-8859-8 or DOS places, as Israeli system fonts and many GDOS fonts
 * have them. A vector font (Speedo, TrueType, Type 1) is asked through
 * NVDI for its Hebrew letters by Unicode; when it has none, they are
 * drawn in the system font in the same cells. Menus and the dialog
 * itself keep the system font.
 */

#define MAXFAM 256
#define MAXSIZE 20

typedef struct {
	short id, format;	/* VDI font id; 1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1 */
	signed char mono;	/* -1: not looked at yet */
	char name[33];
} FAM;

static FAM fam[MAXFAM];
static short nfam;
static short sizes[MAXSIZE], nsizes;
static char size_label[MAXSIZE][12];
static short probed_id = -1;

/* Unicode of the Hebrew letters in the Atari order (0xC2-0xDC), less 0x5D0 */
#define heb_uni heb_order

static short format_of(short id);
static void on_resize(void);

/* a vector font: its Hebrew letters by Unicode through NVDI, else the
   system font draws them */
static void probe_hebrew(short id)
{
	static short probed_mode, probed_why;
	short k, fmt;
	if (id == 1) {
		heb_mode = HM_MAP;
		heb_why = HW_NONE;
		return;
	}
	if (id == probed_id) {
		heb_mode = probed_mode;
		heb_why = probed_why;
		return;
	}
	probed_id = id;
	heb_mode = HM_MAP;
	heb_why = HW_NONE;
	fmt = format_of(id);
	/* a bitmap font draws the Atari codes itself; a vector font (or one
	   whose kind the VDI doesn't say) is asked for its Hebrew letters */
	if (fmt != 1) {
		long a = vqt_char_index(vh, 'A', 1, 0);
		if (a > 0) {
			for (k = 0; k < 27; k++) {
				long i = vqt_char_index(vh, (unsigned short)(0x5D0 + heb_uni[k]), 2, 0);
				if (i <= 0)
					break;
				heb_idx[k] = (unsigned short)i;
			}
			if (k == 27) {
				heb_mode = HM_UNICODE;
			} else {
				heb_mode = HM_SYSTEM;
				heb_why = HW_NOHEB;
			}
		} else if (fmt) {
			heb_mode = HM_SYSTEM;	/* a vector font, and no NVDI 5 to ask */
			heb_why = HW_NOLOOKUP;
		}
	}
	probed_mode = heb_mode;
	probed_why = heb_why;
}

/* set the font; cw/ch become its cell size. 0 if it isn't there */
static short set_font(short id, short pt)
{
	short w, h;
	if (id == 1) {
		vst_font(vh, 1);
		vst_height(vh, pt == 8 ? 6 : pt == 16 ? 13 : (sys_ch >= 16 ? 13 : 6), &w, &h);
	} else {
		if (vst_font(vh, id) != id)
			return 0;
		vst_point(vh, pt, &w, &h);
	}
	if (w < 4 || h < 6)
		return 0;
	cw = w;
	ch = h;
	vst_alignment(vh, 0, 5);
	cur_font_id = id;
	cur_font_pt = pt;
	probe_hebrew(id);
	return 1;
}

/* back to the text font after drawing in another */
static void font_reselect(void)
{
	short keep_cw = cw, keep_ch = ch;
	set_font(cur_font_id, cur_font_pt);
	cw = keep_cw;
	ch = keep_ch;
}

/* the chosen font, or the system font if it's gone */
static void font_apply(void)
{
	if (font_id != 1 && gdos_fonts < 0)
		gdos_fonts = vq_gdos() ? vst_load_fonts(vh, 0) : 0;
	if (!set_font(font_id, font_pt)) {
		font_id = 1;
		font_pt = 0;
		set_font(1, 0);
	}
}

/* the system font at the AES's size, for the dialog's own text */
static void use_sys(void)
{
	set_font(1, sys_ch >= 16 ? 16 : 8);
	cw = sys_cw;
	ch = sys_ch;
}

/* ---- what the shared popup and About box draw with (common/ui/gemui.h;
 * st/ui.h gives them these names) ---- */

void cst_ui_fill(GRECT *r, short color)
{
	if (r->w <= 0 || r->h <= 0)
		return;
	vswr_mode(vh, 1);
	vsf_perimeter(vh, 0);
	fill(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1, color);
}

void cst_ui_line_col(short x1, short y1, short x2, short y2, short color)
{
	vswr_mode(vh, 1);
	line(x1, y1, x2, y2, color);
}

void cst_ui_hline(short x1, short x2, short y)
{
	cst_ui_line_col(x1, y, x2, y, 1);
}

void cst_ui_frame(GRECT *r, short color)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	vswr_mode(vh, 1);
	vsl_color(vh, color);
	v_pline(vh, 5, p);
}

/* bold, white on black, or light for what can't be used; at most cols
   characters; Hebrew as text() draws it */
void cst_ui_text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	if (n > cols)
		n = cols;
	vswr_mode(vh, 2);
	text(x, y, s, (short)n, (flags & 1) ? 1 : (flags & 128) ? 2 : 0, (flags & 2) ? 0 : 1);
	vst_effects(vh, 0);
}

void cst_ui_clip_on(GRECT *r)
{
	short p[4];
	p[0] = r->x;
	p[1] = r->y;
	p[2] = r->x + r->w - 1;
	p[3] = r->y + r->h - 1;
	vs_clip(vh, 1, p);
}

void cst_ui_clip_off(void)
{
	short p[4];
	p[0] = 0;
	p[1] = 0;
	p[2] = scr_w - 1;
	p[3] = scr_h - 1;
	vs_clip(vh, 0, p);
}

/* ST RAM for a bitmap to blit: the screen can't be fed from TT RAM */
void *cst_ui_st_ram(long n)
{
	static unsigned short fallback[256];
	static void *buf;
	if (!buf) {
		void *p = Mxalloc(sizeof(fallback), 0);
		buf = (long)p > 0 ? p : fallback;
	}
	return n <= (long)sizeof(fallback) ? buf : 0;
}

/* the system font for the shared popup and About box, then the text font
   (and its cell) back */
static short sys_depth, sys_saved_id, sys_saved_pt, sys_saved_cw, sys_saved_ch;

void cst_ui_font_sys_push(void)
{
	if (sys_depth++)
		return;
	sys_saved_id = cur_font_id;
	sys_saved_pt = cur_font_pt;
	sys_saved_cw = cw;
	sys_saved_ch = ch;
	use_sys();
}

void cst_ui_font_sys_pop(void)
{
	if (!sys_depth || --sys_depth)
		return;
	set_font(sys_saved_id, sys_saved_pt);
	cw = sys_saved_cw;
	ch = sys_saved_ch;
}

/* whether a font is monospaced: 'i', 'W' and 'm' as wide */
static short measure_mono(FAM *f)
{
	short w, h;
	if (vst_font(vh, f->id) != f->id)
		return 0;
	vst_point(vh, 12, &w, &h);
	if (w < 1)
		return 0;
	return vqt_width(vh, 'i') == vqt_width(vh, 'W') &&
	       vqt_width(vh, 'i') == vqt_width(vh, 'm');
}

static char fnt_buf[MAXFAM * 40];

/* Only monospaced fonts can be picked. Measuring means loading each
   font, slow with hundreds of vector fonts: the answers are kept in
   CLAUDEST.FNT ("1 Courier 10 Pitch", "0 Swiss 721") and only new fonts
   measured. */
static void keep_monospaced(void)
{
	short i, done = 0, todo = 0;
	long fd, n = 0;
	char *line, path[160];
	inf_path(path, sizeof(path), "CLAUDEST.FNT");
	fd = Fopen(path, 0);
	if (fd >= 0) {
		n = Fread((short)fd, sizeof(fnt_buf) - 1, fnt_buf);
		Fclose((short)fd);
	}
	fnt_buf[n > 0 ? n : 0] = 0;
	for (i = 1; i < nfam; i++) {
		fam[i].mono = -1;
		for (line = fnt_buf; *line; ) {
			short len = 0;
			while (line[2 + len] && line[2 + len] != '\r' && line[2 + len] != '\n')
				len++;
			if (line[1] == ' ' && len == (short)strlen(fam[i].name) &&
			    !memcmp(line + 2, fam[i].name, len)) {
				fam[i].mono = line[0] == '1';
				break;
			}
			while (*line && *line != '\n')
				line++;
			if (*line)
				line++;
		}
		if (fam[i].mono < 0)
			todo++;
	}
	if (todo) {
		char m[64], *p, keep[64];
		strlcpy_(keep, status, sizeof(keep));
		graf_mouse(BUSYBEE, 0);
		for (i = 1; i < nfam; i++)
			if (fam[i].mono < 0) {
				p = m;
				strcpy(p, "Looking for fixed-width fonts: ");
				p = put_num(p + strlen(p), ++done);
				strcpy(p, " of ");
				p = put_num(p + 4, todo);
				*p = 0;
				set_status(m);
				redraw(D_STATUS, wx, status_y - 1, sb_w, wy + wh - status_y + 1);
				fam[i].mono = (signed char)measure_mono(&fam[i]);
			}
		graf_mouse(ARROW, 0);
		set_status(keep);	/* shown again now, not after the selector */
		redraw(D_STATUS, wx, status_y - 1, sb_w, wy + wh - status_y + 1);
		/* the answers, for next time: gathered here, written in one go */
		for (line = fnt_buf, i = 1; i < nfam; i++) {
			*line++ = fam[i].mono ? '1' : '0';
			*line++ = ' ';
			strcpy(line, fam[i].name);
			strcat(line, "\r\n");
			line += strlen(line);
		}
		safe_save("CLAUDEST.FNT", "CLAUDEST.FN$", fnt_buf, line - fnt_buf);
		font_reselect();
	}
}

/* the format of the font with this id (1 bitmap, 2 Speedo, 4 TrueType,
   8 Type 1), from the list or the VDI */
static short format_of(short id)
{
	short i, n;
	char name[34];
	for (i = 0; i < nfam; i++)
		if (fam[i].id == id)
			return fam[i].format;
	n = gdos_fonts > 0 ? gdos_fonts : 0;
	for (i = 2; i <= n + 1; i++)
		if (vqt_name(vh, i, name) == id)
			return vqt_font_format(vh, i);
	return 1;
}

/* names only: quick, whatever the number of fonts. The system font
   first, then the GDOS fonts, proportional ones too (greyed) */
static void list_families(void)
{
	short i, k;
	if (nfam)
		return;
	fam[0].id = 1;
	fam[0].format = 1;
	fam[0].mono = 1;
	strcpy(fam[0].name, "System font");
	nfam = 1;
	if (gdos_fonts < 0) {
		graf_mouse(BUSYBEE, 0);
		gdos_fonts = vq_gdos() ? vst_load_fonts(vh, 0) : 0;
		graf_mouse(ARROW, 0);
	}
	for (i = 2; i <= gdos_fonts + 1 && nfam < MAXFAM; i++) {
		FAM *f = &fam[nfam];
		short n;
		f->id = vqt_name(vh, i, f->name);
		if (f->id <= 1)
			continue;
		for (k = 1; k < nfam; k++)
			if (fam[k].id == f->id)
				break;
		if (k < nfam)
			continue;
		for (n = 31; n >= 0 && (f->name[n] == ' ' || !f->name[n]); n--)
			f->name[n] = 0;
		f->format = vqt_font_format(vh, i);
		f->mono = -1;
		nfam++;
	}
	keep_monospaced();
}

/* the sizes of a family */
static void look_at(FAM *f)
{
	static const short try_pt[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36 };
	short i, w, h;
	nsizes = 0;
	if (f->id == 1) {
		sizes[0] = 8;
		strcpy(size_label[0], "8 x 8");
		sizes[1] = 16;
		strcpy(size_label[1], "8 x 16");
		nsizes = 2;
		return;
	}
	graf_mouse(BUSYBEE, 0);
	if (vst_font(vh, f->id) != f->id) {
		f->mono = 0;
		graf_mouse(ARROW, 0);
		return;
	}
	for (i = 0; i < (short)(sizeof(try_pt) / sizeof(try_pt[0])) && nsizes < MAXSIZE; i++) {
		short got = vst_point(vh, try_pt[i], &w, &h);
		if (w < 4 || h < 6 || (nsizes && sizes[nsizes - 1] >= got))
			continue;
		if (f->mono < 0)
			f->mono = vqt_width(vh, 'i') == vqt_width(vh, 'W') &&
				  vqt_width(vh, 'i') == vqt_width(vh, 'm');
		sizes[nsizes] = got;
		strcpy(put_num(size_label[nsizes], got), " pt");
		nsizes++;
	}
	if (f->mono < 0)
		f->mono = 0;
	if (!f->mono)
		nsizes = 0;
	graf_mouse(ARROW, 0);
}

static const char *format_name(FAM *f)
{
	if (f->id == 1)
		return "Bitmap (built in)";
	switch (f->format) {
	case 1: return "Bitmap (GDOS)";
	case 2: return "Speedo";
	case 4: return "TrueType";
	case 8: return "Type 1";
	}
	return "GDOS font";
}

/* ---- the dialog, drawn in the system font ---- */

typedef struct {
	short x, y, w, h;	/* inside the border */
	short n, top, sel, rows, of_sizes;
} FLIST;

static FLIST lf, ls;
static short fd_x, fd_y, fd_w, fd_h, sel_heb;
static short ok_bx, ok_by, cn_bx, cn_by, fb_w, heb_bx[3], heb_by, pv_x, pv_y, pv_w, pv_h;
static short type_x, type_y;

static short fd_cx(short col) { return fd_x + col * cw; }
static short fd_cy(short row) { return fd_y + row * ch + ch / 2; }

static void frame(short x, short y, short w, short h)
{
	line(x, y, x + w - 1, y, 1);
	line(x + w - 1, y, x + w - 1, y + h - 1, 1);
	line(x + w - 1, y + h - 1, x, y + h - 1, 1);
	line(x, y + h - 1, x, y, 1);
}

static short chosen_pt(void)
{
	return ls.sel >= 0 && ls.sel < nsizes ? sizes[ls.sel] : 0;
}

static short ok_enabled(void)
{
	return fam[lf.sel].mono && nsizes;
}

/* a button; `on`: pressed or chosen (inverted), `dim`: can't be used */
static void fbutton(short x, short y, short w, const char *label, short is_default, short on, short dim)
{
	short n = strlen(label), h = ch + 4;
	fill(x, y, x + w - 1, y + h - 1, on ? 1 : 0);
	frame(x, y, w, h);
	if (is_default)
		frame(x - 1, y - 1, w + 2, h + 2);
	text(x + (w - n * cw) / 2, y + 2, label, n, dim ? 2 : is_default ? 1 : 0, on ? 0 : 1);
}

static void flist_draw(FLIST *l)
{
	short i, sw = 2 * cw, cols_ = (l->w - sw - 4) / cw;
	fill(l->x, l->y, l->x + l->w - 1, l->y + l->h - 1, 0);
	for (i = 0; i < l->rows && l->top + i < l->n; i++) {
		short k = l->top + i, y = l->y + i * ch, n, fx = 0, col = 1;
		const char *s = l->of_sizes ? size_label[k] : fam[k].name;
		if (!l->of_sizes && !fam[k].mono)
			fx = 2;			/* proportional: greyed */
		if (k == l->sel) {
			fill(l->x, y, l->x + l->w - sw - 1, y + ch - 1, 1);
			col = 0;
			fx = 0;
		}
		n = strlen(s);
		if (n > cols_)
			n = cols_;
		text(l->x + 2, y, s, n, fx, col);
	}
	/* the scroll bar: arrows, and a box for where the view is */
	{
		short sx = l->x + l->w - sw, th = l->h - 2 * ch;
		line(sx, l->y, sx, l->y + l->h - 1, 1);
		text(sx + cw / 2, l->y, "\001", 1, 0, 1);
		text(sx + cw / 2, l->y + l->h - ch, "\002", 1, 0, 1);
		line(sx, l->y + ch, sx + sw - 1, l->y + ch, 1);
		line(sx, l->y + l->h - ch - 1, sx + sw - 1, l->y + l->h - ch - 1, 1);
		if (l->n > l->rows && th > 4) {
			short h = (short)((long)th * l->rows / l->n), y;
			if (h < 4)
				h = 4;
			y = l->y + ch + (short)((long)(th - h) * l->top / (l->n - l->rows));
			fill(sx + 2, y + 1, sx + sw - 3, y + h - 2, 1);
		}
	}
}

static void flist_show(FLIST *l, short k)
{
	if (k < l->top)
		l->top = k;
	if (k >= l->top + l->rows)
		l->top = k - l->rows + 1;
	if (l->top > l->n - l->rows)
		l->top = l->n - l->rows;
	if (l->top < 0)
		l->top = 0;
}

static void draw_preview(void)
{
	static const char l1[] = "AaBbCc 0123 The quick brown fox";
	/* shalom, olam (logical order, Atari character set) */
	static const char l2[] = "\xD6\xCD\xC7\xDA \xD1\xC7\xCD\xDA - Hebrew";
	short keep = hebfont;
	fill(pv_x + 1, pv_y + 1, pv_x + pv_w - 2, pv_y + pv_h - 2, 0);
	if (fam[lf.sel].mono && set_font(fam[lf.sel].id, chosen_pt())) {
		short cols_ = (pv_w - 2 * cw) / cw, n;
		/* clip to the box: a big font must not spill over the dialog */
		short c[4];
		c[0] = pv_x + 1;
		c[1] = pv_y + 1;
		c[2] = pv_x + pv_w - 2;
		c[3] = pv_y + pv_h - 2;
		vs_clip(vh, 1, c);
		hebfont = sel_heb;
		n = strlen(l1);
		text(pv_x + cw, pv_y + 2, l1, n > cols_ ? cols_ : n, 0, 1);
		n = strlen(l2);
		text_bidi(pv_x + cw, pv_y + 4 + ch, l2, n > cols_ ? cols_ : n, 0, 1, 0);
		hebfont = keep;
		c[0] = 0;
		c[1] = 0;
		c[2] = scr_w - 1;
		c[3] = scr_h - 1;
		vs_clip(vh, 1, c);
		use_sys();
	} else {
		use_sys();
		text(pv_x + cw, pv_y + 2, "Proportional: ClaudeST needs a fixed-width font.", 47, 0, 1);
	}
	use_sys();
}

static void draw_type(void)
{
	FAM *f = &fam[lf.sel];
	char t[72];
	short n;
	strcpy(t, format_name(f));
	if (f->id != 1 && f->format != 1) {
		/* setting it says where its Hebrew comes from */
		set_font(f->id, chosen_pt() ? chosen_pt() : 12);
		strcat(t, heb_mode == HM_UNICODE ? ", Hebrew from the font" :
			  heb_why == HW_NOHEB ? ", Hebrew in the system font (none in it)" :
			  heb_why == HW_NOLOOKUP ? ", Hebrew in the system font (no NVDI 5)" :
			  ", Hebrew as the font has it");
		use_sys();
	}
	fill(type_x, type_y, fd_x + fd_w - 2 * cw, type_y + ch - 1, 0);
	n = strlen(t);
	text(type_x, type_y, t, n, 0, 1);
}

static void draw_ok(void)
{
	fbutton(ok_bx, ok_by, fb_w, "OK", 1, 0, !ok_enabled());
}

/* a family was picked: its sizes, keeping the size if it has it */
static void family_changed(short draw)
{
	short want = chosen_pt(), i, best = 0;
	if (!want)
		want = font_id == 1 ? (font_pt ? font_pt : sys_ch >= 16 ? 16 : 8) : font_pt;
	look_at(&fam[lf.sel]);
	use_sys();
	ls.n = nsizes;
	for (i = 0; i < nsizes; i++)
		if (sizes[i] <= want)
			best = i;
	if (fam[lf.sel].id == 1)
		best = want >= 16 ? 1 : 0;
	ls.sel = nsizes ? best : -1;
	ls.top = 0;
	flist_show(&ls, ls.sel < 0 ? 0 : ls.sel);
	if (draw) {
		graf_mouse(M_OFF, 0);
		flist_draw(&ls);
		draw_type();
		draw_preview();
		draw_ok();
		graf_mouse(M_ON, 0);
	}
}

/* a click in a list: 1 = a double click on an entry */
static short flist_click(FLIST *l, short mx, short my, short clicks)
{
	short sw = 2 * cw;
	if (mx >= l->x + l->w - sw) {
		short old = l->top;
		if (my < l->y + ch)
			l->top--;
		else if (my >= l->y + l->h - ch)
			l->top++;
		else {
			short th = l->h - 2 * ch, mid = l->y + ch + th / 2;
			l->top += my < mid ? -l->rows : l->rows;
		}
		if (l->top > l->n - l->rows)
			l->top = l->n - l->rows;
		if (l->top < 0)
			l->top = 0;
		if (l->top != old) {
			graf_mouse(M_OFF, 0);
			flist_draw(l);
			graf_mouse(M_ON, 0);
		}
		return 0;
	}
	{
		short k = l->top + (my - l->y) / ch;
		if (k < 0 || k >= l->n)
			return 0;
		if (k != l->sel) {
			l->sel = k;
			graf_mouse(M_OFF, 0);
			flist_draw(l);
			graf_mouse(M_ON, 0);
			if (l == &lf)
				family_changed(1);
			else {
				graf_mouse(M_OFF, 0);
				draw_preview();
				graf_mouse(M_ON, 0);
			}
		}
		return clicks >= 2;
	}
}

static void font_menu(void)
{
	static char heb_label[3][24];
	short i, done = 0, ok = 0, m[8], clipr[4];
	short keep_id = cur_font_id, keep_pt = cur_font_pt, keep_heb = hebfont;
	AEVENT e;

	hebfont = HEB_ATARI;	/* the dialog's own samples are drawn as they are */
	use_sys();
	list_families();
	use_sys();
	sel_heb = keep_heb;
	lf.sel = 0;
	for (i = 0; i < nfam; i++)
		if (fam[i].id == font_id)
			lf.sel = i;
	lf.n = nfam;
	lf.top = 0;
	lf.of_sizes = 0;
	ls.of_sizes = 1;
	ls.sel = -1;
	family_changed(0);

	/* samples of shalom as each kind of font would show it */
	strcpy(heb_label[0], "Atari  \xDA\xC7\xCD\xD6");
	strcpy(heb_label[1], "ISO 8859-8  \xED\xE5\xEC\xF9");
	strcpy(heb_label[2], "DOS 862  \x8D\x85\x8C\x99");

	/* 64 x 23 cells: fits ST medium's 640 x 200 too */
	fd_w = 64 * cw;
	fd_h = 23 * ch;
	fd_x = (scr_w - fd_w) / 2;
	fd_y = (scr_h - fd_h) / 2;
	if (fd_y < sys_ch + 2)
		fd_y = sys_ch + 2;
	lf.x = fd_cx(2) + 1;
	lf.y = fd_cy(4) + 1;
	lf.w = 35 * cw - 2;
	lf.rows = 8;
	lf.h = lf.rows * ch;
	ls.x = fd_cx(39) + 1;
	ls.y = lf.y;
	ls.w = 12 * cw - 2;
	ls.rows = lf.rows;
	ls.h = lf.h;
	fb_w = 9 * cw;
	ok_bx = cn_bx = fd_cx(53);
	ok_by = fd_cy(4);
	cn_by = fd_cy(6) + 2;
	type_x = fd_cx(8);
	type_y = fd_cy(14);
	heb_by = fd_cy(17);
	for (i = 0; i < 3; i++)
		heb_bx[i] = fd_cx(2 + i * 20);
	pv_x = fd_cx(2);
	pv_y = fd_cy(19);
	pv_w = 60 * cw;
	pv_h = 3 * ch;
	flist_show(&lf, lf.sel);
	flist_show(&ls, ls.sel < 0 ? 0 : ls.sel);

	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL */
	form_dial(FMD_START, fd_x, fd_y, fd_w + 3, fd_h + 3);
	graf_mouse(M_OFF, 0);
	vswr_mode(vh, 2);
	vsf_perimeter(vh, 0);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vh, 1, clipr);
	dlg_frame(fd_x, fd_y, fd_w, fd_h);
	text(fd_cx(2), fd_cy(1), "Font for the text", 17, 1, 1);
	text(fd_cx(2), fd_cy(3), "Family", 6, 0, 1);
	text(fd_cx(39), fd_cy(3), "Size", 4, 0, 1);
	frame(lf.x - 1, lf.y - 1, lf.w + 2, lf.h + 2);
	frame(ls.x - 1, ls.y - 1, ls.w + 2, ls.h + 2);
	draw_ok();
	fbutton(cn_bx, cn_by, fb_w, "Cancel", 0, 0, 0);
	text(fd_cx(2), type_y, "Type:", 5, 0, 1);
	text(fd_cx(2), fd_cy(16), "Hebrew in bitmap fonts: pick the sample that reads right", 56, 0, 1);
	for (i = 0; i < 3; i++)
		fbutton(heb_bx[i], heb_by, 18 * cw, heb_label[i], 0, i == sel_heb, 0);
	frame(pv_x, pv_y, pv_w, pv_h);
	flist_draw(&lf);
	flist_draw(&ls);
	draw_type();
	draw_preview();
	graf_mouse(M_ON, 0);
	wait_release();

	while (!done) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x102, 3, 1, 0, m, &e);
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			if (as == 0x1b || sc == 0x61) {
				done = 1;
			} else if (as == 0x0d || sc == 0x72) {
				if (ok_enabled())
					done = ok = 1;
			} else if ((sc == 0x48 && lf.sel > 0) || (sc == 0x50 && lf.sel < nfam - 1)) {
				lf.sel += sc == 0x48 ? -1 : 1;
				flist_show(&lf, lf.sel);
				graf_mouse(M_OFF, 0);
				flist_draw(&lf);
				graf_mouse(M_ON, 0);
				family_changed(1);
			} else if ((sc == 0x4b && ls.sel > 0) || (sc == 0x4d && ls.sel >= 0 && ls.sel < nsizes - 1)) {
				ls.sel += sc == 0x4b ? -1 : 1;
				flist_show(&ls, ls.sel);
				graf_mouse(M_OFF, 0);
				flist_draw(&ls);
				draw_preview();
				graf_mouse(M_ON, 0);
			}
		}
		if (e.which & MU_BUTTON) {
			short mx = e.mx, my = e.my, bh = ch + 4;
			FLIST *l = 0;
			if (mx >= lf.x && mx < lf.x + lf.w && my >= lf.y && my < lf.y + lf.h)
				l = &lf;
			else if (mx >= ls.x && mx < ls.x + ls.w && my >= ls.y && my < ls.y + ls.h)
				l = &ls;
			if (l) {
				if (flist_click(l, mx, my, e.breturn) && ok_enabled())
					done = ok = 1;
			} else if (mx >= ok_bx && mx < ok_bx + fb_w && my >= ok_by && my < ok_by + bh) {
				if (ok_enabled())
					done = ok = 1;
			} else if (mx >= cn_bx && mx < cn_bx + fb_w && my >= cn_by && my < cn_by + bh) {
				done = 1;
			} else if (my >= heb_by && my < heb_by + bh) {
				for (i = 0; i < 3; i++)
					if (mx >= heb_bx[i] && mx < heb_bx[i] + 18 * cw && i != sel_heb) {
						short k;
						sel_heb = i;
						graf_mouse(M_OFF, 0);
						for (k = 0; k < 3; k++)
							fbutton(heb_bx[k], heb_by, 18 * cw, heb_label[k], 0, k == sel_heb, 0);
						draw_preview();
						graf_mouse(M_ON, 0);
					}
			}
			if (!done)
				wait_release();
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	form_dial(FMD_FINISH, fd_x, fd_y, fd_w + 3, fd_h + 3);
	wait_release();
	if (ok) {
		font_id = fam[lf.sel].id;
		font_pt = chosen_pt();
		hebfont = sel_heb;
		font_apply();
		save_config();
		on_resize();		/* lay out again, wrap the text anew */
	} else {
		hebfont = keep_heb;
		set_font(keep_id, keep_pt);
	}
}

/* The help: EHypView or ST-Guide (as an accessory or a program) shows
 * CLAUDEST.HYP from ClaudeST's own folder (common/ui/help.c). Without a
 * viewer, an alert says how to get one and offers the About box. */

static void help(void)
{
	if (help_key(home_dir, "CLAUDEST.HYP", 0, "Keys"))
		keys_dialog();
}

/* the About box: the AERA programs' shared one (common/ui/aboutbox.c),
 * with how ClaudeST is connected and the trademarks it names */
static void about(void)
{
	static char conn[64];
	ABOUT a = { "ClaudeST", APP_VERSION, "Claude.ai for the Atari ST, TT and Falcon", { conn, 0, 0 },
		    { "Unofficial client, not affiliated with Anthropic or Atari.",
		      "Claude is a trademark of Anthropic PBC.",
		      "Atari is a trademark of Atari Interactive." },
		    icon_bits, icon16_px, icon16_rgb };
	if (link == LINK_TCP && tcp_ip) {
		char *p;
		strcpy(conn, "Gateway ");
		ip_to_str(tcp_ip, conn + 8);
		p = conn + strlen(conn);
		*p++ = ':';
		p = put_num(p, tcp_port);
		strcpy(p, online ? "  \xF9  online" : "  \xF9  offline");
	} else {
		strcpy(conn, baud == BAUD_4800 ? "Serial port, 4800 baud" :
		       baud == BAUD_9600 ? "Serial port, 9600 baud" : "Serial port, 19200 baud");
		strcpy(conn + strlen(conn), online ? "  \xF9  online" : "  \xF9  offline");
	}
	about_show(&a);
}

static void rename_item(const char *kind, ITEM *it)
{
	char name[72];
	short n;
	strlcpy_(name, it->label, sizeof(name));
	if (!text_dialog(!strcmp(kind, "PROJECT") ? "Rename project" :
			 !strcmp(kind, "CODE") ? "Rename session" : "Rename chat",
			 "Rename", name, sizeof(name)))
		return;
	/* trim spaces; an empty or unchanged name changes nothing */
	for (n = strlen(name); n > 0 && name[n - 1] == ' '; n--)
		name[n - 1] = 0;
	if (name[0] && strcmp(name, it->label))
		tx_cmd4("RENAME", kind, it->id, name);
}

static void open_item(short i);

static short item_row_y(short i)
{
	return list_y + (i - list_top + 1) * row_h - 2;
}

/* only the margin of row i, where its dot is: no flicker */
static void redraw_dot(short i)
{
	if (i >= list_top && i < list_top + list_rows && i < nitems)
		redraw(D_SIDEBAR, wx + 1, item_row_y(i), cw + 3, row_h);
}

static void redraw_item_row(short i)
{
	if (i >= list_top && i < list_top + list_rows && i < nitems)
		redraw(D_SIDEBAR, wx, item_row_y(i), sb_w, row_h + 1);
}

static short current_item(void)
{
	short i;
	for (i = 0; i < nitems; i++)
		if (cur_id[0] && !strcmp(items[i].id, cur_id))
			return i;
	return -1;
}

/* highlight (or un-highlight, i = -1) the item a menu acts on, repainting
 * only the rows that change so it shows up instantly */
static void set_menu_target(short i)
{
	short old = menu_target, cur = current_item();
	flush_dirty();
	menu_target = i;
	redraw_item_row(old);
	redraw_item_row(i);
	if (cur != old && cur != i)
		redraw_item_row(cur);
}

/* right-click menu for a sidebar item, like the one on claude.ai */
static void context_menu(short i, short x, short y)
{
	if (items[i].heading)
		return;
	static const char *chat_menu[6];
	static const char *proj_menu[6];
	static const char *art_menu[2] = { "Open", "Save to disk..." };
	ITEM *it = &items[i];
	short r, y0, y1;

	(void)y;
	if (!strcmp(it->id, ".."))
		return;
	if (i < list_top)
		list_top = i;
	if (i >= list_top + list_rows)
		list_top = i - list_rows + 1;
	y0 = item_row_y(i);
	y1 = y0 + row_h;
	set_menu_target(i);		/* highlight the item before the menu opens */

	if (!strcmp(list_kind, "ARTIFACTS")) {
		r = menu_popup(x, y1, y0, art_menu, 2);
		set_menu_target(-1);
		if (r == 0)
			open_item(i);
		else if (r == 1) {
			strlcpy_(save_id, it->id, sizeof(save_id));
			tx_cmd("SAVE", "ARTIFACT", it->id);
		}
		return;
	}
	if (!strcmp(list_kind, "CODE")) {
		static const char *code_menu[4] = { "Open", "Rename...", "-", "Archive" };
		r = menu_popup(x, y1, y0, code_menu, 4);
		switch (r) {
		case 0: open_item(i); break;
		case 1: rename_item("CODE", it); break;
		case 3: tx_cmd("ARCHIVE", "CODE", it->id); break;
		}
		set_menu_target(-1);
		return;
	}
	if (!strcmp(list_kind, "PROJECTS")) {
		proj_menu[0] = "Open";
		proj_menu[1] = it->pinned ? "Unpin" : "Pin";
		proj_menu[2] = "Rename...";
		proj_menu[3] = "Archive";
		proj_menu[4] = "-";
		proj_menu[5] = "Delete...";
		r = menu_popup(x, y1, y0, proj_menu, 6);
		switch (r) {
		case 0: open_item(i); break;
		case 1: tx_cmd4("PIN", "PROJECT", it->id, it->pinned ? "0" : "1"); break;
		case 2: rename_item("PROJECT", it); break;
		case 3: tx_cmd("ARCHIVE", "PROJECT", it->id); break;
		case 5:
			if (alert(2, "[3][Delete this project?|This can't be undone.][Delete|Cancel]") == 1)
				tx_cmd("DELETE", "PROJECT", it->id);
			break;
		}
		set_menu_target(-1);
		return;
	}
	chat_menu[0] = "Open";
	chat_menu[1] = it->pinned ? "Unpin" : "Pin";
	chat_menu[2] = "Rename...";
	chat_menu[3] = "Move to project...";
	chat_menu[4] = "-";
	chat_menu[5] = "Delete...";
	r = menu_popup(x, y1, y0, chat_menu, 6);
	switch (r) {
	case 0: open_item(i); break;
	case 1: tx_cmd4("PIN", "CHAT", it->id, it->pinned ? "0" : "1"); break;
	case 2: rename_item("CHAT", it); break;
	case 3:
		strlcpy_(pick_chat, it->id, sizeof(pick_chat));
		pick_x = x;
		tx_cmd("PICKPROJ", it->id, 0);
		break;
	case 5:
		if (alert(2, "[3][Delete this chat?|This can't be undone.][Delete|Cancel]") == 1)
			tx_cmd("DELETE", "CHAT", it->id);
		break;
	}
	set_menu_target(-1);
}

/* the GEM file selector, starting in the current folder with the name the
 * bridge suggested; then ask the bridge for the data */
static void choose_save_file(const char *name)
{
	static char path[160], file[16];
	short drv = (short)Dgetdrv(), button = 0, i;
	char *p;
	long fd;

	path[0] = 'A' + drv;
	path[1] = ':';
	Dgetpath(path + 2, 0);
	p = path + strlen(path);
	if (p[-1] != '\\')
		*p++ = '\\';
	strcpy(p, "*.*");
	strlcpy_(file, name, sizeof(file));
	flush_dirty();
	fsel_exinput(path, file, &button, "Save artifact as");
	dirty |= D_ALL;		/* the selector covered the window */
	if (!button || !file[0]) {
		save_id[0] = 0;
		return;
	}
	/* folder from the selector's path (up to the last \) + the name */
	strcpy(save_path, path);
	p = save_path;
	for (i = 0; save_path[i]; i++)
		if (save_path[i] == '\\')
			p = save_path + i + 1;
	strcpy(p, file);
	fd = Fcreate(save_path, 0);
	if (fd < 0) {
		alert(1, "[3][Couldn't create that file.|Is the disk write-protected|or full?][ OK ]");
		save_id[0] = 0;
		return;
	}
	save_fd = (short)fd;
	save_bytes = 0;
	strcpy(notice, "Saving...");
	notice_until = ticks + 250;
	dirty |= D_TITLE;
	tx_cmd("FETCH", "ARTIFACT", save_id);
}

static short hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0;
}

static void save_done(void)
{
	if (save_buf != disk_buf)
		big_free(save_buf);
	save_buf = 0;
	Fclose(save_fd);
	save_fd = -1;
}

/* what is gathered to the file: 0 when the write failed */
static short save_flush(void)
{
	if (save_len && Fwrite(save_fd, save_len, save_buf) != save_len) {
		save_done();
		Fdelete(save_path);
		save_id[0] = 0;
		alert(1, "[3][Writing the file failed.|The disk may be full.][ OK ]");
		return 0;
	}
	save_len = 0;
	return 1;
}

static void save_chunk(const char *hex, long size)
{
	if (!save_buf) {
		/* the whole file when it fits (an older bridge doesn't say how
		   big it is: up to 1 MB), else a buffer at a time */
		int tt;
		save_buf = big_alloc(size > 0 ? size : 1L << 20, &save_cap, &tt);
		if (!save_buf) {
			save_buf = disk_buf;
			save_cap = DISK_BUF;
		}
		save_len = 0;
	}
	while (hex[0] && hex[1]) {
		if (save_len == save_cap && !save_flush())
			return;
		save_buf[save_len++] = (char)(hexval(hex[0]) << 4 | hexval(hex[1]));
		save_bytes++;
		hex += 2;
	}
}

static void finish_save(void)
{
	if (save_fd < 0)
		return;
	if (save_buf && !save_flush())
		return;
	save_done();
	save_id[0] = 0;
	notice[0] = 0;
	dirty |= D_TITLE;
	flush_dirty();
	alert(1, "[1][Saved %s|%lu bytes][ OK ]", save_path, (unsigned long)save_bytes);
}

/* The model menu: the models, then the effort levels for this model.
 * The current ones are ticked. Opened from the chip, F9 or Options. */
static void model_menu(void)
{
	static char lab[NMODEL + NEFFORT + 1][24];
	const char *lp[NMODEL + NEFFORT + 1];
	short i, n = 0, r;
	if (!nmodel) {
		alert(1, "[1][The gateway hasn't sent|the list of models yet.][ OK ]");
		return;
	}
	for (i = 0; i < nmodel; i++, n++) {
		strcpy(lab[n], strcmp(mod_id[i], cur_model) ? " " : "\x08");	/* ticked (common/ui/popup.c) */
		strlcpy_(lab[n] + 1, mod_lab[i], 20);
	}
	if (neffort) {
		strcpy(lab[n++], "----------------");
		for (i = 0; i < neffort; i++, n++) {
			strcpy(lab[n], strcmp(eff_id[i], cur_effort) ? " " : "\x08");
			strcat(lab[n], "Effort: ");
			strlcpy_(lab[n] + strlen(lab[n]), eff_lab[i], 13);
		}
	}
	for (i = 0; i < n; i++)
		lp[i] = lab[i];
	flush_dirty();
	r = menu_popup(chip_x0 ? chip_x0 : px + pw / 2, wy + title_h + 1, -1, lp, n);
	if (r < 0)
		return;
	if (r < nmodel)
		strlcpy_(pref_model, mod_id[r], sizeof(pref_model));
	else {
		strlcpy_(pref_model, cur_model, sizeof(pref_model));
		strlcpy_(pref_effort, eff_id[r - nmodel - 1], sizeof(pref_effort));
	}
	if (r < nmodel)
		strlcpy_(pref_effort, cur_effort, sizeof(pref_effort));
	pref_sent = 1;
	tx_cmd("CHOOSE", pref_model, pref_effort);
	save_config();
}

/* "New project...": name it in a dialog; the bridge creates the project
 * and moves the chat into it */
static void new_project_for(const char *chat_id)
{
	char name[72];
	short n;
	name[0] = 0;
	if (!text_dialog("New project", "Create", name, sizeof(name)))
		return;
	for (n = strlen(name); n > 0 && name[n - 1] == ' '; n--)
		name[n - 1] = 0;
	if (name[0])
		tx_cmd("NEWPROJ", chat_id, name);
}

static void show_picker(void)
{
	/* "New project..." first, a line, then the existing projects */
	const char *lab[PICKMAX + 3];
	short i, r, t, y0, n = 0;
	char chat[40];
	if (!pick_chat[0])
		return;
	lab[n++] = "New project...";
	if (npick)
		lab[n++] = "-";
	for (i = 0; i < npick; i++)
		lab[n++] = pick_label[i];
	/* highlight the chat being moved again while choosing its project */
	for (t = 0; t < nitems && strcmp(items[t].id, pick_chat); t++)
		;
	if (t < nitems && t >= list_top && t < list_top + list_rows) {
		set_menu_target(t);
		y0 = item_row_y(t);
		r = menu_popup(pick_x, y0 + row_h, y0, lab, n);
	} else {
		flush_dirty();
		r = menu_popup(pick_x, list_y + row_h, -1, lab, n);
	}
	strlcpy_(chat, pick_chat, sizeof(chat));
	pick_chat[0] = 0;
	if (r == 0) {
		/* keep the chat highlighted while its new project is named */
		new_project_for(chat);
	} else if (r >= 2) {
		tx_cmd("MOVE", chat, pick_id[r - 2]);
	}
	set_menu_target(-1);
}

static short list_hit(short mx, short my)
{
	short r;
	if (mx < wx || mx >= wx + sb_w || my < list_y + row_h || my >= status_y - 2)
		return -1;
	r = (my - list_y - row_h) / row_h;
	if (r >= list_rows || list_top + r >= nitems || items[list_top + r].heading)
		return -1;		/* headings can't be clicked */
	return list_top + r;
}

/* ------------------------------------------------------------------ */
/* tooltips: the full text of a cut-off list item or title, shown when  */
/* the mouse rests on it                                               */
/* ------------------------------------------------------------------ */

static short tip_on, tip_x, tip_y, tip_w, tip_h;
static short tip_mx = -1, tip_my = -1, tip_done;
static long tip_still;

/* the cut-off text under (mx, my), or 0 */
static const char *tip_text(short mx, short my, short *ty)
{
	short maxc = sb_w / cw - 2, i;
	if (mx < wx + sb_w) {
		i = list_hit(mx, my);
		if (i >= 0) {
			*ty = list_y + (i - list_top + 1) * row_h;
			return (short)strlen(items[i].label) > maxc - (items[i].pinned ? 1 : 0)
				? items[i].label : 0;
		}
		if (my >= list_y && my < list_y + row_h && mx < wx + sb_w - 4 * cw) {
			*ty = list_y;
			return (short)strlen(list_title) > maxc - 4 ? list_title : 0;
		}
		return 0;
	}
	if (my >= wy && my < wy + title_h && mx >= px && mx < title_xr && title_cut) {
		*ty = wy;
		return chat_title;
	}
	return 0;
}

static void hide_tip(void)
{
	short saved;
	if (!tip_on)
		return;
	tip_on = 0;
	saved = text_dirty_row;
	text_dirty_row = 0;
	redraw(D_ALL, tip_x, tip_y, tip_w + 3, tip_h + 3);
	text_dirty_row = saved;
	form_dial(FMD_FINISH, tip_x, tip_y, tip_w + 3, tip_h + 3);
}

static void show_tip(short mx, const char *t, short ty)
{
	short n = strlen(t), maxn = scr_w / cw - 4, clipr[4], top, wtop, d;
	wind_get(0, WF_TOP, &wtop, &d, &d, &d);
	if (wtop != win)
		return;		/* a desk accessory is in front */
	if (n > maxn)
		n = maxn;
	tip_w = (n + 1) * cw;
	tip_h = ch + 4;
	tip_x = mx - cw;
	if (tip_x + tip_w + 3 > scr_w)
		tip_x = scr_w - tip_w - 3;
	if (tip_x < 0)
		tip_x = 0;
	tip_y = ty + row_h;		/* just below the text, or above it */
	if (ty == wy)
		tip_y = wy + title_h + 2;
	if (tip_y + tip_h + 3 > scr_h)
		tip_y = ty - tip_h - 3;
	top = tip_y;
	wind_update(BEG_UPDATE);
	form_dial(FMD_START, tip_x, top, tip_w + 3, tip_h + 3);
	graf_mouse(M_OFF, 0);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vh, 1, clipr);
	vswr_mode(vh, 1);
	fill(tip_x + 2, top + 2, tip_x + tip_w + 1, top + tip_h + 1, 1);	/* shadow */
	fill(tip_x, top, tip_x + tip_w - 1, top + tip_h - 1, 0);
	line(tip_x, top, tip_x + tip_w - 1, top, 1);
	line(tip_x + tip_w - 1, top, tip_x + tip_w - 1, top + tip_h - 1, 1);
	line(tip_x + tip_w - 1, top + tip_h - 1, tip_x, top + tip_h - 1, 1);
	line(tip_x, top + tip_h - 1, tip_x, top, 1);
	text_bidi(tip_x + cw / 2, top + 2, t, n, 0, 1, 0);
	vs_clip(vh, 0, clipr);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	tip_on = 1;
}

/* called every turn of the main loop: show a tip after ~0.6 s at rest */
static void tip_poll(short events)
{
	short mx, my, mb, ks, ty;
	const char *t;
	if (win_icon)
		return;
	graf_mkstate(&mx, &my, &mb, &ks);
	if (mx != tip_mx || my != tip_my || (events & (MU_KEYBD | MU_BUTTON)) || mb) {
		hide_tip();
		tip_mx = mx;
		tip_my = my;
		tip_still = ticks;
		tip_done = 0;
		return;
	}
	if (tip_on || tip_done || ticks - tip_still < 15)
		return;
	tip_done = 1;			/* once per resting place */
	t = tip_text(mx, my, &ty);
	if (t)
		show_tip(mx, t, ty);
}

static void handle_rclick(short mx, short my)
{
	short i = list_hit(mx, my);
	if (i >= 0)
		context_menu(i, mx, my);
}

/* ------------------------------------------------------------------ */
/* the draggable divider between sidebar and conversation              */
/* ------------------------------------------------------------------ */

static short divider_x(void)
{
	return wx + sb_w;
}

static void set_divider_watch(void)
{
	evnt_set_m1(over_divider, divider_x() - 2, wy, 5, wh);
}

static void xor_divider(short x)
{
	short p[4];
	p[0] = x;
	p[1] = wy;
	p[2] = x;
	p[3] = wy + wh - 1;
	v_pline(vh, 2, p);
	p[0] = p[2] = x + 1;
	v_pline(vh, 2, p);
}

static void on_resize(void);

static void drag_divider(void)
{
	short mx, my, mb, ks, x, last = -1, m[8], clipr[4];
	short minx = wx + 14 * cw, maxx = wx + ww - 34 * cw;
	AEVENT e;

	wind_update(BEG_UPDATE);
	wind_update(3);
	graf_mouse(FLAT_HAND, 0);
	clipr[0] = wx;
	clipr[1] = wy;
	clipr[2] = wx + ww - 1;
	clipr[3] = wy + wh - 1;
	vs_clip(vh, 1, clipr);
	vswr_mode(vh, 3);		/* XOR */
	vsl_color(vh, 1);
	do {
		graf_mkstate(&mx, &my, &mb, &ks);
		x = mx < minx ? minx : mx > maxx ? maxx : mx;
		if (x != last) {
			graf_mouse(M_OFF, 0);
			if (last >= 0)
				xor_divider(last);
			xor_divider(x);
			graf_mouse(M_ON, 0);
			last = x;
		}
		evnt_multi_(MU_TIMER, 0, 0, 0, 10, m, &e);
	} while (mb & 1);
	graf_mouse(M_OFF, 0);
	xor_divider(last);
	graf_mouse(M_ON, 0);
	vswr_mode(vh, 2);
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	if (last - wx != sb_w) {
		sb_user = last - wx;
		on_resize();
		save_config();
	}
	over_divider = 0;
	graf_mouse(ARROW, 0);
	set_divider_watch();
}

static void handle_click(short mx, short my)
{
	short i;
	if (mx < wx || my < wy || mx >= wx + ww || my >= wy + wh)
		return;
	if (mx >= divider_x() - 2 && mx <= divider_x() + 2) {
		drag_divider();
		return;
	}
	if (chip_x0 && mx >= chip_x0 && mx <= chip_x1 && my < wy + title_h) {
		model_menu();
		return;
	}
	if (mx < wx + sb_w && my >= status_y) {	/* the status line: Account */
		do_account();
		return;
	}
	if (mx < wx + sb_w) {
		for (i = 0; i < NNAV; i++) {
			short y = nav_y + i * row_h;
			if (my >= y && my < y + row_h) {
				nav_pressed = i;
				dirty |= D_SIDEBAR;
				switch (i) {
				case 0: do_new_chat(); break;
				case 1: do_search(); break;
				case 2: do_list("CHATS"); break;
				case 3: do_list("CODE"); break;
				case 4: do_list("PROJECTS"); break;
				case 5: do_list("ARTIFACTS"); break;
				}
				return;
			}
		}
		if (my >= list_y && my < list_y + row_h) {
			if (mx >= wx + sb_w - 4 * cw && mx < wx + sb_w - 2 * cw)
				scroll_list(-(list_rows - 1));
			else if (mx >= wx + sb_w - 2 * cw)
				scroll_list(list_rows - 1);
			return;
		}
		if (my >= list_y + row_h && my < status_y - 2) {
			i = list_top + (my - list_y - row_h) / row_h;
			if (i < nitems && (my - list_y - row_h) / row_h < list_rows)
				open_item(i);
		}
		return;
	}
	if (my >= ty && my < ty + th) {
		/* click in upper/lower half of the text pages it */
		scroll_text(my < ty + th / 2 ? -(rows - 1) : rows - 1);
	}
}

/* the character a key types, honouring the Hebrew layout; 0 = none */
static u8 key_char(short kstate, short kr)
{
	u8 ascii = kr & 0xff, scan = (kr >> 8) & 0xff;
	/* key_ctrl: a Control key typed while busy comes with kstate 0 */
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | 0x08)) && !key_ctrl(kstate, kr) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii >= 32 && ascii != 127 ? ascii : 0;
}

static void toggle_hebrew(void)
{
	hebrew_kbd = !hebrew_kbd;
	menu_check_baud();
	save_config();
	dirty |= D_INPUT;
}

static void move_cursor(short d)
{
	short start;
	if (nitems == 0)
		return;
	lcur = lcur < 0 ? (d > 0 ? list_top : list_top + list_rows - 1) : lcur + d;
	if (lcur >= nitems)
		lcur = nitems - 1;
	if (lcur < 0)
		lcur = 0;
	/* step over date headings; at either end, turn back */
	start = lcur;
	while (lcur >= 0 && lcur < nitems && items[lcur].heading)
		lcur += d > 0 ? 1 : -1;
	if (lcur < 0 || lcur >= nitems) {
		for (lcur = start; lcur >= 0 && lcur < nitems && items[lcur].heading; )
			lcur += d > 0 ? -1 : 1;
		if (lcur < 0 || lcur >= nitems) {
			lcur = -1;		/* nothing but headings */
			return;
		}
	}
	if (lcur < list_top)
		list_top = lcur;
	if (lcur >= list_top + list_rows)
		list_top = lcur - list_rows + 1;
	dirty |= D_LIST;
}

/* Options > Settings... (^,), as in the other AERA programs: how
 * ClaudeST reaches its gateway. Network: the gateway's address and port;
 * serial: the speed. Tab moves between the two fields, Return is OK, Esc
 * or Undo Cancel. */
static void settings_dialog(void)
{
	static const char *const sp_label[3] = { "4800", "9600", "19200" };
	static const short sp_baud[3] = { BAUD_4800, BAUD_9600, BAUD_19200 };
	char f[2][24];
	short len[2], cur[2], start[2] = { 0, 0 }, cols[2] = { 17, 7 }, fx[2], fy[2];
	short nl = link, nb = baud, focus = 0, res = -1, clipr[4], m[8], i;
	short lh, cx, ly[4], chw, ch_x[2], sp_w, sp_x[3];
	AEVENT e;

	f[0][0] = 0;
	if (tcp_ip)
		ip_to_str(tcp_ip, f[0]);
	*put_num(f[1], tcp_port) = 0;
	for (i = 0; i < 2; i++)
		len[i] = cur[i] = strlen(f[i]);

	lh = ch + 8;
	btn_w = 10 * cw;
	btn_h = ch + 6;
	dlg_w = 50 * cw;
	if (dlg_w > ww - 2 * cw)
		dlg_w = ww - 2 * cw;
	dlg_h = ch + lh * 6 + btn_h + ch;
	if (dlg_h > wh - 4)
		dlg_h = wh - 4;
	dlg_x = wx + (ww - dlg_w) / 2;
	dlg_y = wy + (wh - dlg_h) / 2;
	cx = dlg_x + 20 * cw;			/* where the controls start */
	for (i = 0; i < 4; i++)
		ly[i] = dlg_y + ch + lh * (i + 1) + lh / 2;
	chw = 11 * cw;				/* Network | Serial */
	ch_x[0] = cx;
	ch_x[1] = cx + chw + cw;
	for (i = 0; i < 2; i++) {
		fx[i] = cx;
		fy[i] = ly[i + 1] - 3;
	}
	sp_w = 7 * cw;
	for (i = 0; i < 3; i++)
		sp_x[i] = cx + i * (sp_w + cw);
	btn_y = dlg_y + dlg_h - btn_h - ch;
	ok_x = dlg_x + dlg_w - 2 * cw - btn_w;
	cancel_x = ok_x - btn_w - 2 * cw;

	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL */
	form_dial(FMD_START, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
	graf_mouse(M_OFF, 0);
	vswr_mode(vh, 2);
	vsf_perimeter(vh, 0);
	vst_alignment(vh, 0, 5);
	clipr[0] = 0;
	clipr[1] = 0;
	clipr[2] = scr_w - 1;
	clipr[3] = scr_h - 1;
	vs_clip(vh, 1, clipr);
	dlg_frame(dlg_x, dlg_y, dlg_w, dlg_h);
	text(dlg_x + 2 * cw, dlg_y + ch / 2 + 2, "Settings", 8, 1, 1);
	text(dlg_x + 2 * cw, ly[0], "Connect through", 15, 0, 1);
	text(dlg_x + 2 * cw, ly[1], "Gateway address", 15, 0, 1);
	text(dlg_x + 2 * cw, ly[2], "Gateway port", 12, 0, 1);
	text(dlg_x + 2 * cw, ly[3], "Serial speed", 12, 0, 1);
	dlg_button(cancel_x, "Cancel", 0);
	dlg_button(ok_x, "OK", 1);

	for (;;) {
		/* the choices: the chosen one drawn inverted */
		for (i = 0; i < 5; i++) {
			short x = i < 2 ? ch_x[i] : sp_x[i - 2], w = i < 2 ? chw : sp_w, y = ly[i < 2 ? 0 : 3] - 3;
			const char *l = i == 0 ? "Network" : i == 1 ? "Serial" : sp_label[i - 2];
			short on = i < 2 ? (i == 0) == (nl == LINK_TCP) : sp_baud[i - 2] == nb, n = strlen(l);
			fill(x, y, x + w - 1, y + ch + 5, on ? 1 : 0);
			line(x, y, x + w - 1, y, 1);
			line(x + w - 1, y, x + w - 1, y + ch + 5, 1);
			line(x + w - 1, y + ch + 5, x, y + ch + 5, 1);
			line(x, y + ch + 5, x, y, 1);
			text(x + (w - n * cw) / 2, y + 3, l, n, 0, on ? 0 : 1);
		}
		/* the fields: the one with the cursor is edited */
		for (i = 0; i < 2; i++) {
			fld_x = fx[i];
			fld_y = fy[i];
			fld_cols = cols[i];
			fld_w = cols[i] * cw;
			line(fld_x, fld_y, fld_x + fld_w - 1, fld_y, 1);
			line(fld_x + fld_w - 1, fld_y, fld_x + fld_w - 1, fld_y + ch + 5, 1);
			line(fld_x + fld_w - 1, fld_y + ch + 5, fld_x, fld_y + ch + 5, 1);
			line(fld_x, fld_y + ch + 5, fld_x, fld_y, 1);
			if (i == focus)
				dlg_field(f[i], len[i], cur[i], &start[i]);
			else {
				fill(fld_x + 1, fld_y + 1, fld_x + fld_w - 2, fld_y + ch + 4, 0);
				text(fld_x + cw / 2, fld_y + 3, f[i], len[i] < cols[i] - 1 ? len[i] : cols[i] - 1, 0, 1);
			}
		}
		graf_mouse(M_ON, 0);
		if (res >= 0)
			break;
		wait_release();
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		graf_mouse(M_OFF, 0);
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			if (as == 0x0d)
				res = 1;
			else if (as == 0x1b || sc == 0x61)
				res = 0;
			else if (sc == 0x0f || sc == 0x48 || sc == 0x50)	/* Tab, up, down */
				focus ^= 1;
			else if (clip_key(e.kreturn))
				field_clip(clip_key(e.kreturn), f[focus], &len[focus], &cur[focus],
					   (focus ? 5 : 15));
			else if (sc != 0x44)		/* no Hebrew in an address */
				edit_key(f[focus], &len[focus], &cur[focus], focus ? 5 : 15, e.kstate, e.kreturn);
			f[focus][len[focus]] = 0;
		}
		if (e.which & MU_BUTTON) {
			short mx = e.mx, my = e.my;
			for (i = 0; i < 2; i++) {
				if (mx >= ch_x[i] && mx < ch_x[i] + chw && my >= ly[0] - 3 && my < ly[0] + ch + 3)
					nl = i == 0 ? LINK_TCP : LINK_SERIAL;
				if (mx >= fx[i] && mx < fx[i] + cols[i] * cw && my >= fy[i] && my < fy[i] + ch + 6)
					focus = i;
			}
			for (i = 0; i < 3; i++)
				if (mx >= sp_x[i] && mx < sp_x[i] + sp_w && my >= ly[3] - 3 && my < ly[3] + ch + 3)
					nb = sp_baud[i];
			if (my >= btn_y && my < btn_y + btn_h) {
				if (mx >= ok_x && mx < ok_x + btn_w)
					res = 1;
				else if (mx >= cancel_x && mx < cancel_x + btn_w)
					res = 0;
			}
		}
		/* an address that doesn't read as one: back to it */
		if (res == 1 && nl == LINK_TCP) {
			u32 ip;
			if (!parse_ip(f[0], &ip, 0) || !ip) {
				res = -1;
				focus = 0;
				alert(1, "[1][Please type the gateway's|IP address, for example|192.168.1.10.][ OK ]");
			}
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);
	wind_update(END_UPDATE);
	{
		short saved = text_dirty_row;
		text_dirty_row = 0;
		redraw(D_ALL, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
		text_dirty_row = saved;
	}
	form_dial(FMD_FINISH, dlg_x, dlg_y, dlg_w + 3, dlg_h + 3);
	if (res != 1)
		return;
	{
		u32 ip = 0;
		u16 port = 0;
		const char *q = f[1];
		short changed;
		parse_ip(f[0], &ip, 0);
		while (*q >= '0' && *q <= '9')
			port = port * 10 + (*q++ - '0');
		if (!port)
			port = 2323;
		changed = nl != link || (nl == LINK_TCP && (ip != tcp_ip || port != tcp_port));
		if (ip)
			tcp_ip = ip;
		tcp_port = port;
		if (nb != baud)
			set_baud(nb);
		if (changed)
			use_link(nl);		/* connects again, saves */
		else
			save_config();
	}
}

static void do_list(const char *kind);

/* Esc and Undo in the window (AERA guideline 3.5): they never throw
   away what was typed or attached; they leave Search for Chats, keeping
   its text, and let go of the item picked in the sidebar */
static void esc_key(void)
{
	if (lcur >= 0) {
		lcur = -1;
		dirty |= D_LIST;
	}
	if (search_mode)	/* back to Chats, the typing kept in the line */
		do_list("CHATS");
}

static void handle_key(short kstate, short kr)
{
	u8 ascii = kr & 0xff;
	u8 scan = (kr >> 8) & 0xff;
	short shift = kstate & (K_LSHIFT | K_RSHIFT);

	if (crash_test(scan))		/* test builds only: F1-F4 crash on purpose */
		return;
	switch (scan) {
	case 0x48: scroll_text(shift ? -(rows - 1) : -1); return;	/* up */
	case 0x50: scroll_text(shift ? rows - 1 : 1); return;		/* down */
	case 0x47: scroll_text(shift ? 32000 : -32000); return;		/* Clr/Home */
	case 0x62: help(); return;					/* Help */
	case 0x61: esc_key(); return;					/* Undo */
	case 0x52:						/* Insert: item menu */
		{
			short i = lcur;
			if (i < 0)
				for (i = 0; i < nitems && strcmp(items[i].id, cur_id); i++)
					;
			if (i >= 0 && i < nitems && i >= list_top && i < list_top + list_rows)
				context_menu(i, wx + sb_w / 2, list_y + (i - list_top + 1) * row_h + row_h / 2);
		}
		return;
	case 0x0f: move_cursor(shift ? -1 : 1); return;			/* Tab */
	case 0x3b: do_new_chat(); return;				/* F1 */
	case 0x3c: do_list("CHATS"); return;				/* F2 */
	case 0x3d: do_list("PROJECTS"); return;				/* F3 */
	case 0x3e: do_list("ARTIFACTS"); return;			/* F4 */
	case 0x3f: do_search(); return;					/* F5 */
	case 0x40: do_list("CODE"); return;				/* F6 */
	case 0x42: do_account(); return;				/* F8 */
	case 0x43: model_menu(); return;				/* F9 */
	case 0x44: toggle_hebrew(); return;				/* F10 */
	}
	if (key_ctrl(kstate, kr) && scan == 0x33) {	/* ^, (0x0c, kstate 0 if typed while busy) */
		settings_dialog();
		return;
	}
	if (scan == 0x53 && shift) {		/* Shift+Delete clears the reply line */
		inlen = in_cur = in_start = 0;
		dirty |= D_INPUT;
		return;
	}
	if (ascii == 0x08 && !inlen && natt && !search_mode) {
		/* Backspace on an empty line: the last attached file goes; the
		   bridge says so with an a line, which takes it off the list */
		tx_cmd("DETACH", att_name[natt - 1], 0);
		return;
	}
	if (clip_key(kr)) {
		reply_clip(clip_key(kr));
		return;
	}
	switch (ascii) {
	case 0x0e: do_new_chat(); return;	/* ^N */
	case 0x06: do_search(); return;		/* ^F */
	case 0x0f: choose_attach(); return;	/* ^O */
	case 0x12: reconnect(); return;		/* ^R */
	case 0x11: quit = 1; return;		/* ^Q */
	case 0x17: quit = 1; return;		/* ^W: closes the one window */
	case 0x0d:				/* Return / Enter */
		if (inlen == 0 && !search_mode && lcur >= 0 && lcur < nitems) {
			short i = lcur;
			lcur = -1;
			dirty |= D_LIST;
			open_item(i);
		} else {
			submit();
		}
		return;
	case 0x1b:				/* Esc */
		esc_key();
		return;
	}
	/* editing keys and typing (key_char handles the Hebrew layout) */
	if (edit_key(input, &inlen, &in_cur, INMAX, kstate, kr))
		dirty |= D_INPUT;
}

/* ------------------------------------------------------------------ */
/* window management                                                   */
/* ------------------------------------------------------------------ */

#define WKIND (NAME | CLOSER | FULLER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE)

static void on_resize(void)
{
	short at_end;
	if (win_icon)		/* laid out when it comes back */
		return;
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	at_end = at_bottom();
	layout();
	rewrap_all();
	if (at_end)
		top_line = nlines - rows;
	clamp_top();
	last_slider_pos = last_slider_size = -1;
	update_slider();
	set_divider_watch();
	dirty |= D_ALL;
}

static void set_curr(short x, short y, short w, short h)
{
	short minw = 48 * cw, minh = 14 * ch;
	if (w < minw)
		w = minw;
	if (h < minh)
		h = minh;
	wind_set(win, WF_CURRXYWH, x, y, w, h);
	on_resize();
}

/* VA_START (common/ui/av.c), from the desktop or another program, and
   files dropped on the window: attached to the next message, as on
   claude.ai */
/* ---- the clipboard (common/ui/clip.c) ---- */

static void say_notice(const char *s)
{
	strlcpy_(notice, s, sizeof(notice));
	notice_until = ticks + 125;
	dirty |= D_TITLE;
}

/* text into a field at the cursor: a reply is one line, so line breaks
   and tabs become spaces and control characters stay out; 0 when not
   all of it fitted */
static int field_paste(char *buf, short *len, short *cur, short max, const char *t)
{
	for (; *t; t++) {
		u8 c = (u8)*t;
		if (c == '\n' || c == '\t')
			c = ' ';
		if (c < 32 || (c == ' ' && *cur > 0 && buf[*cur - 1] == ' '))
			continue;
		if (*len >= max)
			return 0;
		memmove(buf + *cur + 1, buf + *cur, *len - *cur);
		buf[(*cur)++] = (char)c;
		(*len)++;
	}
	return 1;
}

/* ^C ^X ^V on a field with nothing selected: all of it */
static short field_clip(int what, char *buf, short *len, short *cur, short max)
{
	char t[INMAX + 1];
	if (what == CLIP_PASTE) {
		clip_get(t, sizeof(t));
		if (!field_paste(buf, len, cur, max, t))
			say_notice("Only the start fits");
		return 1;
	}
	if (!*len || !clip_put(buf, *len) || what != CLIP_CUT)
		return 0;
	*len = *cur = 0;
	return 1;
}

/* Claude's last answer, a line for each paragraph */
static void copy_answer(void)
{
	long p, s = -1, e, k = 0;
	char *t;
	for (p = tlen - 3; p >= 0 && s < 0; p--)
		if (tb[p] == MK_HEADER && tb[p + 1] == 'A' && tb[p + 2] == '\n' && (!p || tb[p - 1] == '\n'))
			s = p + 3;
	if (s < 0) {
		say_notice("No answer to copy");
		return;
	}
	for (e = s; e < tlen && !(tb[e] == MK_HEADER && tb[e - 1] == '\n'); e++)
		;
	if (!(t = malloc(e - s + 1)))
		return;
	for (p = s; p < e; p++)
		if (tb[p] == MK_INL)
			t[k++] = '`';		/* inline code as in Markdown */
		else if (tb[p] != MK_BOLD && tb[p] != MK_GREY && tb[p] != MK_CODE)
			t[k++] = tb[p];
	while (k > 0 && t[k - 1] == '\n')
		k--;
	say_notice(clip_put(t, k) ? "Copied Claude's answer" : "Couldn't write the clipboard");
	free(t);
}

/* ^C ^X ^V in the reply line; ^C on an empty one copies Claude's answer */
static void reply_clip(int what)
{
	if (what == CLIP_COPY && !inlen) {
		copy_answer();
		return;
	}
	if (field_clip(what, input, &inlen, &in_cur, INMAX)) {
		in_start = 0;
		dirty |= D_INPUT;
		if (what == CLIP_COPY)
			say_notice("Copied to the clipboard");
	}
}

static void av_start(const char *s)
{
	char path[200];
	const char *p = s;
	wind_set(win, WF_TOP, 0, 0, 0, 0);
	while ((p = av_arg(p, path, sizeof(path))) != 0)
		attach_file(path);
}

/* the iconified window (common/ui/iconify.c): the icon, and under it
   whether Claude is busy or the bridge isn't there. From a redraw
   message, area; from ClaudeST itself (0), only when that changed */
static void icon_window(GRECT *area)
{
	static const char *shown = "-";
	const char *s = busy ? "busy" : online ? "" : "offline";
	if (!area && s == shown)
		return;
	shown = s;
	icon_redraw(win, area, icon_bits, icon16_px, s);
}

static void handle_msg(short *msg)
{
	switch (msg[0]) {
	case MN_SELECTED:
		switch (mbar_command(&mb, msg[4])) {
		case C_ABOUT: about(); break;
		case C_NEW: do_new_chat(); break;
		case C_FIND: do_search(); break;
		case C_REF: reconnect(); break;
		case C_NET:
			if (tcp_ip)
				use_link(LINK_TCP);
			else
				alert(1, "[1][Type /connect and the IP|address of your gateway|in the reply line, e.g.|/connect 192.168.1.10][ OK ]");
			break;
		case C_SER: use_link(LINK_SERIAL); break;
		case C_HEB: toggle_hebrew(); break;
		case C_ACCT: do_account(); break;
		case C_FONT: font_menu(); break;
		case C_HELP: help(); break;
		case C_KEYS: keys_dialog(); break;
		case C_SET: settings_dialog(); break;
		case C_ATTACH: choose_attach(); break;
		case C_CUT: reply_clip(CLIP_CUT); break;
		case C_COPY: reply_clip(CLIP_COPY); break;
		case C_PASTE: reply_clip(CLIP_PASTE); break;
		case C_ANSWER: copy_answer(); break;
		case C_QUIT: quit = 1; break;
		case C_B48: set_baud(BAUD_4800); break;
		case C_B96: set_baud(BAUD_9600); break;
		case C_B192: set_baud(BAUD_19200); break;
		}
		menu_tnormal(mb.tree, msg[3], 1);
		break;
	case WM_REDRAW:
		if (msg[3] == win && win_icon) {
			GRECT a;
			a.x = msg[4];
			a.y = msg[5];
			a.w = msg[6];
			a.h = msg[7];
			icon_window(&a);
		} else if (msg[3] == win) {
			short saved = text_dirty_row;
			text_dirty_row = 0;
			redraw(D_ALL, msg[4], msg[5], msg[6], msg[7]);
			text_dirty_row = saved;
			if (pending_scroll) {
				/* the screen no longer matches the pre-scroll state */
				pending_scroll = 0;
				text_dirty_row = 0;
				dirty |= D_TEXT;
			}
		}
		break;
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (win_icon)
			break;
		hide_tip();
		over_divider = 0;
		graf_mouse(ARROW, 0);
		win_icon = 1;
		wind_set(win, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_UNICONIFY:
		if (!win_icon)
			break;
		wind_set(win, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		win_icon = 0;
		on_resize();
		break;
	case WM_TOPPED:
		wind_set(win, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_BOTTOMED:
		wind_set(win, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
	case AP_TERM:
		quit = 1;
		break;
	case WM_FULLED: {
		short cx, cy, cw_, ch_, fx, fy, fw, fh;
		if (win_icon)
			break;
		wind_get(win, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
		wind_get(win, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
			wind_get(win, WF_PREVXYWH, &fx, &fy, &fw, &fh);
		set_curr(fx, fy, fw, fh);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
		if (win_icon)	/* the icon goes where it is put, as it is */
			wind_set(win, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
		else
			set_curr(msg[4], msg[5], msg[6], msg[7]);
		break;
	case WM_ARROWED:
		switch (msg[4]) {
		case 0: scroll_text(-(rows - 1)); break;
		case 1: scroll_text(rows - 1); break;
		case 2: scroll_text(-1); break;
		case 3: scroll_text(1); break;
		}
		break;
	case WM_VSLID:
		if (nlines > rows) {
			short want = (short)((long)msg[4] * (nlines - rows) / 1000);
			scroll_text(want - top_line);
		}
		break;
	default: {
		const char *va;
		DROP d;
		/* dropped: files are attached, text goes into the reply line */
		switch (dd_message(msg, DD_WANT_FILES | DD_WANT_TEXT, &d)) {
		case DD_FILES:
			av_start(d.data);
			break;
		case DD_TEXT:
			wind_set(win, WF_TOP, 0, 0, 0, 0);
			if (!field_paste(input, &inlen, &in_cur, INMAX, d.data))
				say_notice("Only the start fits");
			dirty |= D_INPUT;
			break;
		case DD_NONE:
			if (av_message(msg, &va) == AV_START)
				av_start(va);
		}
		dd_done(&d);
		break;
	}
	}
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static short alloc_buffers(void)
{
	long sizes[3] = { 98304L, 49152L, 24576L };
	short i;
	for (i = 0; i < 3; i++) {
		tcap = sizes[i];
		maxlines = (short)(tcap / 16);
		tb = Malloc(tcap + (long)maxlines * sizeof(LINE));
		if ((long)tb > 0) {
			lines = (LINE *)(tb + tcap);
			return 1;
		}
	}
	return 0;
}

/* ClaudeST crashed and CRASH.LOG is written (common/atari/crash.c): say
   so, let the connection and the serial port go, and leave GEM tidy */
static void crash_quit(const char *what)
{
	char a[200];
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof(a), "[3][ClaudeST has crashed|(%s).|What happened is in|CRASH.LOG, next to CLAUDEST.PRG.][ Quit ]", what);
	form_alert(1, a);
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	serial_close();
	if (win > 0) {
		wind_close(win);
		wind_delete(win);
	}
	if (mb.tree)
		menu_bar(mb.tree, 0);
	v_clsvwk(vh);
	appl_exit();
}

int main(void)
{
	short work_out[57], dx, dy, dw, dh, wbox, hbox;
	short msg[8];
	AEVENT ev;

	apid = appl_init();
	if (apid < 0)
		return 1;
	phys = graf_handle(&cw, &ch, &wbox, &hbox);
	sys_cw = cw;
	sys_ch = ch;
	{
		/* our folder, for the help file: the current one at start */
		short drv = (short)Dgetdrv();
		home_dir[0] = 'A' + drv;
		home_dir[1] = ':';
		home_dir[2] = 0;
		Dgetpath(home_dir + 2, 0);
		if (home_dir[strlen(home_dir) - 1] != '\\')
			strcat(home_dir, "\\");
	}
	crash_install(home_dir, "ClaudeST", APP_VERSION);
	crash_cleanup = crash_quit;
	help_init();			/* memory the help viewer may read */
	av_init("CLAUDEST");		/* the desktop's AV server, if there is one */
	vh = v_opnvwk_(phys, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	ncolors = work_out[13];
	if (!screen_check("ClaudeST", scr_w)) {	/* not in ST low */
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}

	if (!alloc_buffers()) {
		alert(1, "[3][ClaudeST: not enough memory.][ Quit ]");
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}

	if (mbar_build(&mb, bar, sys_cw, sys_ch, scr_w, scr_h)) {
		alert(1, "[3][ClaudeST: not enough memory.][ Quit ]");
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}
	menu_check_baud();
	menu_bar(mb.tree, 1);
	graf_mouse(ARROW, 0);

	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	desk_x = dx;
	desk_y = dy;
	desk_w = dw;
	desk_h = dh;
	vq_extnd(vh, 1, work_out);
	planes = work_out[4];
	win = wind_create(icon_kind(WKIND), dx, dy, dw, dh);
	if (win < 0) {
		alert(1, "[3][ClaudeST: no window available.][ Quit ]");
		menu_bar(mb.tree, 0);
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}
	set_window_name();
	wind_open(win, dx, dy, dw, dh);
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	load_config();
	font_apply();		/* the text font saved in CLAUDEST.INF */
	layout();
	set_divider_watch();

	begin_message('I');
	{
		static const char hi[] = "Welcome to ClaudeST. Connecting to your Claude "
			"gateway (claude_bridge.py) over the network or the serial port...";
		append(hi, sizeof(hi) - 1);
	}

	menu_check_baud();
	if (link == LINK_SERIAL) {
		serial_open();
		send_hello();
	}
	dirty = D_ALL;
	if (crash_previous()) {
		flush_dirty();
		crash_offer("ClaudeST", "next to CLAUDEST.PRG");
	}

	while (!quit) {
		/* 0x101/3/0: wake on any button press, left or right */
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_MESAG | MU_TIMER | (win_icon ? 0 : MU_M1),
			    0x101, 3, 0, 40, msg, &ev);
		if (win_icon)	/* an icon takes no keys or clicks */
			ev.which &= ~(MU_KEYBD | MU_BUTTON);
		ticks++;
		tip_poll(ev.which);
		if (ev.which & MU_MESAG)
			handle_msg(msg);
		if (ev.which & MU_KEYBD)
			handle_key(ev.kstate, ev.kreturn);
		if (ev.which & MU_M1) {
			/* hand cursor over the divider */
			over_divider = !over_divider;
			graf_mouse(over_divider ? FLAT_HAND : ARROW, 0);
			set_divider_watch();
		}
		if (ev.which & MU_BUTTON) {
			if (ev.mbutton & 2)
				handle_rclick(ev.mx, ev.my);
			else
				handle_click(ev.mx, ev.my);
			/* act on the press: keep drawing and receiving while the
			 * button is still down, so one press acts only once */
			for (;;) {
				short bx, by, bb, bk, m2[8];
				AEVENT e2;
				flush_dirty();
				graf_mkstate(&bx, &by, &bb, &bk);
				if (!(bb & 3)) {
					if (nav_pressed >= 0) {
						nav_pressed = -1;
						dirty |= D_SIDEBAR;
						flush_dirty();
					}
					break;
				}
				evnt_multi_(MU_TIMER, 0, 0, 0, 20, m2, &e2);
				poll_link();
			}
		}
		poll_link();
		if (notice[0] && ticks > notice_until) {
			notice[0] = 0;
			dirty |= D_TITLE | D_SIDEBAR;
		}
		if (dirty && tip_on)
			hide_tip();	/* keep the tip from being drawn over */
		/* keep saying hello until the bridge answers (~every 5s) */
		{
			/* the working dots pulse, about twice a second */
			static long pulse_at;
			if (ticks - pulse_at >= 12) {
				short i;
				pulse_at = ticks;
				dot_pulse ^= 1;
				for (i = list_top; i < nitems && i < list_top + list_rows; i++)
					if (items[i].state == 'W')
						redraw_dot(i);
			}
		}
		if (!online && ticks - last_hello > 125 && (link == LINK_SERIAL || tcp_cn >= 0))
			send_hello();
		heartbeat();
		flush_dirty();
	}

	tx_cmd("BYE", 0, 0);
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	if (tcp_opening >= 0)
		sting_close(tcp_opening);
	serial_close();
	wind_close(win);
	wind_delete(win);
	menu_bar(mb.tree, 0);
	av_exit();
	v_clsvwk(vh);
	Mfree(tb);
	crash_remove();
	appl_exit();
	return 0;
}
