/* the plat.h calls secret.c makes, over the build machine's files, for
   the host test */
#define PF_READ  0
#define PF_WRITE 1
int  pf_open(const char *path, int mode);
long pf_read(int h, void *buf, long n);
long pf_write(int h, const void *buf, long n);
void pf_close(int h);
void pf_entropy(void *buf, int n);
