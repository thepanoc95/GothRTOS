#include "pe_internal.h"
#include "pe_cortexm.h"
#include "um_internal_cm.h"

#include "gothrtos/cortexm.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GOTH_CM_MAX_TASKS 16U
#define GOTH_CM_MIN_STACK 256U

typedef struct {
    uint32_t *sp;
    goth_task_t *task;
    bool used;
} goth_cm_context_t;

static goth_cm_context_t task_contexts[GOTH_CM_MAX_TASKS];
static goth_cm_context_t idle_context;
static goth_cm_context_t *active_context;
static bool port_initialized;
static bool um_running;
static uint32_t um_tick_period_ms;
static uint64_t um_last_tick_ns;

static const goth_cm_board_ops_t *board_ops(void)
{
    static const goth_cm_board_ops_t *ops;

    if (ops == NULL) {
        ops = goth_cm_board_get_ops();
    }
    return ops;
}

static uint64_t cm_now_ns(void)
{
    const goth_cm_board_ops_t *board = board_ops();

    return board != NULL && board->time_ns != NULL ? board->time_ns() : 0U;
}

void goth_cm_cpu_relax(void)
{
    __asm__ volatile("yield" ::: "memory");
}

void goth_cm_exit(int status)
{
    const goth_cm_board_ops_t *board = board_ops();

    if (board != NULL && board->exit != NULL) {
        board->exit(status);
    }
    for (;;) {
        goth_cm_cpu_relax();
    }
}

static goth_cm_context_t *cm_alloc_context(void)
{
    uint32_t index;

    for (index = 0U; index < GOTH_CM_MAX_TASKS; index++) {
        if (!task_contexts[index].used) {
            task_contexts[index].used = true;
            task_contexts[index].task = NULL;
            task_contexts[index].sp = NULL;
            return &task_contexts[index];
        }
    }
    return NULL;
}

static void cm_release_context(goth_cm_context_t *context)
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
    const goth_cm_board_ops_t *board = board_ops();

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
        cm_release_context((goth_cm_context_t *)task->port_context);
        task->port_context = NULL;
    }
    for (index = 0U; index < GOTH_CM_MAX_TASKS; index++) {
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
    goth_cm_context_t *context;
    uintptr_t stack_top;
    uint32_t *frame;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!port_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task->stack_size < GOTH_CM_MIN_STACK) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    context = cm_alloc_context();
    if (context == NULL) {
        return GOTH_STATUS_FULL;
    }

    stack_top = ((uintptr_t)task->stack + task->stack_size) &
                ~(uintptr_t)7U;
    frame = (uint32_t *)stack_top - 10U;

    /* Matches the frame consumed by goth_cm_context_switch. The switch saves
       in the order lr, r8-r11, r4-r7 below a padding word, so the frame is
       padding, lr (the trampoline), r8-r11, r4-r7. r4 carries the task
       pointer to the trampoline. */
    frame[0] = 0U;                                             /* padding */
    frame[1] = (uint32_t)(uintptr_t)goth_cm_task_trampoline;   /* lr */
    frame[2] = 0U;                                             /* r8 */
    frame[3] = 0U;                                             /* r9 */
    frame[4] = 0U;                                             /* r10 */
    frame[5] = 0U;                                             /* r11 */
    frame[6] = (uint32_t)(uintptr_t)task;                      /* r4 */
    frame[7] = 0U;                                             /* r5 */
    frame[8] = 0U;                                             /* r6 */
    frame[9] = 0U;                                             /* r7 */

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
        if ((goth_cm_context_t *)task->port_context == active_context) {
            return GOTH_STATUS_BAD_STATE;
        }
        cm_release_context((goth_cm_context_t *)task->port_context);
        task->port_context = NULL;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_guard(goth_task_t *task)
{
    uint32_t *words;

    if (task == NULL || task->stack == NULL || task->stack_size < 8U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    /* The ARM stack grows down from the high end, so the low words are the
     * first thing a stack overflow reaches. */
    words = (uint32_t *)task->stack;
    words[0] = GOTH_UM_GUARD_WORD;
    words[1] = GOTH_UM_GUARD_WORD ^ GOTH_UM_GUARD_XOR;
    return GOTH_STATUS_OK;
}

bool goth_um_task_guard_valid(goth_task_t *task)
{
    const uint32_t *words;

    if (task == NULL || task->stack == NULL || task->stack_size < 8U) {
        return false;
    }
    words = (const uint32_t *)task->stack;
    return words[0] == GOTH_UM_GUARD_WORD &&
           words[1] == (GOTH_UM_GUARD_WORD ^ GOTH_UM_GUARD_XOR);
}

void goth_um_context_switch(goth_task_t *previous, goth_task_t *next)
{
    goth_cm_context_t *from = active_context;
    goth_cm_context_t *to;

    (void)previous;

    if (!port_initialized || !um_running) {
        return;
    }

    if (next != NULL) {
        to = (goth_cm_context_t *)next->port_context;
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
    goth_cm_context_switch(&from->sp, to->sp);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ns = cm_now_ns();
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

    now = cm_now_ns();
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
                goth_cm_cpu_relax();
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

void goth_cm_task_start(goth_task_t *task)
{
    if (task == NULL) {
        return;
    }

    task->entry(task->argument);
    (void)goth_task_exit();

    for (;;) {
        goth_cm_cpu_relax();
    }
}

static goth_pe_status_t cm_pe_initialize(goth_pe_info_t *info)
{
    const goth_cm_board_ops_t *board = board_ops();

    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->uart_init == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    board->uart_init();
    info->platform_name = board->name;
    info->architecture = "cortex-m";
    info->logical_cpu_count = 1U;
    info->pointer_width = 32U;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t cm_pe_time_ns(uint64_t *time_ns)
{
    const goth_cm_board_ops_t *board = board_ops();

    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->time_ns == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    *time_ns = board->time_ns();
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t cm_pe_console_write(const char *text, size_t length)
{
    const goth_cm_board_ops_t *board = board_ops();
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

static goth_pe_status_t cm_pe_console_read(char *buffer,
                                              size_t capacity,
                                              size_t *read_length)
{
    const goth_cm_board_ops_t *board = board_ops();
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

static goth_pe_status_t cm_pe_hdf_register(void)
{
    const goth_cm_board_ops_t *board = board_ops();

    if (board != NULL && board->hdf_register != NULL) {
        board->hdf_register();
    }
    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        cm_pe_initialize,
        cm_pe_time_ns,
        cm_pe_console_write,
        cm_pe_console_read,
        cm_pe_hdf_register
    };

    return &platform_ops;
}
