/* the GEMDOS calls accmem.c makes, for the host test */
void *Malloc(long n);
void *Mxalloc(long n, int mode);
void *Fgetdta(void);
long Fsetdta(void *p);
long Fsfirst(const char *path, int attr);
long Fsnext(void);
short boot_drive(void);
