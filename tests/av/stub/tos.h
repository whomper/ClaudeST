/* what av.c takes from TOS, for the host test */
extern void *_BasePage;
long get_cookie(long id);
void *Mxalloc(long size, short mode);
