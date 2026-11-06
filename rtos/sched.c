#include "gothrtos/kernel.h"

#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/nanokernel.h"
#include "kinternal.h"
#include "um_internal.h"

/* Task scheduler for the GothRTOS nanokernel.
 *
 * The ready set is a single intrusive list. Selection starts after the task
 * that ran last, picks the highest-priority READY task and uses the list
 * order as the tie breaker. That gives strict priority plus round-robin
 * fairness between equal-priority tasks, whether the switch was requested by
 * a cooperative yield or by a preemptive tick. */

static goth_task_t *task_list;
static goth_task_t *current_task;
static goth_task_t *last_scheduled;
static goth_task_id_t next_task_id;
static uint64_t system_ticks;
static goth_sched_policy_t sched_policy;
static uint32_t sched_quantum;
static bool async_preempt;

/* Set while the scheduler is touching its own state. A timer interrupt may
 * not re-enter the scheduler during that window. It is cleared immediately
 * before a context switch, when the task is about to run on its own. */
static volatile bool scheduler_critical;

static void reset_task(goth_task_t *task)
{
    task->id = 0U;
    task->entry = NULL;
    task->argument = NULL;
    task->stack = NULL;
    task->stack_size = 0U;
    task->priority = 0U;
    task->state = GOTH_TASK_UNUSED;
    task->sleep_ticks = 0U;
    task->slice_ticks = 0U;
    task->total_ticks = 0U;
    task->port_context = NULL;
    task->next = NULL;
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
        if (current_task != NULL && current_task->state == GOTH_TASK_RUNNING) {
            current_task->total_ticks++;
            current_task->slice_ticks++;
        }
        for (task = task_list; task != NULL; task = task->next) {
            if (task->state == GOTH_TASK_SLEEPING) {
                if (task->sleep_ticks > 0U) {
                    task->sleep_ticks--;
                }
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

void goth_sched_reset(void)
{
    goth_task_t *task = task_list;

    while (task != NULL) {
        goth_task_t *next = task->next;

        reset_task(task);
        task = next;
    }

    task_list = NULL;
    current_task = NULL;
    last_scheduled = NULL;
    next_task_id = 1U;
    system_ticks = 0U;
    sched_policy = GOTH_SCHED_PREEMPTIVE;
    sched_quantum = 1U;
    async_preempt = false;
    scheduler_critical = false;
}

goth_task_t *goth_sched_task_list(void)
{
    return task_list;
}

goth_status_t goth_kernel_set_scheduler(goth_sched_policy_t policy)
{
    if (policy != GOTH_SCHED_COOPERATIVE &&
        policy != GOTH_SCHED_PREEMPTIVE) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    sched_policy = policy;
    async_preempt = policy == GOTH_SCHED_PREEMPTIVE;
    return GOTH_STATUS_OK;
}

goth_sched_policy_t goth_kernel_get_scheduler(void)
{
    return sched_policy;
}

void goth_kernel_set_preemptive(bool enabled)
{
    (void)goth_kernel_set_scheduler(enabled ? GOTH_SCHED_PREEMPTIVE
                                            : GOTH_SCHED_COOPERATIVE);
}

bool goth_kernel_is_preemptive(void)
{
    return sched_policy == GOTH_SCHED_PREEMPTIVE;
}

goth_status_t goth_kernel_set_quantum(uint32_t ticks)
{
    if (ticks == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    sched_quantum = ticks;
    return GOTH_STATUS_OK;
}

uint32_t goth_kernel_quantum(void)
{
    return sched_quantum;
}

bool goth_kernel_async_preempt_enabled(void)
{
    return async_preempt && sched_policy == GOTH_SCHED_PREEMPTIVE;
}

bool goth_kernel_preempt_allowed(void)
{
    return !scheduler_critical;
}

goth_status_t goth_task_create(goth_task_t *task,
                               void *stack,
                               size_t stack_size,
                               uint8_t priority,
                               goth_task_entry_t entry,
                               void *argument)
{
    goth_task_t **tail;

    if (!goth_kernel_is_initialized()) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL || stack == NULL || stack_size == 0U || entry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task->state != GOTH_TASK_UNUSED) {
        return GOTH_STATUS_BAD_STATE;
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
    task->slice_ticks = 0U;
    task->total_ticks = 0U;
    task->port_context = NULL;
    task->next = NULL;

    {
        goth_status_t status = goth_um_task_create(task);

        if (status != GOTH_STATUS_OK) {
            reset_task(task);
            return status;
        }
    }

    {
        goth_status_t status = goth_mem_guard_task(task);

        if (status != GOTH_STATUS_OK) {
            (void)goth_um_task_destroy(task);
            reset_task(task);
            return status;
        }
    }

    tail = &task_list;
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }
    *tail = task;
    goth_klog_printf(GOTH_LOG_DEBUG,
                     "task %u created (priority %u, stack %zu bytes)",
                     (unsigned int)task->id, (unsigned int)task->priority,
                     task->stack_size);
    return GOTH_STATUS_OK;
}

goth_status_t goth_task_destroy(goth_task_t *task)
{
    goth_task_t **link;

    if (!goth_kernel_is_initialized()) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task == current_task || task->state == GOTH_TASK_RUNNING) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (task->state == GOTH_TASK_UNUSED || !task_is_registered(task)) {
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
    goth_klog_printf(GOTH_LOG_DEBUG, "task %u destroyed",
                     (unsigned int)task->id);
    goth_mem_forget_task(task->id);
    reset_task(task);
    return GOTH_STATUS_OK;
}

goth_task_t *goth_kernel_schedule(void)
{
    goth_task_t *previous;
    goth_task_t *start;
    goth_task_t *task;
    goth_task_t *best = NULL;

    if (!goth_kernel_is_initialized() || goth_kernel_panicked()) {
        return NULL;
    }

    scheduler_critical = true;

    previous = current_task;
    if (previous != NULL && !goth_mem_check_task_stack(previous)) {
        scheduler_critical = false;
        goth_kernel_panic("task stack guard corrupted");
        return NULL;
    }
    if (previous != NULL && previous->state == GOTH_TASK_RUNNING) {
        previous->state = GOTH_TASK_READY;
    }
    current_task = NULL;

    if (task_list == NULL) {
        last_scheduled = NULL;
        scheduler_critical = false;
        /* The last task left the ready set: the nanokernel has finished its
         * work, so move the lifecycle to HALTED (RUNNING -> HALTED). */
        if (goth_nk_state() == GOTH_NK_STATE_RUNNING) {
            goth_nk_transition(GOTH_NK_STATE_HALTED);
        }
        goth_um_context_switch(previous, NULL);
        return NULL;
    }

    /* First dispatch moves the lifecycle from READY to RUNNING. */
    if (goth_nk_state() == GOTH_NK_STATE_READY) {
        goth_nk_transition(GOTH_NK_STATE_RUNNING);
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
        best->slice_ticks = 0U;
        current_task = best;
        last_scheduled = best;
    }

    scheduler_critical = false;
    goth_um_context_switch(previous, best);
    return best;
}

goth_task_t *goth_kernel_current_task(void)
{
    return current_task;
}

goth_task_t *goth_kernel_find_task(goth_task_id_t id)
{
    goth_task_t *task;

    if (id == 0U) {
        return NULL;
    }
    for (task = task_list; task != NULL; task = task->next) {
        if (task->id == id && task->state != GOTH_TASK_UNUSED) {
            return task;
        }
    }
    return NULL;
}

size_t goth_kernel_task_count(void)
{
    const goth_task_t *task;
    size_t count = 0U;

    for (task = task_list; task != NULL; task = task->next) {
        if (task->state != GOTH_TASK_UNUSED) {
            count++;
        }
    }
    return count;
}

goth_task_t *goth_kernel_tick(void)
{
    if (!goth_kernel_is_initialized() || goth_kernel_panicked()) {
        return NULL;
    }
    if (current_task != NULL && !goth_mem_check_task_stack(current_task)) {
        goth_kernel_panic("task stack guard corrupted");
        return NULL;
    }

    advance_ticks(1U);

    if (sched_policy == GOTH_SCHED_COOPERATIVE &&
        current_task != NULL && current_task->state == GOTH_TASK_RUNNING) {
        return current_task;
    }

    if (sched_policy == GOTH_SCHED_PREEMPTIVE &&
        current_task != NULL && current_task->state == GOTH_TASK_RUNNING &&
        current_task->slice_ticks < sched_quantum) {
        return current_task;
    }

    return goth_kernel_schedule();
}

goth_task_t *goth_kernel_preempt(void)
{
    if (!goth_kernel_is_initialized()) {
        return NULL;
    }
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
    if (!goth_kernel_is_initialized() || current_task == NULL) {
        return NULL;
    }
    update_elapsed_ticks();
    return goth_kernel_schedule();
}

goth_task_t *goth_task_sleep(uint32_t ticks)
{
    if (!goth_kernel_is_initialized() || current_task == NULL || ticks == 0U) {
        return NULL;
    }

    update_elapsed_ticks();
    current_task->sleep_ticks = ticks;
    current_task->state = GOTH_TASK_SLEEPING;
    return goth_kernel_schedule();
}

goth_task_t *goth_task_exit(void)
{
    if (!goth_kernel_is_initialized() || current_task == NULL) {
        return NULL;
    }

    update_elapsed_ticks();
    {
        goth_task_id_t id = current_task->id;

        current_task->state = GOTH_TASK_TERMINATED;
        current_task->sleep_ticks = 0U;
        goth_klog_printf(GOTH_LOG_DEBUG, "task %u exited",
                         (unsigned int)id);
    }
    return goth_kernel_schedule();
}

goth_status_t goth_task_suspend(goth_task_t *task)
{
    if (!goth_kernel_is_initialized()) {
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
    if (!goth_kernel_is_initialized()) {
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
