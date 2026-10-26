/* the AES calls twin.c makes, for the host test */
#define MU_MESAG 0x10
#define MU_TIMER 0x20
#define APP_FIRST 0
#define APP_NEXT 1
#define APP_APPLICATION 2
#define APP_ACCESSORY 4
typedef struct { short mx, my, mbutton, kstate, kreturn, breturn; } AEVENT;
short appl_id(void);
short appl_find(const char *name);
short appl_write(short id, short len, const void *buf);
short appl_search(short mode, char *name, short *type, short *id);
short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4);
short aes_version(void);
short evnt_timer_(unsigned long ms);
short evnt_multi_(short flags, short clicks, short mask, short state, unsigned long timer, short *msg, AEVENT *ev);
