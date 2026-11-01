#ifndef GOTHRTOS_LIBC_H
#define GOTHRTOS_LIBC_H

#include <stddef.h>

int goth_libc_heap_init(void *memory, size_t size);
void goth_libc_heap_reset(void);
size_t goth_libc_heap_available(void);

#endif
