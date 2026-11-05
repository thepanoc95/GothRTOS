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

goth_status_t goth_kapi_self(goth_task_id_t *self)
{
    return dispatch_task_call(GOTH_SYSCALL_TASK_SELF, 0U, self);
}

goth_status_t goth_kapi_set_preemptive(bool enabled)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    request.number = GOTH_SYSCALL_KERNEL_SET_SCHED;
    request.arguments[0] = enabled ? GOTH_SCHED_PREEMPTIVE
                                   : GOTH_SCHED_COOPERATIVE;
    result = goth_syscall_dispatch(&request);
    return result.status;
}

goth_status_t goth_kapi_is_preemptive(bool *enabled)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (enabled == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_KERNEL_GET_SCHED;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        *enabled = result.value == GOTH_SCHED_PREEMPTIVE;
    }
    return result.status;
}

goth_status_t goth_kapi_set_quantum(uint32_t ticks)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (ticks == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_KERNEL_SET_QUANTUM;
    request.arguments[0] = ticks;
    result = goth_syscall_dispatch(&request);
    return result.status;
}

goth_status_t goth_kapi_get_quantum(uint32_t *ticks)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (ticks == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_KERNEL_GET_QUANTUM;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        *ticks = (uint32_t)result.value;
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

goth_status_t goth_kapi_endpoint_poll(goth_endpoint_t *endpoint,
                                       goth_message_t *message)
{
    goth_syscall_request_t request = {0};

    if (endpoint == NULL || message == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    request.number = GOTH_SYSCALL_ENDPOINT_POLL;
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

goth_status_t goth_kapi_vfs_open(const char *path,
                                 uint32_t flags,
                                 uint32_t *fd)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (path == NULL || fd == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_OPEN;
    request.arguments[0] = (uintptr_t)path;
    request.arguments[1] = flags;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > UINT32_MAX) {
            return GOTH_STATUS_BAD_STATE;
        }
        *fd = (uint32_t)result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_vfs_close(uint32_t fd)
{
    goth_syscall_request_t request = {0};

    request.number = GOTH_SYSCALL_VFS_CLOSE;
    request.arguments[0] = fd;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_vfs_read(uint32_t fd,
                                 void *buffer,
                                 size_t capacity,
                                 size_t *read_length)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (read_length == NULL || (buffer == NULL && capacity != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_READ;
    request.arguments[0] = fd;
    request.arguments[1] = (uintptr_t)buffer;
    request.arguments[2] = capacity;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > (uint64_t)(size_t)-1) {
            return GOTH_STATUS_BAD_STATE;
        }
        *read_length = (size_t)result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_vfs_write(uint32_t fd,
                                  const void *data,
                                  size_t length,
                                  size_t *written)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    if (written == NULL || (data == NULL && length != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_WRITE;
    request.arguments[0] = fd;
    request.arguments[1] = (uintptr_t)data;
    request.arguments[2] = length;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > (uint64_t)(size_t)-1) {
            return GOTH_STATUS_BAD_STATE;
        }
        *written = (size_t)result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_vfs_seek(uint32_t fd,
                                 int64_t offset,
                                 goth_vfs_whence_t whence,
                                 uint64_t *position)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    request.number = GOTH_SYSCALL_VFS_SEEK;
    request.arguments[0] = fd;
    request.arguments[1] = (uintptr_t)offset;
    request.arguments[2] = (uintptr_t)whence;
    result = goth_syscall_dispatch(&request);
    if (result.status == GOTH_STATUS_OK && position != NULL) {
        *position = result.value;
    }
    return result.status;
}

goth_status_t goth_kapi_vfs_stat(const char *path, goth_vfs_stat_t *stat)
{
    goth_syscall_request_t request = {0};

    if (path == NULL || stat == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_STAT;
    request.arguments[0] = (uintptr_t)path;
    request.arguments[1] = (uintptr_t)stat;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_vfs_mkdir(const char *path)
{
    goth_syscall_request_t request = {0};

    if (path == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_MKDIR;
    request.arguments[0] = (uintptr_t)path;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_vfs_unlink(const char *path)
{
    goth_syscall_request_t request = {0};

    if (path == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_UNLINK;
    request.arguments[0] = (uintptr_t)path;
    return goth_syscall_dispatch(&request).status;
}

goth_status_t goth_kapi_vfs_readdir(const char *path,
                                    uint32_t index,
                                    goth_vfs_dirent_t *entry)
{
    goth_syscall_request_t request = {0};

    if (path == NULL || entry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    request.number = GOTH_SYSCALL_VFS_READDIR;
    request.arguments[0] = (uintptr_t)path;
    request.arguments[1] = index;
    request.arguments[2] = (uintptr_t)entry;
    return goth_syscall_dispatch(&request).status;
}
