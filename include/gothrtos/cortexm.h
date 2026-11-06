#ifndef GOTHRTOS_CORTEXM_H
#define GOTHRTOS_CORTEXM_H

#include <stdint.h>

/* Helpers provided by the Cortex-M (ARMv6-M / ARMv8-M) bare-metal port. */

void goth_cm_exit(int status) __attribute__((noreturn));
void goth_cm_cpu_relax(void);

#endif
