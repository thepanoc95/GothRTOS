#ifndef GOTHRTOS_SYSCALL_H
#define GOTHRTOS_SYSCALL_H

#include "gothrtos/kernel.h"

typedef enum {
    GOTH_SYSCALL_TASK_YIELD = 1U,
    GOTH_SYSCALL_TASK_SLEEP,
    GOTH_SYSCALL_TASK_EXIT,
    GOTH_SYSCALL_KERNEL_TICKS,
    GOTH_SYSCALL_ENDPOINT_SEND,
    GOTH_SYSCALL_ENDPOINT_RECEIVE
} goth_syscall_number_t;

typedef struct {
    uint32_t number;
    uintptr_t arguments[4];
} goth_syscall_request_t;

typedef struct {
    goth_status_t status;
    uint64_t value;
} goth_syscall_result_t;

goth_syscall_result_t goth_syscall_dispatch(
    const goth_syscall_request_t *request);

#endif
