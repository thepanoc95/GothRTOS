#include "pe_internal.h"
#include "pe_amd64.h"
#include "um_internal.h"

#include "gothrtos/amd64.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* AMD64 bare-metal platform expert.
 *
 * This is the 64-bit sibling of the ARM32 port (pexpert/arm/pe_arm32.c): the
 * cooperative scheduler hooks, guard words and console glue are the same, and
 * only the CPU context switch and the board-specific hardware differ. Keeping
 * the two ports structurally identical is deliberate, so a change to the
 * scheduling contract has to be made in both. */

#define GOTH_AMD64_MAX_TASKS 16U
#define GOTH_AMD64_MIN_STACK 256U

typedef struct {
    uint64_t *sp;
    goth_task_t *task;
    bool used;
} goth_amd64_context_t;

static goth_amd64_context_t task_contexts[GOTH_AMD64_MAX_TASKS];
static goth_amd64_context_t idle_context;
static goth_amd64_context_t *active_context;
static bool port_initialized;
static bool um_running;
static uint32_t um_tick_period_ms;
static uint64_t um_last_tick_ns;

static const goth_amd64_board_ops_t *board_ops(void)
{
    static const goth_amd64_board_ops_t *ops;

    if (ops == NULL) {
        ops = goth_amd64_board_get_ops();
    }
    return ops;
}

static uint64_t amd64_now_ns(void)
{
    const goth_amd64_board_ops_t *board = board_ops();

    return board != NULL && board->time_ns != NULL ? board->time_ns() : 0U;
}

void goth_amd64_cpu_relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}

uint64_t goth_amd64_read_tsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

void goth_amd64_exit(int status)
{
    const goth_amd64_board_ops_t *board = board_ops();

    if (board != NULL && board->exit != NULL) {
        board->exit(status);
    }
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static goth_amd64_context_t *amd64_alloc_context(void)
{
    uint32_t index;

    for (index = 0U; index < GOTH_AMD64_MAX_TASKS; index++) {
        if (!task_contexts[index].used) {
            task_contexts[index].used = true;
            task_contexts[index].task = NULL;
            task_contexts[index].sp = NULL;
            return &task_contexts[index];
        }
    }
    return NULL;
}

static void amd64_release_context(goth_amd64_context_t *context)
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
    const goth_amd64_board_ops_t *board = board_ops();

    if (port_initialized) {
        return GOTH_STATUS_OK;
    }
    if (board == NULL || board->serial_init == NULL) {
        return GOTH_STATUS_PLATFORM_ERROR;
    }

    board->serial_init();
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
        amd64_release_context((goth_amd64_context_t *)task->port_context);
        task->port_context = NULL;
    }
    for (index = 0U; index < GOTH_AMD64_MAX_TASKS; index++) {
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
    goth_amd64_context_t *context;
    uintptr_t stack_top;
    uint64_t *frame;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!port_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (task->stack_size < GOTH_AMD64_MIN_STACK) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    context = amd64_alloc_context();
    if (context == NULL) {
        return GOTH_STATUS_FULL;
    }

    stack_top = ((uintptr_t)task->stack + task->stack_size) &
                ~(uintptr_t)15U;
    /* Seven 8-byte slots: r15, r14, r13, r12, rbx, rbp and the return
       address consumed by goth_amd64_context_switch. r12 carries the task
       pointer to the trampoline. */
    frame = (uint64_t *)stack_top - 7U;

    frame[0] = 0U;                                             /* r15 */
    frame[1] = 0U;                                             /* r14 */
    frame[2] = 0U;                                             /* r13 */
    frame[3] = (uint64_t)(uintptr_t)task;                      /* r12 */
    frame[4] = 0U;                                             /* rbx */
    frame[5] = 0U;                                             /* rbp */
    frame[6] = (uint64_t)(uintptr_t)goth_amd64_task_trampoline; /* rip */

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
        if ((goth_amd64_context_t *)task->port_context == active_context) {
            return GOTH_STATUS_BAD_STATE;
        }
        amd64_release_context((goth_amd64_context_t *)task->port_context);
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
    /* The x86 stack grows down from the high end, so the low words are the
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
    goth_amd64_context_t *from = active_context;
    goth_amd64_context_t *to;

    (void)previous;

    if (!port_initialized || !um_running) {
        return;
    }

    if (next != NULL) {
        to = (goth_amd64_context_t *)next->port_context;
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
    goth_amd64_context_switch(&from->sp, to->sp);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ns = amd64_now_ns();
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

    now = amd64_now_ns();
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
                goth_amd64_cpu_relax();
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

void goth_amd64_task_start(goth_task_t *task)
{
    if (task == NULL) {
        return;
    }

    task->entry(task->argument);
    (void)goth_task_exit();

    for (;;) {
        goth_amd64_cpu_relax();
    }
}

static goth_pe_status_t amd64_pe_initialize(goth_pe_info_t *info)
{
    const goth_amd64_board_ops_t *board = board_ops();

    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->serial_init == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    board->serial_init();
    info->platform_name = board->name;
    info->architecture = "amd64";
    info->logical_cpu_count = 1U;
    info->pointer_width = 64U;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t amd64_pe_time_ns(uint64_t *time_ns)
{
    const goth_amd64_board_ops_t *board = board_ops();

    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->time_ns == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    *time_ns = board->time_ns();
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t amd64_pe_console_write(const char *text, size_t length)
{
    const goth_amd64_board_ops_t *board = board_ops();
    size_t index;

    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->serial_putc == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    for (index = 0U; index < length; index++) {
        if (text[index] == '\n') {
            board->serial_putc('\r');
        }
        board->serial_putc(text[index]);
    }
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t amd64_pe_console_read(char *buffer,
                                              size_t capacity,
                                              size_t *read_length)
{
    const goth_amd64_board_ops_t *board = board_ops();
    size_t count = 0U;

    if (buffer == NULL || read_length == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (board == NULL || board->serial_getc == NULL) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    while (count < capacity) {
        int character = board->serial_getc();

        if (character < 0) {
            break;
        }
        buffer[count++] = (char)character;
    }
    *read_length = count;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t amd64_pe_hdf_register(void)
{
    const goth_amd64_board_ops_t *board = board_ops();

    if (board != NULL && board->hdf_register != NULL) {
        board->hdf_register();
    }
    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        amd64_pe_initialize,
        amd64_pe_time_ns,
        amd64_pe_console_write,
        amd64_pe_console_read,
        amd64_pe_hdf_register
    };

    return &platform_ops;
}
