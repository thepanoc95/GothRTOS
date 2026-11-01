#include "gothrtos/kapi.h"
#include "gothrtos/syscall.h"

#include <limits.h>

static goth_status_t dispatch_task_call(uint32_t number,
                                        uintptr_t argument,
                                        goth_task_id_t *next_task)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (next_task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = number;
    request.arguments[0] = argument;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > UINT32_MAX) {
            return GOTH_STATUS_BAD_STATE;
        }
        *next_task = (goth_task_id_t)result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_yield(goth_task_id_t *next_task)
{
    return dispatch_task_call(GOTH_SYSCALL_TASK_YIELD, 0U, next_task);
}

goth_status_t goth_kapi_sleep(uint32_t ticks, goth_task_id_t *next_task)
{
    if (ticks == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return dispatch_task_call(GOTH_SYSCALL_TASK_SLEEP, ticks, next_task);
}

goth_status_t goth_kapi_exit(goth_task_id_t *next_task)
{
    return dispatch_task_call(GOTH_SYSCALL_TASK_EXIT, 0U, next_task);
}

goth_status_t goth_kapi_get_ticks(uint64_t *ticks)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (ticks == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_KERNEL_TICKS;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        *ticks = result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_endpoint_send(goth_endpoint_t *endpoint,
                                       uint32_t type,
                                       uintptr_t value0,
                                       uintptr_t value1)
{
    goth_syscall_request_t request = {0};

    if (endpoint == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_ENDPOINT_SEND;
    request.arguments[0] = (uintptr_t)endpoint;
    request.arguments[1] = type;
    request.arguments[2] = value0;
    request.arguments[3] = value1;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_endpoint_receive(goth_endpoint_t *endpoint,
                                          goth_message_t *message)
{
    goth_syscall_request_t request = {0};

    if (endpoint == NULL || message == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_ENDPOINT_RECEIVE;
    request.arguments[0] = (uintptr_t)endpoint;
    request.arguments[1] = (uintptr_t)message;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_start_programs(const goth_kapi_program_t *programs,
                                       size_t program_count,
                                       size_t *started_count)
{
    size_t index;

    if (started_count == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    *started_count = 0U;
    if (programs == NULL && program_count != 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!goth_kernel_is_initialized()) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }

    for (index = 0U; index < program_count; index++) {
        const goth_kapi_program_t *program = &programs[index];

        if (program->name == NULL || program->task == NULL ||
            program->stack == NULL || program->stack_size == 0U ||
            program->entry == NULL) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
    }

    for (index = 0U; index < program_count; index++) {
        const goth_kapi_program_t *program = &programs[index];
        goth_status_t status;

        status = goth_task_create(program->task,
                                  program->stack,
                                  program->stack_size,
                                  program->priority,
                                  program->entry,
                                  program->argument);
        if (status != GOTH_STATUS_OK) {
            return status;
        }
        (*started_count)++;
    }
    return GOTH_STATUS_OK;
}
