#include "gothrtos/syscall.h"

#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/vfs.h"

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

static bool syscall_fits_u32(uintptr_t value)
{
#if UINTPTR_MAX > UINT32_MAX
    return value <= UINT32_MAX;
#else
    (void)value;
    return true;
#endif
}

static goth_syscall_result_t syscall_fault(const char *reason)
{
    goth_mem_note_violation(reason);
    return syscall_result(GOTH_STATUS_FAULT, 0U);
}

static bool syscall_user_readable(uintptr_t address, size_t length)
{
    return address != 0U &&
           goth_mem_check_user((const void *)address, length, GOTH_MEM_READ);
}

static bool syscall_user_writable(uintptr_t address, size_t length)
{
    return address != 0U &&
           goth_mem_check_user((const void *)address, length, GOTH_MEM_WRITE);
}

static goth_status_t syscall_copy_path(uintptr_t address, char *buffer,
                                       size_t capacity)
{
    size_t index;

    if (address == 0U || buffer == NULL || capacity == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < capacity; index++) {
        char character;

        if (!syscall_user_readable(address + index, 1U)) {
            return GOTH_STATUS_FAULT;
        }
        character = ((const char *)address)[index];
        buffer[index] = character;
        if (character == '\0') {
            return GOTH_STATUS_OK;
        }
    }
    return GOTH_STATUS_INVALID_ARGUMENT;
}

static goth_syscall_result_t syscall_endpoint_receive(
    const goth_syscall_request_t *request,
    bool poll_only)
{
    goth_endpoint_t *endpoint;
    goth_message_t *message;
    goth_status_t status;

    if (request->arguments[0] == 0U || request->arguments[1] == 0U) {
        return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
    }

    endpoint = (goth_endpoint_t *)request->arguments[0];
    message = (goth_message_t *)request->arguments[1];
    status = poll_only ? goth_endpoint_poll(endpoint, message)
                       : goth_endpoint_receive(endpoint, message);
    return syscall_result(status, 0U);
}

goth_syscall_result_t goth_syscall_dispatch(
    const goth_syscall_request_t *request)
{
    goth_endpoint_t *endpoint;
    goth_status_t status;
    char path[GOTH_VFS_MAX_PATH];

    if (request == NULL) {
        return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
    }
    if (!goth_kernel_is_initialized()) {
        return syscall_result(GOTH_STATUS_NOT_INITIALIZED, 0U);
    }

    switch (request->number) {
    case GOTH_SYSCALL_TASK_YIELD:
        if (goth_kernel_current_task() == NULL) {
            return syscall_result(GOTH_STATUS_BAD_STATE, 0U);
        }
        return syscall_task_result(goth_task_yield());

    case GOTH_SYSCALL_TASK_SLEEP:
        if (request->arguments[0] == 0U ||
            !syscall_fits_u32(request->arguments[0])) {
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

    case GOTH_SYSCALL_TASK_SELF:
        {
            goth_task_t *current = goth_kernel_current_task();

            return syscall_result(GOTH_STATUS_OK,
                                  current != NULL ? current->id : 0U);
        }

    case GOTH_SYSCALL_KERNEL_TICKS:
        return syscall_result(GOTH_STATUS_OK, goth_kernel_ticks());

    case GOTH_SYSCALL_KERNEL_TASK_COUNT:
        return syscall_result(GOTH_STATUS_OK, goth_kernel_task_count());

    case GOTH_SYSCALL_KERNEL_GET_SCHED:
        return syscall_result(GOTH_STATUS_OK,
                              (uint64_t)goth_kernel_get_scheduler());

    case GOTH_SYSCALL_KERNEL_SET_SCHED:
        status = goth_kernel_set_scheduler(
            (goth_sched_policy_t)request->arguments[0]);
        return syscall_result(status, (uint64_t)goth_kernel_get_scheduler());

    case GOTH_SYSCALL_KERNEL_GET_QUANTUM:
        return syscall_result(GOTH_STATUS_OK, goth_kernel_quantum());

    case GOTH_SYSCALL_KERNEL_SET_QUANTUM:
        if (request->arguments[0] == 0U ||
            !syscall_fits_u32(request->arguments[0])) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        status = goth_kernel_set_quantum((uint32_t)request->arguments[0]);
        return syscall_result(status, goth_kernel_quantum());

    case GOTH_SYSCALL_ENDPOINT_SEND:
        if (request->arguments[0] == 0U ||
            !syscall_fits_u32(request->arguments[1])) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        endpoint = (goth_endpoint_t *)request->arguments[0];
        status = goth_endpoint_send(endpoint,
                                    (uint32_t)request->arguments[1],
                                    request->arguments[2],
                                    request->arguments[3]);
        return syscall_result(status, 0U);

    case GOTH_SYSCALL_ENDPOINT_RECEIVE:
        return syscall_endpoint_receive(request, false);

    case GOTH_SYSCALL_ENDPOINT_POLL:
        return syscall_endpoint_receive(request, true);

    case GOTH_SYSCALL_KLOG_COUNT:
        return syscall_result(GOTH_STATUS_OK, goth_klog_count());

    case GOTH_SYSCALL_KLOG_LINE:
        {
            size_t capacity = (size_t)request->arguments[2];
            size_t length;

            if (capacity == 0U ||
                !syscall_user_writable(request->arguments[1], capacity)) {
                return syscall_fault("denied klog destination buffer");
            }
            length = goth_klog_line((size_t)request->arguments[0],
                                    (char *)request->arguments[1], capacity);
            return syscall_result(GOTH_STATUS_OK, length);
        }

    case GOTH_SYSCALL_MEM_REGIONS:
        return syscall_result(GOTH_STATUS_OK, goth_mem_region_count());

    case GOTH_SYSCALL_MEM_VIOLATIONS:
        return syscall_result(GOTH_STATUS_OK, goth_mem_violations());

    case GOTH_SYSCALL_MEM_CHECK:
        return syscall_result(
            GOTH_STATUS_OK,
            goth_mem_check_user((const void *)request->arguments[0],
                                (size_t)request->arguments[1],
                                (uint32_t)request->arguments[2])
                ? 1U
                : 0U);

    case GOTH_SYSCALL_VFS_OPEN:
        {
            uint32_t fd = 0U;

            status = syscall_copy_path(request->arguments[0], path,
                                       sizeof(path));
            if (status == GOTH_STATUS_FAULT) {
                return syscall_fault("denied VFS path buffer");
            }
            if (status != GOTH_STATUS_OK) {
                return syscall_result(status, 0U);
            }
            status = goth_vfs_open(path, (uint32_t)request->arguments[1], &fd);
            return syscall_result(status, status == GOTH_STATUS_OK ? fd : 0U);
        }

    case GOTH_SYSCALL_VFS_CLOSE:
        if (!syscall_fits_u32(request->arguments[0])) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        return syscall_result(
            goth_vfs_close((uint32_t)request->arguments[0]), 0U);

    case GOTH_SYSCALL_VFS_READ:
        {
            size_t capacity = (size_t)request->arguments[2];
            size_t read_length = 0U;

            if (!syscall_fits_u32(request->arguments[0])) {
                return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
            }
            if (capacity != 0U &&
                !syscall_user_writable(request->arguments[1], capacity)) {
                return syscall_fault("denied VFS read buffer");
            }
            status = goth_vfs_read((uint32_t)request->arguments[0],
                                   (void *)request->arguments[1], capacity,
                                   &read_length);
            return syscall_result(status, read_length);
        }

    case GOTH_SYSCALL_VFS_WRITE:
        {
            size_t length = (size_t)request->arguments[2];
            size_t written = 0U;

            if (!syscall_fits_u32(request->arguments[0])) {
                return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
            }
            if (length != 0U &&
                !syscall_user_readable(request->arguments[1], length)) {
                return syscall_fault("denied VFS write buffer");
            }
            status = goth_vfs_write((uint32_t)request->arguments[0],
                                    (const void *)request->arguments[1],
                                    length, &written);
            return syscall_result(status, written);
        }

    case GOTH_SYSCALL_VFS_SEEK:
        {
            uint64_t position = 0U;

            if (!syscall_fits_u32(request->arguments[0])) {
                return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
            }
            status = goth_vfs_seek(
                (uint32_t)request->arguments[0],
                (int64_t)(intptr_t)request->arguments[1],
                (goth_vfs_whence_t)request->arguments[2], &position);
            return syscall_result(status, position);
        }

    case GOTH_SYSCALL_VFS_TRUNCATE:
        if (!syscall_fits_u32(request->arguments[0])) {
            return syscall_result(GOTH_STATUS_INVALID_ARGUMENT, 0U);
        }
        return syscall_result(
            goth_vfs_truncate((uint32_t)request->arguments[0],
                              (uint64_t)request->arguments[1]),
            0U);

    case GOTH_SYSCALL_VFS_STAT:
        {
            goth_vfs_stat_t *stat =
                (goth_vfs_stat_t *)request->arguments[1];

            if (!syscall_user_writable(request->arguments[1],
                                       sizeof(*stat))) {
                return syscall_fault("denied VFS stat buffer");
            }
            status = syscall_copy_path(request->arguments[0], path,
                                       sizeof(path));
            if (status == GOTH_STATUS_FAULT) {
                return syscall_fault("denied VFS path buffer");
            }
            if (status != GOTH_STATUS_OK) {
                return syscall_result(status, 0U);
            }
            return syscall_result(goth_vfs_stat(path, stat), 0U);
        }

    case GOTH_SYSCALL_VFS_MKDIR:
    case GOTH_SYSCALL_VFS_UNLINK:
        status = syscall_copy_path(request->arguments[0], path, sizeof(path));
        if (status == GOTH_STATUS_FAULT) {
            return syscall_fault("denied VFS path buffer");
        }
        if (status != GOTH_STATUS_OK) {
            return syscall_result(status, 0U);
        }
        return syscall_result(request->number == GOTH_SYSCALL_VFS_MKDIR
                                  ? goth_vfs_mkdir(path)
                                  : goth_vfs_unlink(path),
                              0U);

    case GOTH_SYSCALL_VFS_READDIR:
        {
            goth_vfs_dirent_t *entry =
                (goth_vfs_dirent_t *)request->arguments[2];

            if (!syscall_user_writable(request->arguments[2],
                                       sizeof(*entry))) {
                return syscall_fault("denied VFS dirent buffer");
            }
            status = syscall_copy_path(request->arguments[0], path,
                                       sizeof(path));
            if (status == GOTH_STATUS_FAULT) {
                return syscall_fault("denied VFS path buffer");
            }
            if (status != GOTH_STATUS_OK) {
                return syscall_result(status, 0U);
            }
            return syscall_result(
                goth_vfs_readdir(path, (uint32_t)request->arguments[1],
                                 entry),
                0U);
        }

    case GOTH_SYSCALL_VFS_USAGE:
        {
            uint32_t used = 0U;
            uint32_t total = 0U;

            status = goth_vfs_usage(&used, &total);
            return syscall_result(status,
                                  ((uint64_t)used << 32) | (uint64_t)total);
        }

    default:
        return syscall_result(GOTH_STATUS_INVALID_SYSCALL, 0U);
    }
}
