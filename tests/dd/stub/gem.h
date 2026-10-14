/* the AES calls dd.c and clip.c make, for the host test */
short appl_id(void);
short aes_version(void);
short appl_write(short id, short len, const void *buf);
short appl_getinfo(short type, short *o1, short *o2, short *o3, short *o4);
short scrp_read(char *path);
short scrp_write(const char *path);
short shel_write(short mode, short wisgr, short wiscr, const void *cmd, const void *tail);
