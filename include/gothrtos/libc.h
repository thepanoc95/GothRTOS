#ifndef GOTHRTOS_LIBC_H
#define GOTHRTOS_LIBC_H

#include <stddef.h>

int goth_libc_heap_init(void *memory, size_t size);
int goth_libc_heap_init_auto(void);
void goth_libc_heap_reset(void);
size_t goth_libc_heap_available(void);

/* Populated by the GothLibC process entry point. */
extern char **goth_libc_environ;

int goth_libc_start(int argc, char **argv, char **environment);

#endif
