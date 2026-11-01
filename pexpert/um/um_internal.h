#ifndef GOTHRTOS_UM_INTERNAL_H
#define GOTHRTOS_UM_INTERNAL_H

#include "gothrtos/kernel.h"

goth_status_t goth_um_platform_init(void);
goth_status_t goth_um_platform_reset(goth_task_t *tasks);
goth_status_t goth_um_task_create(goth_task_t *task);
goth_status_t goth_um_task_destroy(goth_task_t *task);
void goth_um_context_switch(goth_task_t *previous, goth_task_t *next);
bool goth_um_is_running(void);
void goth_um_start_clock(uint32_t tick_period_ms);
uint32_t goth_um_collect_ticks(void);

#endif
