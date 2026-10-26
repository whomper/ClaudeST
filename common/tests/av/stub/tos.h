/* what av.c takes from TOS, for the host test */
extern void *_BasePage;
long get_cookie(long id);
unsigned long tos_ms(void);
void *Mxalloc(long size, short mode);
const char *cmd_line(void);
