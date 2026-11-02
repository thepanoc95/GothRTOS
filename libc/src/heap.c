#include <gothrtos/libc.h>

/*
 * Default arena for hosted GothLibC programs.  It lives in BSS, so it costs
 * nothing in the executable image, and goth_libc_heap_init_auto() makes it the
 * allocator used by a normal process entry point.
 */
#define GOTH_LIBC_DEFAULT_HEAP_SIZE (32U * 1024U * 1024U)

static unsigned char default_heap[GOTH_LIBC_DEFAULT_HEAP_SIZE];

int goth_libc_heap_init_auto(void)
{
    return goth_libc_heap_init(default_heap, sizeof(default_heap));
}
