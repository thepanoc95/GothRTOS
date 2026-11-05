#ifndef GOTHRTOS_KINTERNAL_H
#define GOTHRTOS_KINTERNAL_H

#include "gothrtos/kernel.h"

/* Nanokernel-internal scheduler interface.
 *
 * rtos/kernel.c owns the kernel lifecycle and delegates task management to
 * rtos/sched.c. The split keeps the scheduler free of platform policy and
 * lets the IPC and syscall layers use the same primitives. Nothing declared
 * here is part of the userland ABI. */

void goth_sched_reset(void);
goth_task_t *goth_sched_task_list(void);

#endif
