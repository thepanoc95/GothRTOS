#ifndef GOTHRTOS_AMD64_H
#define GOTHRTOS_AMD64_H

#include <stdint.h>

/* Helpers provided by the AMD64 bare-metal port. */

void goth_amd64_exit(int status) __attribute__((noreturn));
void goth_amd64_cpu_relax(void);
uint64_t goth_amd64_read_tsc(void);

#endif
