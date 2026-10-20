/* the host's, and libc.c's heap counters, for the host test */
#include_next <stdlib.h>
void heap_add(void *p, size_t bytes);
extern long heap_use, heap_peak, heap_early;
