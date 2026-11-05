#ifndef GOTHRTOS_KERNEL_H
#define GOTHRTOS_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t goth_task_id_t;
typedef void (*goth_task_entry_t)(void *argument);

typedef enum {
    GOTH_STATUS_OK = 0,
    GOTH_STATUS_INVALID_ARGUMENT,
    GOTH_STATUS_NOT_INITIALIZED,
    GOTH_STATUS_BAD_STATE,
    GOTH_STATUS_EMPTY,
    GOTH_STATUS_FULL,
    GOTH_STATUS_INVALID_SYSCALL,
    GOTH_STATUS_PLATFORM_ERROR,
    GOTH_STATUS_UNSUPPORTED,
    GOTH_STATUS_WOULD_BLOCK,
    GOTH_STATUS_FAULT
} goth_status_t;

/* Scheduler policy.
 *
 * GOTH_SCHED_COOPERATIVE: tasks change only when they explicitly yield,
 * sleep, exit or are suspended. A tick still advances the clock and wakes
 * sleepers but does not move the CPU to another task.
 *
 * GOTH_SCHED_PREEMPTIVE: the platform tick source may reschedule tasks at
 * quantum boundaries. goth_kernel_tick() enforces the quantum, and the
 * platform may drive it from a timer interrupt (bare metal) or an interval
 * timer (user mode). A task that never yields can therefore still be
 * descheduled. */
typedef enum {
    GOTH_SCHED_COOPERATIVE = 0,
    GOTH_SCHED_PREEMPTIVE = 1
} goth_sched_policy_t;

typedef enum {
    GOTH_TASK_UNUSED = 0,
    GOTH_TASK_READY,
    GOTH_TASK_RUNNING,
    GOTH_TASK_SLEEPING,
    GOTH_TASK_SUSPENDED,
    GOTH_TASK_TERMINATED,
    GOTH_TASK_BLOCKED
} goth_task_state_t;

typedef struct goth_task {
    goth_task_id_t id;
    goth_task_entry_t entry;
    void *argument;
    void *stack;
    size_t stack_size;
    uint8_t priority;
    goth_task_state_t state;
    uint32_t sleep_ticks;
    uint32_t slice_ticks;
    uint32_t total_ticks;
    void *port_context;
    struct goth_task *next;
} goth_task_t;

typedef struct {
    goth_task_id_t sender;
    uint32_t type;
    uintptr_t value[2];
} goth_message_t;

typedef struct {
    goth_message_t *storage;
    size_t capacity;
    size_t head;
    size_t count;
} goth_endpoint_t;

void goth_kernel_init(void);
bool goth_kernel_is_initialized(void);

/* Panic stops the scheduler after a protection fault or an unrecoverable
 * kernel error. The log records the reason first. */
void goth_kernel_panic(const char *reason);
bool goth_kernel_panicked(void);

/* Scheduler policy and time slice. The kernel starts preemptive with a
 * one-tick quantum, but the asynchronous platform timer is only enabled by
 * an explicit call to goth_kernel_set_preemptive(true) or
 * goth_kernel_set_scheduler(GOTH_SCHED_PREEMPTIVE). System.lua performs that
 * call during boot from its PREEMPT field. */
goth_status_t goth_kernel_set_scheduler(goth_sched_policy_t policy);
goth_sched_policy_t goth_kernel_get_scheduler(void);
void goth_kernel_set_preemptive(bool enabled);
bool goth_kernel_is_preemptive(void);
goth_status_t goth_kernel_set_quantum(uint32_t ticks);
uint32_t goth_kernel_quantum(void);
bool goth_kernel_async_preempt_enabled(void);
bool goth_kernel_preempt_allowed(void);

goth_status_t goth_task_create(goth_task_t *task,
                               void *stack,
                               size_t stack_size,
                               uint8_t priority,
                               goth_task_entry_t entry,
                               void *argument);
goth_status_t goth_task_destroy(goth_task_t *task);
goth_status_t goth_task_suspend(goth_task_t *task);
goth_status_t goth_task_resume(goth_task_t *task);
goth_task_t *goth_task_yield(void);
goth_task_t *goth_task_sleep(uint32_t ticks);
goth_task_t *goth_task_exit(void);

goth_task_t *goth_kernel_schedule(void);
goth_task_t *goth_kernel_tick(void);
goth_task_t *goth_kernel_preempt(void);
goth_task_t *goth_kernel_current_task(void);
goth_task_t *goth_kernel_find_task(goth_task_id_t id);
size_t goth_kernel_task_count(void);
uint64_t goth_kernel_ticks(void);
bool goth_kernel_has_sleeping_tasks(void);

goth_status_t goth_endpoint_init(goth_endpoint_t *endpoint,
                                 goth_message_t *storage,
                                 size_t capacity);
goth_status_t goth_endpoint_send(goth_endpoint_t *endpoint,
                                 uint32_t type,
                                 uintptr_t value0,
                                 uintptr_t value1);
goth_status_t goth_endpoint_receive(goth_endpoint_t *endpoint,
                                    goth_message_t *message);
goth_status_t goth_endpoint_poll(goth_endpoint_t *endpoint,
                                 goth_message_t *message);
size_t goth_endpoint_pending(const goth_endpoint_t *endpoint);
bool goth_endpoint_valid(const goth_endpoint_t *endpoint);

#endif
