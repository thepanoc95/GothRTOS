#include "gothrtos/syscall.h"

#include <limits.h>

static goth_syscall_result_t syscall_result(goth_status_t status,
                                            uint64_t value)
{
    goth_syscall_result_t result;

    result.status = status;
    result.value = value;
    return result;
}

static goth_syscall_result_t syscall_task_result(goth_task_t *task)
{
    return syscall_result(GOTH_STATUS_OK, task != NULL ? task->id : 0U);
}

goth_syscall_result_t goth_syscall_dispatch(
    const goth_syscall_request_t *request)
{
    goth_endpoint_t *endpoint;
    goth_message_t *message;
    goth_status_t status;

    if (request == NULL) {
        return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
    }
    if (!goth_kernel_is_initialized()) {
        return syscall_result(GOTH_STATUS_NOT_INITIALIZED, 0U);
    }

    switch (request->number) {
    case GOTH_SYSCALL_GET_ABI_VERSION:
        return syscall_result(GOTH_STATUS_OK, GOTH_SYSCALL_ABI_VERSION);

    case GOTH_SYSCALL_TASK_YIELD:
        if (goth_kernel_current_task() == NULL) {
            return syscall_result(GOTH_STATUS_BAD_STATE, 0U);
        }
        return syscall_task_result(goth_task_yield());

    case GOTH_SYSCALL_TASK_SLEEP:
        if (request->arguments[0] == 0U ||
            request->arguments[0] > UINT32_MAX) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        if (goth_kernel_current_task() == NULL) {
            return syscall_result(GOTH_STATUS_BAD_STATE, 0U);
        }
        return syscall_task_result(
            goth_task_sleep((uint32_t)request->arguments[0]));

    case GOTH_SYSCALL_TASK_EXIT:
        if (goth_kernel_current_task() == NULL) {
            return syscall_result(GOTH_STATUS_BAD_STATE, 0U);
        }
        return syscall_task_result(goth_task_exit());

    case GOTH_SYSCALL_KERNEL_TICKS:
        return syscall_result(GOTH_STATUS_OK, goth_kernel_ticks());

    case GOTH_SYSCALL_ENDPOINT_SEND:
        if (request->arguments[0] == 0U ||
            request->arguments[1] > UINT32_MAX) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        endpoint = (goth_endpoint_t *)request->arguments[0];
        status = goth_endpoint_send(endpoint,
                                    (uint32_t)request->arguments[1],
                                    request->arguments[2],
                                    request->arguments[3]);
        return syscall_result(status, 0U);

    case GOTH_SYSCALL_ENDPOINT_RECEIVE:
        if (request->arguments[0] == 0U ||
            request->arguments[1] == 0U) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        endpoint = (goth_endpoint_t *)request->arguments[0];
        message = (goth_message_t *)request->arguments[1];
        status = goth_endpoint_receive(endpoint, message);
        return syscall_result(status, 0U);

    default:
        return syscall_result(GOTH_STATUS_INVALID_SYSCALL, 0U);
    }
}
