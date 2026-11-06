#ifndef GOTHRTOS_UM_INTERNAL_H
#define GOTHRTOS_UM_INTERNAL_H

#include "gothrtos/kernel.h"

/* LLVM-style target triple of the compiler that built this port, e.g.
 * "x86_64-linux-gnu" or "x86_64-w64-mingw32". GNUmakefile asks the compiler
 * for its own answer (clang's -print-target-triple, falling back to gcc's
 * -dumpmachine) and passes it here, so the user-mode platform identifier
 * always names the toolchain that actually produced the binary. */
#ifndef GOTH_UM_TARGET_TRIPLE
#define GOTH_UM_TARGET_TRIPLE "unknown"
#endif

#define GOTH_UM_GUARD_WORD UINT32_C(0x474F5448)
#define GOTH_UM_GUARD_XOR UINT32_C(0xA5A5A5A5)

goth_status_t goth_um_platform_init(void);
goth_status_t goth_um_platform_reset(goth_task_t *tasks);
goth_status_t goth_um_task_create(goth_task_t *task);
goth_status_t goth_um_task_destroy(goth_task_t *task);
goth_status_t goth_um_task_guard(goth_task_t *task);
bool goth_um_task_guard_valid(goth_task_t *task);
void goth_um_context_switch(goth_task_t *previous, goth_task_t *next);
bool goth_um_is_running(void);
void goth_um_start_clock(uint32_t tick_period_ms);
uint32_t goth_um_collect_ticks(void);

#endif
