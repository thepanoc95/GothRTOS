#include "pe_internal.h"
#include "pe_arm32.h"
#include "um_internal.h"

#include "gothrtos/arm32.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GOTH_ARM32_MAX_TASKS 16U
#define GOTH_ARM32_MIN_STACK 256U

typedef struct {
    uint32_t *sp;
    goth_task_t *task;
    bool used;
} goth_arm32_context_t;

static goth_arm32_context_t task_contexts[GOTH_ARM32_MAX_TASKS];
static goth_arm32_context_t idle_context;
static goth_arm32_context_t *active_context;
static bool port_initialized;
static bool um_running;
static uint32_t um_tick_period_ms;
static uint64_t um_last_tick_ns;

static const goth_arm_board_ops_t *board_ops(void)
{
    static const goth_arm_board_ops_t *ops;

    if (ops == NULL) {
        ops = goth_arm_board_get_ops();
    }
    return ops;
}

static uint64_t arm32_now_ns(void)
{
    const goth_arm_board_ops_t *board = board_ops();

    return board != NULL && board->time_ns != NULL ? board->time_ns() : 0U;
}

void goth_arm32_cpu_relax(void)
{
    __asm__ volatile("yield" ::: "memory");
}

void goth_arm32_exit(int status)
{
    const goth_arm_board_ops_t *board = board_ops();

    if (board != NULL && board->exit != NULL) {
        board->exit(status);
    }
    for (;;) {
        goth_arm32_cpu_relax();
    }
}

static goth_arm32_context_t *arm32_alloc_context(void)
{
    uint32_t index;

    for (index = 0U; index < GOTH_ARM32_MAX_TASKS; index++) {
        if (!task_contexts[index].used) {
            task_contexts[index].used = true;
            task_contexts[index].task = NULL;
            task_contexts[index].sp = NULL;
            return &task_contexts[index];
        }
    }
    return NULL;
}

static void arm32_release_context(goth_arm32_context_t *context)
{
    if (context == NULL || context == &idle_context) {
        return;
    }
    context->sp = NULL;
    context->task = NULL;
    context->used = false;
}

goth_status_t goth_um_platform_init(void)
{
    const goth_arm_board_ops_t *board = board_ops();

    if (port_initialized) {
        return GOTH_STATUS_OK;
    }
    if (board == NULL || board->uart_init == NULL) {
        return GOTH_STATUS_PLATFORM_ERROR;
    }

    board->uart_init();
    active_context = &idle_context;
    idle_context.sp = NULL;
    idle_context.task = NULL;
    idle_context.used = true;
    port_initialized = true;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_platform_reset(goth_task_t *tasks)
{
    goth_task_t *task;
    uint32_t index;

    if (um_running) {
        return GOTH_STATUS_BAD_STATE;
    }

    for (task = tasks; task != NULL; task = task->next) {
        arm32_release_context((goth_arm32_context_t *)task->port_context);
        task->port_context = NULL;
    }
    for (index = 0U; index < GOTH_ARM32_MAX_TASKS; index++) {
        task_contexts[index].sp = NULL;
        task_contexts[index].task = NULL;
        task_contexts[index].used = false;
    }

    active_context = &idle_context;
    idle_context.sp = NULL;
    idle_context.task = NULL;
    idle_context.used = true;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_create(goth_task_t *task)
{
    goth_arm32_context_t *context;
    uintptr_t stack_top;
    uint32_t *frame;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!port_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task->stack_size < GOTH_ARM32_MIN_STACK) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    context = arm32_alloc_context();
    if (context == NULL) {
        return GOTH_STATUS_FULL;
    }

    stack_top = ((uintptr_t)task->stack + task->stack_size) &
                ~(uintptr_t)7U;
    frame = (uint32_t *)stack_top - 10U;

    /* Matches the frame consumed by goth_arm_context_switch:
       padding, r4-r11, lr. r4 carries the task pointer to the trampoline. */
    frame[0] = 0U;
    frame[1] = (uint32_t)(uintptr_t)task;
    frame[2] = 0U;
    frame[3] = 0U;
    frame[4] = 0U;
    frame[5] = 0U;
    frame[6] = 0U;
    frame[7] = 0U;
    frame[8] = 0U;
    frame[9] = (uint32_t)(uintptr_t)goth_arm_task_trampoline;

    context->sp = frame;
    context->task = task;
    task->port_context = context;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_destroy(goth_task_t *task)
{
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!port_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task->port_context != NULL) {
        if ((goth_arm32_context_t *)task->port_context == active_context) {
            return GOTH_STATUS_BAD_STATE;
        }
        arm32_release_context((goth_arm32_context_t *)task->port_context);
        task->port_context = NULL;
    }
    return GOTH_STATUS_OK;
}

void goth_um_context_switch(goth_task_t *previous, goth_task_t *next)
{
    goth_arm32_context_t *from = active_context;
    goth_arm32_context_t *to;

    (void)previous;

    if (!port_initialized || !um_running) {
        return;
    }

    if (next != NULL) {
        to = (goth_arm32_context_t *)next->port_context;
        if (to == NULL) {
            return;
        }
    } else {
        to = &idle_context;
    }

    if (from == NULL || from == to) {
        return;
    }

    active_context = to;
    goth_arm_context_switch(&from->sp, to->sp);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ns = arm32_now_ns();
}

uint32_t goth_um_collect_ticks(void)
{
    uint64_t now;
    uint64_t elapsed;
    uint64_t period_ns;
    uint64_t ticks;

    if (!um_running || um_tick_period_ms == 0U) {
        return 0U;
    }

    now = arm32_now_ns();
    if (now < um_last_tick_ns) {
        um_last_tick_ns = now;
        return 0U;
    }

    elapsed = now - um_last_tick_ns;
    period_ns = (uint64_t)um_tick_period_ms * UINT64_C(1000000);
    ticks = elapsed / period_ns;
    um_last_tick_ns += ticks * period_ns;

    return ticks > UINT32_MAX ? UINT32_MAX : (uint32_t)ticks;
}

goth_status_t goth_um_run(uint32_t tick_period_ms)
{
    goth_status_t status;

    if (tick_period_ms == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    status = goth_um_platform_init();
    if (status != GOTH_STATUS_OK) {
        return status;
    }
    if (!goth_kernel_is_initialized()) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (um_running) {
        return GOTH_STATUS_BAD_STATE;
    }

    um_running = true;
    goth_um_start_clock(tick_period_ms);

    for (;;) {
        if (goth_kernel_schedule() == NULL) {
            uint32_t elapsed_ticks;

            if (!goth_kernel_has_sleeping_tasks()) {
                break;
            }

            do {
                goth_arm32_cpu_relax();
                elapsed_ticks = goth_um_collect_ticks();
            } while (elapsed_ticks == 0U);

            while (elapsed_ticks-- > 0U) {
                (void)goth_kernel_tick();
            }
        }
    }

    um_running = false;
    return GOTH_STATUS_OK;
}

void goth_arm_task_start(goth_task_t *task)
{
    if (task == NULL) {
        return;
    }

    task->entry(task->argument);
    (void)goth_task_exit();

    for (;;) {
        goth_arm32_cpu_relax();
    }
}

static goth_pe_status_t arm32_pe_initialize(goth_pe_info_t *info)
{
    const goth_arm_board_ops_t *board = board_ops();

    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->uart_init == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    board->uart_init();
    info->platform_name = board->name;
    info->architecture = "arm32";
    info->logical_cpu_count = 1U;
    info->pointer_width = 32U;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t arm32_pe_time_ns(uint64_t *time_ns)
{
    const goth_arm_board_ops_t *board = board_ops();

    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->time_ns == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    *time_ns = board->time_ns();
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t arm32_pe_console_write(const char *text, size_t length)
{
    const goth_arm_board_ops_t *board = board_ops();
    size_t index;

    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->uart_putc == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    for (index = 0U; index < length; index++) {
        if (text[index] == '\n') {
            board->uart_putc('\r');
        }
        board->uart_putc(text[index]);
    }
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t arm32_pe_console_read(char *buffer,
                                              size_t capacity,
                                              size_t *read_length)
{
    const goth_arm_board_ops_t *board = board_ops();
    size_t count = 0U;

    if (buffer == NULL || read_length == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->uart_getc == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    /* Drain whatever the UART has already received, up to capacity. */
    while (count < capacity) {
        int character = board->uart_getc();

        if (character < 0) {
            break;
        }
        buffer[count++] = (char)character;
    }
    *read_length = count;
    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        arm32_pe_initialize,
        arm32_pe_time_ns,
        arm32_pe_console_write,
        arm32_pe_console_read
    };

    return &platform_ops;
}
