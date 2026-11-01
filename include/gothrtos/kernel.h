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
    GOTH_STATUS_PLATFORM_ERROR
} goth_status_t;

typedef enum {
    GOTH_TASK_UNUSED = 0,
    GOTH_TASK_READY,
    GOTH_TASK_RUNNING,
    GOTH_TASK_SLEEPING,
    GOTH_TASK_SUSPENDED,
    GOTH_TASK_TERMINATED
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
goth_task_t *goth_kernel_current_task(void);
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

#endif
