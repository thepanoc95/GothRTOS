#ifndef GOTHRTOS_ARM32_INTERNAL_H
#define GOTHRTOS_ARM32_INTERNAL_H

#include "gothrtos/arm32.h"
#include "gothrtos/kernel.h"

/* Port hooks consumed by rtos/kernel.c. The "um" prefix is historical: the
   same cooperative scheduling hooks are implemented by the user-mode and
   bare-metal ports. */
goth_status_t goth_um_platform_init(void);
goth_status_t goth_um_platform_reset(goth_task_t *tasks);
goth_status_t goth_um_task_create(goth_task_t *task);
goth_status_t goth_um_task_destroy(goth_task_t *task);
void goth_um_context_switch(goth_task_t *previous, goth_task_t *next);
bool goth_um_is_running(void);
void goth_um_start_clock(uint32_t tick_period_ms);
uint32_t goth_um_collect_ticks(void);

/* Implemented by arm32_start.S and used by the port. */
void goth_arm_context_switch(uint32_t **save_sp, uint32_t *next_sp);
void goth_arm_task_trampoline(void);
void goth_arm_task_start(goth_task_t *task);

#endif
