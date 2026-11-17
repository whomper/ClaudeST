/* the AES calls av.c makes, for the host test */
short appl_id(void);
short appl_find(const char *name);
short appl_write(short id, short len, const void *buf);
short aes_version(void);
short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4);
short appl_search(short mode, char *name, short *type, short *id);
