#ifndef GOTHRTOS_SYSCALL_H
#define GOTHRTOS_SYSCALL_H

#include "gothrtos/kernel.h"

#define GOTH_SYSCALL_ABI_VERSION 1U

typedef enum {
    GOTH_SYSCALL_TASK_YIELD = 1U,
    GOTH_SYSCALL_TASK_SLEEP = 2U,
    GOTH_SYSCALL_TASK_EXIT = 3U,
    GOTH_SYSCALL_KERNEL_TICKS = 4U,
    GOTH_SYSCALL_ENDPOINT_SEND = 5U,
    GOTH_SYSCALL_ENDPOINT_RECEIVE = 6U,
    GOTH_SYSCALL_GET_ABI_VERSION = 7U
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
