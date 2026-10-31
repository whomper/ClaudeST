/* what dd.c and clip.c take from TOS, for the host test */
typedef struct {
	char  d_reserved[21];
	unsigned char d_attrib;
	unsigned short d_time, d_date;
	unsigned long d_length;
	char  d_fname[14];
} DTA;
#define SIGPIPE 13
#define SIG_IGN 1L
long Fopen(const char *name, short mode);
long Fcreate(const char *name, short attr);
long Fread(short fd, long n, void *buf);
long Fwrite(short fd, long n, const void *buf);
long Fclose(short fd);
long Fseek(long off, short fd, short mode);
long Fdelete(const char *name);
long Fsfirst(const char *pat, short attr);
long Fsnext(void);
long Fsetdta(DTA *d);
void *Fgetdta(void);
long Dcreate(const char *path);
long Fattrib(const char *name, short wflag, short attr);
short Dgetdrv(void);
long Psignal(short sig, long handler);
long get_cookie(long id);
short boot_drive(void);
