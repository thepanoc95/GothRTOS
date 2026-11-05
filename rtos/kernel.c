#include "gothrtos/kernel.h"
#include "um_internal.h"

#include "version.h"

static goth_task_t *task_list;
static goth_task_t *current_task;
static goth_task_t *last_scheduled;
static goth_task_id_t next_task_id;
static uint64_t system_ticks;
static bool kernel_initialized;

static void reset_task_list(void)
{
    goth_task_t *task = task_list;

    while (task != NULL) {
        goth_task_t *next = task->next;

        task->id = 0U;
        task->entry = NULL;
        task->argument = NULL;
        task->stack = NULL;
        task->stack_size = 0U;
        task->priority = 0U;
        task->state = GOTH_TASK_UNUSED;
        task->sleep_ticks = 0U;
        task->port_context = NULL;
        task->next = NULL;
        task = next;
    }
}

static bool task_is_registered(const goth_task_t *candidate)
{
    const goth_task_t *task;

    for (task = task_list; task != NULL; task = task->next) {
        if (task == candidate) {
            return true;
        }
    }
    return false;
}

static void advance_ticks(uint32_t ticks)
{
    while (ticks-- > 0U) {
        goth_task_t *task;

        system_ticks++;
        for (task = task_list; task != NULL; task = task->next) {
            if (task->state == GOTH_TASK_SLEEPING) {
                task->sleep_ticks--;
                if (task->sleep_ticks == 0U) {
                    task->state = GOTH_TASK_READY;
                }
            }
        }
    }
}

static void update_elapsed_ticks(void)
{
    uint32_t elapsed_ticks = goth_um_collect_ticks();

    if (elapsed_ticks > 0U) {
        advance_ticks(elapsed_ticks);
    }
}

void goth_kernel_init(void)
{
    if (goth_um_is_running()) {
        return;
    }

    if (goth_um_platform_reset(task_list) != GOTH_STATUS_OK) {
        return;
    }
    reset_task_list();
    task_list = NULL;
    current_task = NULL;
    last_scheduled = NULL;
    next_task_id = 1U;
    system_ticks = 0U;
    kernel_initialized = goth_um_platform_init() == GOTH_STATUS_OK;
}

bool goth_kernel_is_initialized(void)
{
    return kernel_initialized;
}

goth_status_t goth_task_create(goth_task_t *task,
                               void *stack,
                               size_t stack_size,
                               uint8_t priority,
                               goth_task_entry_t entry,
                               void *argument)
{
    goth_task_t **tail;

    if (!kernel_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL || stack == NULL || stack_size == 0U || entry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task->state != GOTH_TASK_UNUSED) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (!kernel_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }

    task->id = next_task_id++;
    if (next_task_id == 0U) {
        next_task_id = 1U;
    }
    task->entry = entry;
    task->argument = argument;
    task->stack = stack;
    task->stack_size = stack_size;
    task->priority = priority;
    task->state = GOTH_TASK_READY;
    task->sleep_ticks = 0U;
    task->port_context = NULL;
    task->next = NULL;

    {
        goth_status_t status = goth_um_task_create(task);

        if (status != GOTH_STATUS_OK) {
            task->id = 0U;
            task->entry = NULL;
            task->argument = NULL;
            task->stack = NULL;
            task->stack_size = 0U;
            task->priority = 0U;
            task->state = GOTH_TASK_UNUSED;
            return status;
        }
    }

    tail = &task_list;
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }
    *tail = task;
    return GOTH_STATUS_OK;
}

goth_status_t goth_task_destroy(goth_task_t *task)
{
    goth_task_t **link;

    if (!kernel_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task == current_task || task->state == GOTH_TASK_RUNNING) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (task->state == GOTH_TASK_UNUSED) {
        return GOTH_STATUS_BAD_STATE;
    }

    if (!task_is_registered(task)) {
        return GOTH_STATUS_BAD_STATE;
    }

    {
        goth_status_t status = goth_um_task_destroy(task);

        if (status != GOTH_STATUS_OK) {
            return status;
        }
    }
    link = &task_list;
    while (*link != task) {
        link = &(*link)->next;
    }
    *link = task->next;
    if (last_scheduled == task) {
        last_scheduled = NULL;
    }
    task->id = 0U;
    task->entry = NULL;
    task->argument = NULL;
    task->stack = NULL;
    task->stack_size = 0U;
    task->priority = 0U;
    task->state = GOTH_TASK_UNUSED;
    task->sleep_ticks = 0U;
    task->port_context = NULL;
    task->next = NULL;
    return GOTH_STATUS_OK;
}

goth_task_t *goth_kernel_schedule(void)
{
    goth_task_t *previous;
    goth_task_t *start;
    goth_task_t *task;
    goth_task_t *best = NULL;

    if (!kernel_initialized) {
        return NULL;
    }

    previous = current_task;
    if (previous != NULL && previous->state == GOTH_TASK_RUNNING) {
        previous->state = GOTH_TASK_READY;
    }
    current_task = NULL;

    if (task_list == NULL) {
        last_scheduled = NULL;
        goth_um_context_switch(previous, NULL);
        return NULL;
    }

    start = task_list;
    if (last_scheduled != NULL) {
        start = last_scheduled->next != NULL ? last_scheduled->next : task_list;
    }

    task = start;
    do {
        if (task->state == GOTH_TASK_READY &&
            (best == NULL || task->priority > best->priority)) {
            best = task;
        }
        task = task->next != NULL ? task->next : task_list;
    } while (task != start);

    if (best != NULL) {
        best->state = GOTH_TASK_RUNNING;
        current_task = best;
        last_scheduled = best;
    }
    goth_um_context_switch(previous, best);
    return best;
}

goth_task_t *goth_kernel_current_task(void)
{
    return current_task;
}

goth_task_t *goth_kernel_tick(void)
{
    if (!kernel_initialized) {
        return NULL;
    }

    advance_ticks(1U);
    return goth_kernel_schedule();
}

uint64_t goth_kernel_ticks(void)
{
    return system_ticks;
}

bool goth_kernel_has_sleeping_tasks(void)
{
    const goth_task_t *task;

    for (task = task_list; task != NULL; task = task->next) {
        if (task->state == GOTH_TASK_SLEEPING) {
            return true;
        }
    }
    return false;
}

goth_task_t *goth_task_yield(void)
{
    if (!kernel_initialized || current_task == NULL) {
        return NULL;
    }
    update_elapsed_ticks();
    return goth_kernel_schedule();
}

goth_task_t *goth_task_sleep(uint32_t ticks)
{
    if (!kernel_initialized || current_task == NULL || ticks == 0U) {
        return NULL;
    }

    update_elapsed_ticks();
    current_task->sleep_ticks = ticks;
    current_task->state = GOTH_TASK_SLEEPING;
    return goth_kernel_schedule();
}

goth_task_t *goth_task_exit(void)
{
    if (!kernel_initialized || current_task == NULL) {
        return NULL;
    }

    update_elapsed_ticks();
    current_task->state = GOTH_TASK_TERMINATED;
    current_task->sleep_ticks = 0U;
    return goth_kernel_schedule();
}

goth_status_t goth_task_suspend(goth_task_t *task)
{
    if (!kernel_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!task_is_registered(task)) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (task->state != GOTH_TASK_READY && task->state != GOTH_TASK_SLEEPING) {
        return GOTH_STATUS_BAD_STATE;
    }
    task->state = GOTH_TASK_SUSPENDED;
    task->sleep_ticks = 0U;
    return GOTH_STATUS_OK;
}

goth_status_t goth_task_resume(goth_task_t *task)
{
    if (!kernel_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!task_is_registered(task)) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (task->state != GOTH_TASK_SUSPENDED) {
        return GOTH_STATUS_BAD_STATE;
    }
    task->state = GOTH_TASK_READY;
    return GOTH_STATUS_OK;
}
