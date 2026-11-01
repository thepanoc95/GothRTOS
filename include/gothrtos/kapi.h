#ifndef GOTHRTOS_KAPI_H
#define GOTHRTOS_KAPI_H

#include "gothrtos/kernel.h"

typedef struct {
    const char *name;
    goth_task_t *task;
    void *stack;
    size_t stack_size;
    uint8_t priority;
    goth_task_entry_t entry;
    void *argument;
} goth_kapi_program_t;

goth_status_t goth_kapi_yield(goth_task_id_t *next_task);
goth_status_t goth_kapi_sleep(uint32_t ticks, goth_task_id_t *next_task);
goth_status_t goth_kapi_exit(goth_task_id_t *next_task);
goth_status_t goth_kapi_get_ticks(uint64_t *ticks);
goth_status_t goth_kapi_endpoint_send(goth_endpoint_t *endpoint,
                                       uint32_t type,
                                       uintptr_t value0,
                                       uintptr_t value1);
goth_status_t goth_kapi_endpoint_receive(goth_endpoint_t *endpoint,
                                          goth_message_t *message);

goth_status_t goth_kapi_start_programs(const goth_kapi_program_t *programs,
                                       size_t program_count,
                                       size_t *started_count);

#endif
