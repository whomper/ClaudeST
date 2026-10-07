/*
 * Claude ST - a Claude.ai client for the Atari ST, STE, TT and Falcon.
 *
 * A single GEM window: the sidebar on the left (New chat, Search, Chats,
 * Projects, Artifacts and the current list), the active conversation on
 * the right with an input line underneath. The machine talks a small
 * line protocol over the serial port (AUX:) to claude_bridge.py running
 * on any modern computer, which does the TLS/HTTP work against claude.ai.
 * See ../PROTOCOL.md.
 */
#include "tos.h"
#include "gem.h"
#include "sting.h"
#include "bidi.h"
#include "icon.h"
#include "icon16.h"

#define VERSION "1.14"

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static short apid, phys, vh;
static short cw, ch;			/* character cell */
static short ncolors;
static short scr_w, scr_h;
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
#define L_TYPE   0x0f
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
	char heading;		/* a date heading ("Today"), not an item */
} ITEM;

#define MAXITEMS 300
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
static char save_path[160];

/* dirty regions */
#define D_SIDEBAR 1
#define D_TITLE   2
#define D_TEXT    4
#define D_INPUT   8
#define D_STATUS  16
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
static char txbuf[1200];
static short txlen;

static char rxline[1200];
static short rxlen;
static long ticks, last_hello;

/* Claude model and effort, offered by the bridge (O/V/U/K lines) */
#define NMODEL 12
#define NEFFORT 6
static char mod_id[NMODEL][28], mod_lab[NMODEL][20];
static char eff_id[NEFFORT][8], eff_lab[NEFFORT][14];
static short nmodel, neffort;
static char cur_model[28], cur_effort[8];
static char pref_model[28], pref_effort[8];	/* the user's choice, kept in CLAUDE.INF */
static short pref_sent;				/* sent to the bridge since HELLO */
static short chip_x0, chip_x1;			/* the model chip in the reply line */

static void tx_cmd(const char *a, const char *b, const char *c);
static void show_picker(void);
static void choose_save_file(const char *name);
static void save_chunk(const char *hex);
static void finish_save(void);
static void save_config(void);

/* ------------------------------------------------------------------ */
/* menu bar (built in code, so no .RSC file is needed)                 */
/* ------------------------------------------------------------------ */

static char t_desk[] = " Desk ";
static char t_file[] = " File ";
static char t_opts[] = " Options ";
static char m_about[] = "  About Claude ST... ";
static char m_sep1[]  = "---------------------";
static char m_acc[6][22] = {
	"  Desk Accessory 1   ", "  Desk Accessory 2   ", "  Desk Accessory 3   ",
	"  Desk Accessory 4   ", "  Desk Accessory 5   ", "  Desk Accessory 6   "
};
static char m_new[]   = "  New chat      ^N ";
static char m_find[]  = "  Search chats  ^F ";
static char m_ref[]   = "  Reconnect     ^R ";
static char m_sep2[]  = "-------------------";
static char m_quit[]  = "  Quit          ^Q ";
static char m_b48[]   = "  4800 baud       ";
static char m_b96[]   = "  9600 baud       ";
static char m_b192[]  = "  19200 baud      ";
static char m_sep3[]  = "------------------";
static char m_net[]   = "  Network (STinG) ";
static char m_ser[]   = "  Serial port     ";
static char m_sep4[]  = "------------------";
static char m_heb[]   = "  Hebrew keys F10 ";
static char m_acct[]  = "  Account...   F8 ";

enum {
	MN_ROOT, MN_BAR, MN_ACTIVE, MN_TDESK, MN_TFILE, MN_TOPTS, MN_SCREEN,
	MN_DDESK, MN_ABOUT, MN_SEP1, MN_ACC1, MN_ACC2, MN_ACC3, MN_ACC4, MN_ACC5, MN_ACC6,
	MN_DFILE, MN_NEW, MN_FIND, MN_REF, MN_SEP2, MN_QUIT,
	MN_DOPTS, MN_B48, MN_B96, MN_B192, MN_SEP3, MN_NET, MN_SER, MN_SEP4, MN_HEB,
	MN_ACCT,
	MN_COUNT
};

#define S(p) ((long)(p))
static OBJECT menu[MN_COUNT] = {
	/* next  head tail type    flags state spec       x  y  w   h */
	{ -1, 1, 6,  G_IBOX, 0, 0, 0,          0, 0, 80, 25 },		/* ROOT */
	{ 6, 2, 2,   G_BOX,  0, 0, 0x1100L,    0, 0, 80, 0x201 },	/* BAR */
	{ 1, 3, 5,   G_IBOX, 0, 0, 0,          2, 0, 21, 0x301 },	/* ACTIVE */
	{ 4, -1, -1, G_TITLE, 0, 0, 0,         0, 0, 6, 0x301 },
	{ 5, -1, -1, G_TITLE, 0, 0, 0,         6, 0, 6, 0x301 },
	{ 2, -1, -1, G_TITLE, 0, 0, 0,         12, 0, 9, 0x301 },
	{ 0, 7, 22,  G_IBOX, 0, 0, 0,          0, 0x301, 80, 19 },	/* SCREEN */
	{ 16, 8, 15, G_BOX,  0, 0, 0xFF1100L, 2, 0, 21, 8 },		/* desk drop */
	{ 9, -1, -1, G_STRING, 0, 0, 0,        0, 0, 21, 1 },
	{ 10, -1, -1, G_STRING, 0, DISABLED, 0, 0, 1, 21, 1 },
	{ 11, -1, -1, G_STRING, 0, 0, 0,       0, 2, 21, 1 },
	{ 12, -1, -1, G_STRING, 0, 0, 0,       0, 3, 21, 1 },
	{ 13, -1, -1, G_STRING, 0, 0, 0,       0, 4, 21, 1 },
	{ 14, -1, -1, G_STRING, 0, 0, 0,       0, 5, 21, 1 },
	{ 15, -1, -1, G_STRING, 0, 0, 0,       0, 6, 21, 1 },
	{ 7, -1, -1, G_STRING, 0, 0, 0,        0, 7, 21, 1 },
	{ 22, 17, 21, G_BOX, 0, 0, 0xFF1100L,  8, 0, 19, 5 },		/* file drop */
	{ 18, -1, -1, G_STRING, 0, 0, 0,       0, 0, 19, 1 },
	{ 19, -1, -1, G_STRING, 0, 0, 0,       0, 1, 19, 1 },
	{ 20, -1, -1, G_STRING, 0, 0, 0,       0, 2, 19, 1 },
	{ 21, -1, -1, G_STRING, 0, DISABLED, 0, 0, 3, 19, 1 },
	{ 16, -1, -1, G_STRING, 0, 0, 0,       0, 4, 19, 1 },
	{ 6, 23, 31, G_BOX,  0, 0, 0xFF1100L,  14, 0, 18, 9 },		/* options drop */
	{ 24, -1, -1, G_STRING, 0, 0, 0,       0, 0, 18, 1 },
	{ 25, -1, -1, G_STRING, 0, 0, 0,       0, 1, 18, 1 },
	{ 26, -1, -1, G_STRING, 0, 0, 0,       0, 2, 18, 1 },
	{ 27, -1, -1, G_STRING, 0, DISABLED, 0, 0, 3, 18, 1 },
	{ 28, -1, -1, G_STRING, 0, 0, 0,       0, 4, 18, 1 },
	{ 29, -1, -1, G_STRING, 0, 0, 0,       0, 5, 18, 1 },
	{ 30, -1, -1, G_STRING, 0, DISABLED, 0, 0, 6, 18, 1 },
	{ 31, -1, -1, G_STRING, 0, 0, 0,       0, 7, 18, 1 },
	{ 22, -1, -1, G_STRING, LASTOB, 0, 0,  0, 8, 18, 1 },
};

static void menu_init(void)
{
	short i, cols_ = scr_w / cw;

	menu[MN_TDESK].ob_spec = S(t_desk);
	menu[MN_TFILE].ob_spec = S(t_file);
	menu[MN_TOPTS].ob_spec = S(t_opts);
	menu[MN_ABOUT].ob_spec = S(m_about);
	menu[MN_SEP1].ob_spec = S(m_sep1);
	for (i = 0; i < 6; i++)
		menu[MN_ACC1 + i].ob_spec = S(m_acc[i]);
	menu[MN_NEW].ob_spec = S(m_new);
	menu[MN_FIND].ob_spec = S(m_find);
	menu[MN_REF].ob_spec = S(m_ref);
	menu[MN_SEP2].ob_spec = S(m_sep2);
	menu[MN_QUIT].ob_spec = S(m_quit);
	menu[MN_B48].ob_spec = S(m_b48);
	menu[MN_B96].ob_spec = S(m_b96);
	menu[MN_B192].ob_spec = S(m_b192);
	menu[MN_SEP3].ob_spec = S(m_sep3);
	menu[MN_NET].ob_spec = S(m_net);
	menu[MN_SER].ob_spec = S(m_ser);
	menu[MN_SEP4].ob_spec = S(m_sep4);
	menu[MN_HEB].ob_spec = S(m_heb);
	menu[MN_ACCT].ob_spec = S(m_acct);

	menu[MN_ROOT].ob_width = cols_;
	menu[MN_ROOT].ob_height = scr_h / ch;
	menu[MN_BAR].ob_width = cols_;
	menu[MN_SCREEN].ob_width = cols_;
	for (i = 0; i < MN_COUNT; i++)
		rsrc_obfix(menu, i);
}

static void menu_check_baud(void)
{
	menu[MN_B48].ob_state = baud == BAUD_4800 ? CHECKED : 0;
	menu[MN_B96].ob_state = baud == BAUD_9600 ? CHECKED : 0;
	menu[MN_B192].ob_state = baud == BAUD_19200 ? CHECKED : 0;
	menu[MN_NET].ob_state = link == LINK_TCP ? CHECKED : 0;
	menu[MN_SER].ob_state = link == LINK_SERIAL ? CHECKED : 0;
	menu[MN_HEB].ob_state = hebrew_kbd ? CHECKED : 0;
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

static void sup_set_iorec(void)
{
	short sr = ints_off();
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
	short sr = ints_off();
	iorec->ibuf = old_ibuf;
	iorec->ibufsiz = old_ibufsiz;
	iorec->ibufhd = 0;
	iorec->ibuftl = 0;
	iorec->ibuflow = old_low;
	iorec->ibufhi = old_high;
	ints_restore(sr);
}

static short aux_port;		/* CLAUDE.INF "port N": Bconmap device, 0 = TOS's choice */
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
	old_ibuf = iorec->ibuf;
	old_ibufsiz = iorec->ibufsiz;
	old_low = iorec->ibuflow;
	old_high = iorec->ibufhi;
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
				EVENT e;
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
}

static void send_hello(void)
{
	pref_sent = 0;
	tx_cmd("HELLO", "1", VERSION);
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
			}
			/* the paragraph's direction comes from its first strong letter */
			if (bidi_is_rtl(tb + s, e - s > 2000 ? 2000 : (short)(e - s)))
				kind |= L_RTL;
			w = wrap_width(role);
			while (s < e) {
				long b;
				if (e - s <= w) {
					add_line(s, (short)(e - s), kind, role);
					break;
				}
				for (b = s + w; b > s && tb[b] != ' '; b--)
					;
				if (b == s) {
					add_line(s, w, kind, role);
					s += w;
				} else {
					add_line(s, (short)(b - s), kind, role);
					s = b + 1;
				}
			}
		}
		if (e >= tlen)
			break;
		p = e + 1;
	}
}

static short is_para_start(long off)
{
	return off == 0 || tb[off - 1] == '\n' || tb[off - 1] == MK_BOLD;
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
		if (li < nlines && (lines[li].kind & L_TYPE) == L_BOLD && p > 0 && tb[p - 1] == MK_BOLD)
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

static void text(short x, short y, const char *s, short n, short fx, short color)
{
	vst_effects(vh, fx);
	vst_color(vh, color);
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
	status_y = wy + wh - row_h;
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

static void draw_sidebar(void)
{
	short i, y, maxc = sb_w / cw - 2;
	short x = wx + cw;

	fill(wx, wy, wx + sb_w - 1, wy + wh - 1, 0);
	line(wx + sb_w, wy, wx + sb_w, wy + wh - 1, 1);

	/* brand */
	if (row_visible(wy, wy + title_h)) {
		spark(x + cw / 2, wy + title_h / 2, ch / 2 - 1);
		text(x + 2 * cw, wy + (title_h - ch) / 2, "Claude", 6, 1, 1);
		text(x + 9 * cw, wy + (title_h - ch) / 2, "ST", 2, 0, 1);
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

	/* status line */
	line(wx, status_y - 2, wx + sb_w - 1, status_y - 2, 1);
	{
		short n = strlen(status);
		if (n > maxc - 1)
			n = maxc - 1;
		fill(x, status_y + row_h / 2 - 2, x + 3, status_y + row_h / 2 + 1, online ? accent() : 1);
		if (!online)
			fill(x + 1, status_y + row_h / 2 - 1, x + 2, status_y + row_h / 2, 0);
		text_bidi(x + cw, status_y + (row_h - ch) / 2, status, n, 0, 1, 0);
	}
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
	short cl = model_chip(chip), scw, sch, big = ch >= 16;

	fill(px, wy, px + pw - 1, wy + title_h, 0);
	/* the model chip ("Opus 5.5 · Med") at the right, in the small system
	 * font (8x8 in ST high, 6x6 in ST medium); the title keeps at least 16
	 * characters, or the chip goes */
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
	vst_height(vh, big ? 13 : 6, &scw, &sch);
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
			short fx = (l->kind & L_TYPE) == L_BOLD ? 1 : 0;
			short rtl = (l->kind & L_RTL) != 0;
			if (rtl || bidi_has_rtl(tb + l->off, l->len)) {
				static char vis[BIDI_MAX];
				short n = l->len > BIDI_MAX ? BIDI_MAX : l->len;
				bidi_visual(tb + l->off, n, rtl, vis);
				if (rtl)	/* right-aligned, like dir="rtl" */
					x = tx + cols * cw - n * cw;
				text(x, y, vis, n, fx, 1);
			} else {
				text(x, y, tb + l->off, l->len, fx, 1);
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
static short edit_key(char *buf, short *len, short *cur, short max, short kstate, short kr)
{
	u8 ascii = kr & 0xff, scan = (kr >> 8) & 0xff, c;
	short shift = kstate & (K_LSHIFT | K_RSHIFT);
	short ctrl = (kstate & K_CTRL) || scan == 0x73 || scan == 0x74;
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
	if (inlen == 0 && !search_mode) {
		const char *ph = busy ? "Claude is replying..." : "Reply to Claude...";
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

	if (win < 0)
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
			if (mask & D_SIDEBAR)
				draw_sidebar();
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

static void flush_dirty(void)
{
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
		if (dirty & D_SIDEBAR)
			redraw(D_SIDEBAR, wx, wy, sb_w + 1, wh);
		else if (dirty & D_STATUS)
			redraw(D_SIDEBAR, wx, status_y - 1, sb_w, wy + wh - status_y + 1);
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

static void handle_line(char *s)
{
	char *f[4];
	short n = split(s, f, 4);
	char c = f[0][0];

	if (!online) {
		online = 1;
		dirty |= D_STATUS;
	}
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
			items[nitems].pinned = n > 3 && f[3][0] == 'P';
			items[nitems].heading = 0;
			nitems++;
		}
		break;
	case 'X':			/* X <label> : a heading in the list ("Today") */
		if (!drop_list && nitems < MAXITEMS && n > 1) {
			items[nitems].id[0] = 0;
			strlcpy_(items[nitems].label, f[1], sizeof(items[0].label));
			items[nitems].pinned = 0;
			items[nitems].heading = 1;
			nitems++;
		}
		break;
	case 'F':			/* F <name>: where to save the artifact? */
		if (n > 1 && save_id[0])
			choose_save_file(f[1]);
		break;
	case 'D':			/* D <hex>: a piece of the file being saved */
		if (save_fd >= 0 && n > 1)
			save_chunk(f[1]);
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
		dirty |= D_SIDEBAR;
		break;
	case 'C':			/* C <id> : current chat (for highlight) */
		/* just started, the pane is empty: don't highlight the chat
		 * the bridge kept from our last run */
		if (fresh)
			break;
		if (strcmp(cur_id, n > 1 ? f[1] : "")) {
			strlcpy_(cur_id, n > 1 ? f[1] : "", sizeof(cur_id));
			dirty |= D_SIDEBAR;
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
		/* after (re)connecting, ask for the model saved in CLAUDE.INF */
		if (!pref_sent) {
			pref_sent = 1;
			if (pref_model[0] && (strcmp(pref_model, cur_model) ||
					      strcmp(pref_effort, cur_effort)))
				tx_cmd("CHOOSE", pref_model, pref_effort);
		}
		break;
	case 'Y':			/* Y 0|1 : busy */
		busy = n > 1 && f[1][0] == '1';
		dirty |= D_TITLE | D_INPUT;
		break;
	case 'A': {			/* A <text> : alert box */
		static char buf[200];
		char *p = buf;
		const char *q = n > 1 ? f[1] : "";
		const char *pre = "[1][", *post = "][ OK ]";
		while (*pre)
			*p++ = *pre++;
		while (*q && p < buf + 180)
			*p++ = (*q == '[' || *q == ']') ? '|' : *q, q++;
		while (*post)
			*p++ = *post++;
		*p = 0;
		flush_dirty();
		form_alert(1, buf);
		break;
	}
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
	flush_dirty();
	graf_mouse(BUSYBEE, 0);
	cn = sting_open(tcp_ip, tcp_port);
	if (cn >= 0) {
		r = sting_wait_established(cn, 8);
		if (r < 0) {
			sting_close(cn);
			cn = r;
		}
	}
	graf_mouse(ARROW, 0);
	if (cn < 0) {
		strcpy(msg, "Offline: ");
		strlcpy_(msg + 9, sting_error(cn), sizeof(msg) - 9);
		set_status(msg);
		return;
	}
	tcp_cn = cn;
	rxlen = 0;
	set_status("Connected, loading...");
	send_hello();
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
		if (ticks >= next_connect)
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

/* ---- settings: CLAUDE.INF next to the program ---- */

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

static void load_config(void)
{
	char buf[256], *p, *l;
	long fd = Fopen("CLAUDE.INF", 0);
	long n;
	if (fd < 0)
		return;
	n = Fread((short)fd, sizeof(buf) - 1, buf);
	Fclose((short)fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	for (l = p = buf; ; p++) {
		if (*p == '\r' || *p == '\n' || *p == 0) {
			char end = *p;
			*p = 0;
			if (p > l)
				parse_config_line(l);
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

static void save_config(void)
{
	char text[160], *p = text;
	long fd = Fcreate("CLAUDE.INF", 0);
	if (fd < 0)
		return;
	if (link == LINK_TCP && tcp_ip) {
		strcpy(p, "tcp ");
		ip_to_str(tcp_ip, p + 4);
		p += strlen(p);
		*p++ = ' ';
		p = put_num(p, tcp_port);
	} else {
		strcpy(p, "serial");
		p += 6;
	}
	*p++ = '\r';
	*p++ = '\n';
	if (sb_user) {
		strcpy(p, "sidebar ");
		p = put_num(p + 8, sb_user);
		*p++ = '\r';
		*p++ = '\n';
	}
	strcpy(p, baud == BAUD_4800 ? "baud 4800\r\n" : baud == BAUD_9600 ? "baud 9600\r\n" : "baud 19200\r\n");
	p += strlen(p);
	if (hebrew_kbd) {
		strcpy(p, "keyboard hebrew\r\n");
		p += strlen(p);
	}
	if (aux_port) {
		strcpy(p, "port 6\r\n");
		p[5] = '0' + aux_port;
		p += strlen(p);
	}
	if (pref_model[0]) {
		strcpy(p, "model ");
		strcat(p, pref_model);
		if (pref_effort[0]) {
			strcat(p, " ");
			strcat(p, pref_effort);
		}
		strcat(p, "\r\n");
		p += strlen(p);
	}
	*p = 0;
	Fwrite((short)fd, strlen(text), text);
	Fclose((short)fd);
}

static void use_link(short l)
{
	if (link == LINK_TCP && tcp_cn >= 0)
		sting_close(tcp_cn);
	tcp_cn = -1;
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
	static char name[] = " Claude ";
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
			form_alert(1, "[1][Use: /connect 192.168.1.10|or /connect 192.168.1.10:2323|(your gateway's address)][ OK ]");
			return;
		}
	} else if (!search_mode && !strcmp(input, "/serial")) {
		use_link(LINK_SERIAL);
	} else if (search_mode) {
		tx_cmd("FIND", input, 0);
		search_mode = 0;
		dirty |= D_SIDEBAR;
	} else if (inlen > 0) {
		if (fresh) {		/* not into the chat from our last run */
			tx_cmd("NEW", "QUIET", 0);
			fresh = 0;
		}
		tx_cmd("SEND", input, 0);
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
		dirty |= D_SIDEBAR | D_TITLE;
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
		dirty |= D_SIDEBAR;
}

static void about(void);

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

static void reconnect(void)
{
	if (link == LINK_TCP) {
		if (tcp_cn >= 0)
			sting_close(tcp_cn);
		tcp_cn = -1;
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
	EVENT e;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_multi_(MU_TIMER, 0, 0, 0, 10, m, &e);
	}
}

static short pop_x, pop_y, pop_w, pop_ih;

static void pop_item(const char *const *lab, short i, short on)
{
	short y = pop_y + 2 + i * pop_ih;
	short x1 = pop_x + 1, x2 = pop_x + pop_w - 2;
	if (lab[i][0] == '-') {
		fill(x1, y, x2, y + pop_ih - 1, 0);
		line(x1 + 2, y + pop_ih / 2, x2 - 2, y + pop_ih / 2, 1);
		return;
	}
	fill(x1, y, x2, y + pop_ih - 1, on ? 1 : 0);
	text(pop_x + 2 * cw, y + (pop_ih - ch) / 2, lab[i], strlen(lab[i]), 0, on ? 0 : 1);
}

static short pop_next(const char *const *lab, short n, short from, short d)
{
	short i = from;
	do {
		i += d;
		if (i < 0)
			i = n - 1;
		if (i >= n)
			i = 0;
	} while (lab[i][0] == '-' && i != from);
	return i;
}

/* a GEM-style popup at (x, y); returns the chosen item or -1.
 * Mouse: click an item (or press-drag-release). Keys: arrows, Return, Esc. */
static short popup(short x, short y, short alt_y, const char *const *lab, short n)
{
	short i, w = 0, sel = -1, res = -1, held, moved = 0;
	short mx, my, mb, ks, ox, oy, m[8], clipr[4];
	EVENT e;

	for (i = 0; i < n; i++)
		if ((short)strlen(lab[i]) > w)
			w = strlen(lab[i]);
	pop_ih = row_h;
	pop_w = (w + 4) * cw;
	{
		short bh = n * pop_ih + 4;
		if (x + pop_w + 2 > wx + ww)
			x = wx + ww - pop_w - 2;
		if (x < wx)
			x = wx;
		if (y + bh + 2 > wy + wh)
			y = alt_y >= 0 ? alt_y - bh - 2 : wy + wh - bh - 2;
		if (y < wy)
			y = wy;
		pop_x = x;
		pop_y = y;

		wind_update(BEG_UPDATE);
		wind_update(3);		/* BEG_MCTRL: we own the mouse */
		form_dial(FMD_START, x, y, pop_w + 3, bh + 3);
		graf_mouse(M_OFF, 0);
		vswr_mode(vh, 2);
		vsf_perimeter(vh, 0);
		vst_alignment(vh, 0, 5);
		clipr[0] = 0;
		clipr[1] = 0;
		clipr[2] = scr_w - 1;
		clipr[3] = scr_h - 1;
		vs_clip(vh, 1, clipr);
		fill(x + 2, y + 2, x + pop_w + 2, y + bh + 2, 1);	/* shadow */
		fill(x, y, x + pop_w - 1, y + bh - 1, 0);
		line(x, y, x + pop_w - 1, y, 1);
		line(x + pop_w - 1, y, x + pop_w - 1, y + bh - 1, 1);
		line(x + pop_w - 1, y + bh - 1, x, y + bh - 1, 1);
		line(x, y + bh - 1, x, y, 1);
		for (i = 0; i < n; i++)
			pop_item(lab, i, 0);
		graf_mouse(M_ON, 0);
	}

	graf_mkstate(&ox, &oy, &held, &ks);
	held &= 3;
	for (;;) {
		short hit = -1, nsel, ev_x, ev_y;
		/* button events come from the AES queue, so even a very quick
		 * click is never missed; while the opening button is still held
		 * we wait for its release instead */
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_TIMER,
			    held ? 1 : 0x101, 3, 0, 20, m, &e);
		graf_mkstate(&mx, &my, &mb, &ks);
		if (e.which & MU_BUTTON) {
			ev_x = e.mx;
			ev_y = e.my;
		} else {
			ev_x = mx;
			ev_y = my;
		}
		if (ev_x - ox > 3 || ox - ev_x > 3 || ev_y - oy > 3 || oy - ev_y > 3)
			moved = 1;
		if (ev_x >= pop_x && ev_x < pop_x + pop_w && ev_y >= pop_y + 2 &&
		    ev_y < pop_y + 2 + n * pop_ih) {
			hit = (ev_y - pop_y - 2) / pop_ih;
			if (lab[hit][0] == '-')
				hit = -1;
		}
		nsel = sel;
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			if (as == 0x1b || sc == 0x61)
				break;
			if (sc == 0x48)
				nsel = pop_next(lab, n, sel < 0 ? 0 : sel, -1);
			else if (sc == 0x50)
				nsel = pop_next(lab, n, sel < 0 ? n - 1 : sel, 1);
			else if ((as == 0x0d || as == ' ') && sel >= 0) {
				res = sel;
				break;
			}
		} else if (moved) {
			nsel = hit;
		}
		if (nsel != sel) {
			graf_mouse(M_OFF, 0);
			if (sel >= 0)
				pop_item(lab, sel, 0);
			if (nsel >= 0)
				pop_item(lab, nsel, 1);
			graf_mouse(M_ON, 0);
			sel = nsel;
		}
		if (!(e.which & MU_BUTTON))
			continue;
		if (held) {				/* the opening button came up */
			held = 0;
			if (moved && hit >= 0) {	/* press, drag, release */
				res = hit;
				break;
			}
		} else {				/* click: an item, or outside to cancel */
			res = hit;
			break;
		}
	}
	vs_clip(vh, 0, clipr);
	wind_update(2);		/* END_MCTRL */
	wind_update(END_UPDATE);
	wait_release();
	/* repaint our part now, and let the AES repaint anything else covered */
	{
		short saved = text_dirty_row;
		text_dirty_row = 0;
		redraw(D_ALL, pop_x, pop_y, pop_w + 3, n * pop_ih + 4 + 3);
		text_dirty_row = saved;
	}
	form_dial(FMD_FINISH, pop_x, pop_y, pop_w + 3, n * pop_ih + 4 + 3);
	return res;
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
	EVENT e;

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

/* ---- the About box ---- */

/* the 32x32 icon scaled by sx, sy (2x on square pixels; 2x1 in ST medium,
 * whose pixels are twice as tall as wide) */
static unsigned short icon_big[64 * 4];

static void draw_icon(short x, short y, short sx, short sy)
{
	MFDB src, dst;
	short pxy[8], colors[2], r, c, k, wpl = 2 * sx;
	short w = ICON_W * sx, h = ICON_H * sy;

	memset(icon_big, 0, sizeof(icon_big));
	for (r = 0; r < ICON_H; r++) {
		for (c = 0; c < ICON_W; c++) {
			if (!(icon_bits[r * 2 + c / 16] & (0x8000 >> (c & 15))))
				continue;
			for (k = 0; k < sx; k++) {
				short bx = c * sx + k, yy;
				for (yy = 0; yy < sy; yy++)
					icon_big[(r * sy + yy) * wpl + bx / 16] |= 0x8000 >> (bx & 15);
			}
		}
	}
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = icon_big;
	src.fd_w = w;
	src.fd_h = h;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = w - 1;
	pxy[3] = h - 1;
	pxy[4] = x;
	pxy[5] = y;
	pxy[6] = x + w - 1;
	pxy[7] = y + h - 1;
	colors[0] = 1;		/* set bits black */
	colors[1] = 0;
	vrt_cpyfm(vh, 2, pxy, &src, &dst, colors);	/* transparent */
}

/* The 16-colour icon, drawn as runs of VDI fills so that the pen numbers
 * mean the same thing in every video mode. Pens 8..15 are given the
 * icon's colours while the About box is open (see about()). */
static void draw_icon16(short x, short y, short sx, short sy)
{
	short r, c, p[4];
	vsf_interior(vh, 1);
	for (r = 0; r < ICON_H; r++) {
		for (c = 0; c < ICON_W; ) {
			u8 b = icon16_px[r * 16 + c / 2];
			short pen = (c & 1) ? (b & 15) : (b >> 4), e = c + 1;
			for (;;) {
				u8 b2;
				short pen2;
				if (e >= ICON_W)
					break;
				b2 = icon16_px[r * 16 + e / 2];
				pen2 = (e & 1) ? (b2 & 15) : (b2 >> 4);
				if (pen2 != pen)
					break;
				e++;
			}
			if (pen) {
				vsf_color(vh, pen);
				p[0] = x + c * sx;
				p[1] = y + r * sy;
				p[2] = x + e * sx - 1;
				p[3] = y + r * sy + sy - 1;
				vr_recfl(vh, p);
			}
			c = e;
		}
	}
}

static void center_text(short y, const char *s, short fx)
{
	short n = strlen(s);
	text(dlg_x + (dlg_w - n * cw) / 2, y, s, n, fx, 1);
}

static void about(void)
{
	static char conn[64];
	short sx = 2, sy = ch >= 16 ? 2 : 1;
	short ih = ICON_H * sy, lh = ch + (ch >= 16 ? 2 : 1);
	short m[8], clipr[4], y, done = 0, i;
	short saved_rgb[8][3], colour = ncolors >= 16;
	EVENT e;

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

	dlg_w = 62 * cw;
	if (dlg_w > ww - 2 * cw)
		dlg_w = ww - 2 * cw;
	/* the content, top to bottom, sets the height (see the drawing below) */
	btn_h = ch + 6;
	dlg_h = lh + ih + lh / 2 + lh + (lh + lh / 2) + (lh + lh / 2) + lh / 4 + lh +
		(lh + lh / 2) + (lh + lh / 2) + 3 * lh + lh / 2 + btn_h + lh;
	if (dlg_h > wh - 4)
		dlg_h = wh - 4;
	dlg_x = wx + (ww - dlg_w) / 2;
	dlg_y = wy + (wh - dlg_h) / 2;
	btn_w = 10 * cw;
	btn_y = dlg_y + dlg_h - btn_h - lh;
	ok_x = dlg_x + (dlg_w - btn_w) / 2;

	wind_update(BEG_UPDATE);
	wind_update(3);
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

	y = dlg_y + lh;
	if (colour) {
		/* lend pens 8..15 to the icon while the box is open */
		for (i = 0; i < 8; i++) {
			vq_color(vh, 8 + i, saved_rgb[i]);
			vs_color(vh, 8 + i, icon16_rgb[i]);
		}
		draw_icon16(dlg_x + (dlg_w - ICON_W * sx) / 2, y, sx, sy);
	} else {
		draw_icon(dlg_x + (dlg_w - ICON_W * sx) / 2, y, sx, sy);
	}
	y += ih + lh / 2;
	center_text(y, "Claude ST", 1 | 8);		/* bold, underlined */
	y += lh;
	center_text(y, "Version " VERSION, 0);
	y += lh + lh / 2;
	center_text(y, "Claude.ai for the Atari ST, TT and Falcon", 0);
	y += lh + lh / 2;
	line(dlg_x + 4 * cw, y - lh / 4, dlg_x + dlg_w - 4 * cw, y - lh / 4, 1);
	y += lh / 4;
	center_text(y, "Created by Erez Yaary", 1);
	y += lh;
	center_text(y, "\xBD 2026 Erez Yaary", 0);
	y += lh + lh / 2;
	center_text(y, conn, 0);
	y += lh + lh / 2;
	/* the fine print, in the 8x8 system font where the screen has room */
	{
		short scw, sch, big = ch >= 16;
		if (big)
			vst_height(vh, 6, &scw, &sch);
		short step = big ? sch + 3 : lh;
		center_text(y, "Unofficial client, not affiliated with Anthropic or Atari.", 0);
		y += step;
		center_text(y, "Claude is a trademark of Anthropic PBC.", 0);
		y += step;
		center_text(y, "Atari is a trademark of Atari Interactive.", 0);
		if (big)
			vst_height(vh, 13, &scw, &sch);
	}
	dlg_button(ok_x, "OK", 1);
	graf_mouse(M_ON, 0);
	wait_release();

	/* Return, Esc, Undo, Help, Space or a click closes it */
	while (!done) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			u8 sc = e.kreturn >> 8, as = e.kreturn & 0xff;
			if (as == 0x0d || as == 0x1b || as == ' ' || sc == 0x61 || sc == 0x62)
				done = 1;
		}
		if (e.which & MU_BUTTON) {
			wait_release();
			done = 1;
		}
	}
	vs_clip(vh, 0, clipr);
	if (colour)
		for (i = 0; i < 8; i++)
			vs_color(vh, 8 + i, saved_rgb[i]);
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
		r = popup(x, y1, y0, art_menu, 2);
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
		r = popup(x, y1, y0, code_menu, 4);
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
		r = popup(x, y1, y0, proj_menu, 6);
		switch (r) {
		case 0: open_item(i); break;
		case 1: tx_cmd4("PIN", "PROJECT", it->id, it->pinned ? "0" : "1"); break;
		case 2: rename_item("PROJECT", it); break;
		case 3: tx_cmd("ARCHIVE", "PROJECT", it->id); break;
		case 5:
			if (form_alert(1, "[3][Delete this project?|This can't be undone.][Cancel|Delete]") == 2)
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
	r = popup(x, y1, y0, chat_menu, 6);
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
		if (form_alert(1, "[3][Delete this chat?|This can't be undone.][Cancel|Delete]") == 2)
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
		form_alert(1, "[3][Couldn't create that file.|Is the disk write-protected|or full?][ OK ]");
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

static void save_chunk(const char *hex)
{
	static u8 buf[128];
	short n = 0;
	while (hex[0] && hex[1] && n < (short)sizeof(buf)) {
		buf[n++] = (u8)(hexval(hex[0]) << 4 | hexval(hex[1]));
		hex += 2;
	}
	if (Fwrite(save_fd, n, buf) != n) {
		Fclose(save_fd);
		save_fd = -1;
		Fdelete(save_path);
		form_alert(1, "[3][Writing the file failed.|The disk may be full.][ OK ]");
		return;
	}
	save_bytes += n;
}

static void finish_save(void)
{
	static char msg[200];
	char *p;
	if (save_fd < 0)
		return;
	Fclose(save_fd);
	save_fd = -1;
	save_id[0] = 0;
	strcpy(msg, "[1][Saved ");
	p = msg + strlen(msg);
	strlcpy_(p, save_path, 60);
	p += strlen(p);
	strcpy(p, "|");
	p = put_num(p + 1, save_bytes > 65535 ? 65535 : (u16)save_bytes);
	strcpy(p, save_bytes > 65535 ? "+ bytes][ OK ]" : " bytes][ OK ]");
	notice[0] = 0;
	dirty |= D_TITLE;
	flush_dirty();
	form_alert(1, msg);
}

/* The model menu: the models, then the effort levels for this model.
 * The current ones are ticked. Opened from the chip, F9 or Options. */
static void model_menu(void)
{
	static char lab[NMODEL + NEFFORT + 1][24];
	const char *lp[NMODEL + NEFFORT + 1];
	short i, n = 0, r;
	if (!nmodel) {
		form_alert(1, "[1][The gateway hasn't sent|the list of models yet.][ OK ]");
		return;
	}
	for (i = 0; i < nmodel; i++, n++) {
		strcpy(lab[n], strcmp(mod_id[i], cur_model) ? "  " : "\x08 ");
		strlcpy_(lab[n] + 2, mod_lab[i], 20);
	}
	if (neffort) {
		strcpy(lab[n++], "----------------");
		for (i = 0; i < neffort; i++, n++) {
			strcpy(lab[n], strcmp(eff_id[i], cur_effort) ? "  " : "\x08 ");
			strcat(lab[n], "Effort: ");
			strlcpy_(lab[n] + strlen(lab[n]), eff_lab[i], 13);
		}
	}
	for (i = 0; i < n; i++)
		lp[i] = lab[i];
	flush_dirty();
	r = popup(chip_x0 ? chip_x0 : px + pw / 2, wy + title_h + 1, -1, lp, n);
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
		r = popup(pick_x, y0 + row_h, y0, lab, n);
	} else {
		flush_dirty();
		r = popup(pick_x, list_y + row_h, -1, lab, n);
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
	EVENT e;

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
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_CTRL | 0x08)) &&
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
	dirty |= D_SIDEBAR;
}

static void handle_key(short kstate, short kr)
{
	u8 ascii = kr & 0xff;
	u8 scan = (kr >> 8) & 0xff;
	short shift = kstate & (K_LSHIFT | K_RSHIFT);

	switch (scan) {
	case 0x48: scroll_text(shift ? -(rows - 1) : -1); return;	/* up */
	case 0x50: scroll_text(shift ? rows - 1 : 1); return;		/* down */
	case 0x47: scroll_text(shift ? 32000 : -32000); return;		/* Clr/Home */
	case 0x62: about(); return;					/* Help */
	case 0x61: inlen = in_cur = in_start = 0; search_mode = 0; dirty |= D_INPUT | D_SIDEBAR; return; /* Undo */
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
	switch (ascii) {
	case 0x0e: do_new_chat(); return;	/* ^N */
	case 0x06: do_search(); return;		/* ^F */
	case 0x12: reconnect(); return;		/* ^R */
	case 0x11: quit = 1; return;		/* ^Q */
	case 0x0d:				/* Return / Enter */
		if (inlen == 0 && !search_mode && lcur >= 0 && lcur < nitems) {
			short i = lcur;
			lcur = -1;
			dirty |= D_SIDEBAR;
			open_item(i);
		} else {
			submit();
		}
		return;
	case 0x1b:				/* Esc */
		if (lcur >= 0) {
			lcur = -1;
			dirty |= D_SIDEBAR;
		}
		inlen = in_cur = in_start = 0;
		search_mode = 0;
		dirty |= D_INPUT | D_SIDEBAR;
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

static void handle_msg(short *msg)
{
	switch (msg[0]) {
	case MN_SELECTED:
		switch (msg[4]) {
		case MN_ABOUT: about(); break;
		case MN_NEW: do_new_chat(); break;
		case MN_FIND: do_search(); break;
		case MN_REF: reconnect(); break;
		case MN_NET:
			if (tcp_ip)
				use_link(LINK_TCP);
			else
				form_alert(1, "[1][Type /connect and the IP|address of your gateway|in the reply line, e.g.|/connect 192.168.1.10][ OK ]");
			break;
		case MN_SER: use_link(LINK_SERIAL); break;
		case MN_HEB: toggle_hebrew(); break;
		case MN_ACCT: do_account(); break;
		case MN_QUIT: quit = 1; break;
		case MN_B48: set_baud(BAUD_4800); break;
		case MN_B96: set_baud(BAUD_9600); break;
		case MN_B192: set_baud(BAUD_19200); break;
		}
		menu_tnormal(menu, msg[3], 1);
		break;
	case WM_REDRAW:
		if (msg[3] == win) {
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
	case WM_TOPPED:
		wind_set(win, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
	case AP_TERM:
		quit = 1;
		break;
	case WM_FULLED: {
		short cx, cy, cw_, ch_, fx, fy, fw, fh;
		wind_get(win, WF_CURRXYWH, &cx, &cy, &cw_, &ch_);
		wind_get(win, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (cx == fx && cy == fy && cw_ == fw && ch_ == fh)
			wind_get(win, WF_PREVXYWH, &fx, &fy, &fw, &fh);
		set_curr(fx, fy, fw, fh);
		break;
	}
	case WM_SIZED:
	case WM_MOVED:
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

int main(void)
{
	short work_out[57], dx, dy, dw, dh, wbox, hbox;
	short msg[8];
	EVENT ev;

	apid = appl_init();
	if (apid < 0)
		return 1;
	phys = graf_handle(&cw, &ch, &wbox, &hbox);
	vh = v_opnvwk_(phys, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	ncolors = work_out[13];

	if (!alloc_buffers()) {
		form_alert(1, "[3][Claude ST: not enough memory.][ Quit ]");
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}

	menu_init();
	menu_check_baud();
	menu_bar(menu, 1);
	graf_mouse(ARROW, 0);

	wind_get(0, WF_WORKXYWH, &dx, &dy, &dw, &dh);
	win = wind_create(WKIND, dx, dy, dw, dh);
	if (win < 0) {
		form_alert(1, "[3][Claude ST: no window available.][ Quit ]");
		menu_bar(menu, 0);
		v_clsvwk(vh);
		appl_exit();
		return 1;
	}
	set_window_name();
	wind_open(win, dx, dy, dw, dh);
	wind_get(win, WF_WORKXYWH, &wx, &wy, &ww, &wh);
	load_config();
	layout();
	set_divider_watch();

	begin_message('I');
	{
		static const char hi[] = "Welcome to Claude ST. Connecting to your Claude "
			"gateway (claude_bridge.py) over the network or the serial port...";
		append(hi, sizeof(hi) - 1);
	}

	menu_check_baud();
	if (link == LINK_SERIAL) {
		serial_open();
		send_hello();
	}
	dirty = D_ALL;

	while (!quit) {
		/* 0x101/3/0: wake on any button press, left or right */
		evnt_multi_(MU_KEYBD | MU_BUTTON | MU_MESAG | MU_TIMER | MU_M1,
			    0x101, 3, 0, 40, msg, &ev);
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
				EVENT e2;
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
		if (!online && ticks - last_hello > 125 && (link == LINK_SERIAL || tcp_cn >= 0))
			send_hello();
		flush_dirty();
	}

	tx_cmd("BYE", 0, 0);
	if (tcp_cn >= 0)
		sting_close(tcp_cn);
	serial_close();
	wind_close(win);
	wind_delete(win);
	menu_bar(menu, 0);
	v_clsvwk(vh);
	Mfree(tb);
	appl_exit();
	return 0;
}
