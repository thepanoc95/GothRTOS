#include "gothrtos/mem.h"

#include "gothrtos/klog.h"
#include "um_internal.h"

static goth_mem_region_t regions[GOTH_MEM_MAX_REGIONS];
static bool mem_initialized;
static uint64_t violations;

static bool range_overlaps(uintptr_t first, size_t first_size,
                           uintptr_t second, size_t second_size)
{
    uintptr_t first_end;
    uintptr_t second_end;

    if (first_size == 0U || second_size == 0U) {
        return false;
    }
    first_end = first + first_size;
    second_end = second + second_size;
    if (first_end < first || second_end < second) {
        return true;
    }
    return first < second_end && second < first_end;
}

void goth_mem_init(void)
{
    size_t index;

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        regions[index].base = 0U;
        regions[index].size = 0U;
        regions[index].flags = 0U;
        regions[index].kind = GOTH_MEM_KERNEL;
        regions[index].owner = 0U;
        regions[index].name = NULL;
    }
    violations = 0U;
    mem_initialized = true;
}

bool goth_mem_is_initialized(void)
{
    return mem_initialized;
}

goth_status_t goth_mem_register(void *base,
                                size_t size,
                                uint32_t flags,
                                goth_mem_kind_t kind,
                                goth_task_id_t owner,
                                const char *name)
{
    size_t index;
    size_t free_index = GOTH_MEM_MAX_REGIONS;

    if (!mem_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (base == NULL || size == 0U ||
        (uintptr_t)base + size < (uintptr_t)base) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if ((flags & GOTH_MEM_READ) == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base == (uintptr_t)base) {
            return GOTH_STATUS_BAD_STATE;
        }
        if (regions[index].base == 0U && free_index == GOTH_MEM_MAX_REGIONS) {
            free_index = index;
        }
    }
    if (free_index == GOTH_MEM_MAX_REGIONS) {
        return GOTH_STATUS_FULL;
    }

    regions[free_index].base = (uintptr_t)base;
    regions[free_index].size = size;
    regions[free_index].flags = flags;
    regions[free_index].kind = kind;
    regions[free_index].owner = owner;
    regions[free_index].name = name != NULL ? name : "region";
    return GOTH_STATUS_OK;
}

goth_status_t goth_mem_unregister(void *base)
{
    size_t index;

    if (!mem_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (base == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base == (uintptr_t)base) {
            regions[index].base = 0U;
            regions[index].size = 0U;
            regions[index].flags = 0U;
            regions[index].kind = GOTH_MEM_KERNEL;
            regions[index].owner = 0U;
            regions[index].name = NULL;
            return GOTH_STATUS_OK;
        }
    }
    return GOTH_STATUS_EMPTY;
}

goth_status_t goth_mem_protect(void *base, uint32_t flags)
{
    size_t index;

    if (!mem_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (base == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if ((flags & GOTH_MEM_READ) == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base == (uintptr_t)base) {
            regions[index].flags = flags;
            return GOTH_STATUS_OK;
        }
    }
    return GOTH_STATUS_EMPTY;
}

size_t goth_mem_region_count(void)
{
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base != 0U) {
            count++;
        }
    }
    return count;
}

size_t goth_mem_region_capacity(void)
{
    return GOTH_MEM_MAX_REGIONS;
}

const goth_mem_region_t *goth_mem_region_at(size_t index)
{
    size_t seen = 0U;
    size_t slot;

    for (slot = 0U; slot < GOTH_MEM_MAX_REGIONS; slot++) {
        if (regions[slot].base == 0U) {
            continue;
        }
        if (seen == index) {
            return &regions[slot];
        }
        seen++;
    }
    return NULL;
}

bool goth_mem_check_range(const void *pointer,
                          size_t length,
                          uint32_t required)
{
    uintptr_t start;
    size_t index;

    if (!mem_initialized) {
        return false;
    }
    if (pointer == NULL || length == 0U) {
        return false;
    }
    start = (uintptr_t)pointer;
    if (start + length < start) {
        return false;
    }

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base == 0U) {
            continue;
        }
        if (!range_overlaps(start, length, regions[index].base,
                            regions[index].size)) {
            continue;
        }
        if ((regions[index].flags & required) != required) {
            return false;
        }
    }
    return true;
}

bool goth_mem_check_user(const void *pointer,
                         size_t length,
                         uint32_t required)
{
    uintptr_t start;
    goth_task_t *current;
    goth_task_id_t current_id = 0U;
    size_t index;

    if (!goth_mem_check_range(pointer, length, required)) {
        return false;
    }

    start = (uintptr_t)pointer;
    current = goth_kernel_current_task();
    if (current != NULL) {
        current_id = current->id;
    }

    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base == 0U) {
            continue;
        }
        if (!range_overlaps(start, length, regions[index].base,
                            regions[index].size)) {
            continue;
        }
        if (regions[index].kind != GOTH_MEM_USER &&
            (regions[index].flags & GOTH_MEM_USER_ACCESS) == 0U) {
            return false;
        }
        if (regions[index].owner != 0U && current_id != 0U &&
            regions[index].owner != current_id) {
            return false;
        }
    }
    return true;
}

goth_status_t goth_mem_guard_task(goth_task_t *task)
{
    goth_status_t status;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!mem_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }

    status = goth_mem_register(task->stack, task->stack_size,
                               GOTH_MEM_READ | GOTH_MEM_WRITE |
                                   GOTH_MEM_USER_ACCESS,
                               GOTH_MEM_USER, task->id, "task stack");
    if (status != GOTH_STATUS_OK) {
        return status;
    }

    status = goth_um_task_guard(task);
    if (status != GOTH_STATUS_OK) {
        (void)goth_mem_unregister(task->stack);
        return status;
    }
    return GOTH_STATUS_OK;
}

void goth_mem_forget_task(goth_task_id_t id)
{
    size_t index;

    if (!mem_initialized || id == 0U) {
        return;
    }
    for (index = 0U; index < GOTH_MEM_MAX_REGIONS; index++) {
        if (regions[index].base != 0U &&
            regions[index].kind == GOTH_MEM_USER &&
            regions[index].owner == id) {
            regions[index].base = 0U;
            regions[index].size = 0U;
            regions[index].flags = 0U;
            regions[index].kind = GOTH_MEM_KERNEL;
            regions[index].owner = 0U;
            regions[index].name = NULL;
        }
    }
}

bool goth_mem_check_task_stack(const goth_task_t *task)
{
    if (task == NULL) {
        return false;
    }
    if (!goth_um_task_guard_valid((goth_task_t *)task)) {
        violations++;
        goth_klog_printf(GOTH_LOG_CRIT,
                         "stack guard corrupted in task %u (%s)",
                         (unsigned int)task->id,
                         task->stack != NULL ? "canary mismatch"
                                             : "missing stack");
        return false;
    }
    return true;
}

uint64_t goth_mem_violations(void)
{
    return violations;
}

void goth_mem_note_violation(const char *reason)
{
    violations++;
    goth_klog_printf(GOTH_LOG_WARNING, "memory protection: %s",
                     reason != NULL ? reason : "denied access");
}
