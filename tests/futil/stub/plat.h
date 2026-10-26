/* the plat.h calls futil.c makes, over the build machine's files, for the
   host test; TOS's rename (never over a file) when tos_rename is set */
#define PF_READ  0
#define PF_WRITE 1
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
long pf_size(const char *path);
int  pf_remove(const char *path);
int  pf_rename(const char *from, const char *to);
char *pf_load(const char *path, long *len);
extern long (*pf_size_open)(int h);
int  pf_save(const char *path, const void *data, long len);
int  pf_wopen(const char *path);
long pf_wwrite(int h, const void *data, long len);
int  pf_wclose(int h, const char *path, int keep);
