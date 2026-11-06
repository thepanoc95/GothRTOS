#ifndef GOTHRTOS_AMD64_INTERNAL_H
#define GOTHRTOS_AMD64_INTERNAL_H

#include "gothrtos/amd64.h"
#include "gothrtos/kernel.h"

#define GOTH_UM_GUARD_WORD UINT32_C(0x474F5448)
#define GOTH_UM_GUARD_XOR UINT32_C(0xA5A5A5A5)

/* Port hooks consumed by rtos/kernel.c and rtos/sched.c. The "um" prefix is
   historical: the same cooperative scheduling hooks are implemented by the
   user-mode and bare-metal ports. */
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

/* Implemented by amd64_start.S and used by the port. */
void goth_amd64_context_switch(uint64_t **save_sp, uint64_t *next_sp);
void goth_amd64_task_trampoline(void);
void goth_amd64_task_start(goth_task_t *task);

#endif
