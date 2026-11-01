#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pe_internal.h"
#include "um_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct {
    ucontext_t context;
    void *stack;
    size_t stack_size;
} goth_um_linux_context_t;

#if UINTPTR_MAX > UINT32_MAX
#define GOTH_UM_LINUX_POINTER_HIGH(pointer) \
    ((unsigned int)((uintptr_t)(pointer) >> 32))
#else
#define GOTH_UM_LINUX_POINTER_HIGH(pointer) 0U
#endif

static ucontext_t host_context;
static pid_t host_thread_id;
static bool host_context_ready;
static bool um_running;
static uint32_t um_tick_period_ms;
static uint64_t um_last_tick_ms;

static pid_t goth_um_linux_thread_id(void)
{
#ifdef SYS_gettid
    return (pid_t)syscall(SYS_gettid);
#else
    return getpid();
#endif
}

static void goth_um_linux_task_entry(unsigned int low, unsigned int high)
{
    uintptr_t value = (uintptr_t)low;
    goth_task_t *task;
    goth_um_linux_context_t *context;

#if UINTPTR_MAX > UINT32_MAX
    value |= (uintptr_t)high << 32;
#else
    (void)high;
#endif

    task = (goth_task_t *)value;
    context = (goth_um_linux_context_t *)task->port_context;
    task->entry(task->argument);
    (void)goth_task_exit();

    for (;;) {
        (void)swapcontext(&context->context, &host_context);
    }
}

static void goth_um_linux_release_context(goth_task_t *task)
{
    goth_um_linux_context_t *context;

    if (task->port_context == NULL) {
        return;
    }

    context = (goth_um_linux_context_t *)task->port_context;
    free(context->stack);
    free(context);
    task->port_context = NULL;
}

static uint64_t goth_um_linux_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) {
        return 0U;
    }

    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void goth_um_linux_sleep_ms(uint32_t milliseconds)
{
    struct timespec remaining;

    remaining.tv_sec = (time_t)(milliseconds / 1000U);
    remaining.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;

    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

goth_status_t goth_um_platform_init(void)
{
    if (host_context_ready) {
        return goth_um_linux_thread_id() == host_thread_id
                   ? GOTH_STATUS_OK
                   : GOTH_STATUS_BAD_STATE;
    }

    host_thread_id = goth_um_linux_thread_id();
    if (getcontext(&host_context) != 0) {
        host_thread_id = 0;
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    host_context_ready = true;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_platform_reset(goth_task_t *tasks)
{
    goth_task_t *task;

    if (um_running) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (host_context_ready &&
        goth_um_linux_thread_id() != host_thread_id) {
        return GOTH_STATUS_BAD_STATE;
    }

    for (task = tasks; task != NULL; task = task->next) {
        goth_um_linux_release_context(task);
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_create(goth_task_t *task)
{
    goth_um_linux_context_t *context;
    size_t stack_size;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!host_context_ready ||
        goth_um_linux_thread_id() != host_thread_id) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }

    stack_size = task->stack_size;
    if (stack_size < 64U * 1024U) {
        stack_size = 64U * 1024U;
    }

    context = (goth_um_linux_context_t *)calloc(1U, sizeof(*context));
    if (context == NULL) {
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    context->stack = malloc(stack_size);
    if (context->stack == NULL) {
        free(context);
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    context->stack_size = stack_size;

    if (getcontext(&context->context) != 0) {
        free(context->stack);
        free(context);
        return GOTH_STATUS_PLATFORM_ERROR;
    }

    context->context.uc_stack.ss_sp = context->stack;
    context->context.uc_stack.ss_size = context->stack_size;
    context->context.uc_link = &host_context;
    makecontext(&context->context,
                (void (*)(void))goth_um_linux_task_entry,
                2,
                (unsigned int)((uintptr_t)task & UINT32_MAX),
                GOTH_UM_LINUX_POINTER_HIGH(task));

    task->port_context = context;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_destroy(goth_task_t *task)
{
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task->port_context == NULL) {
        return GOTH_STATUS_OK;
    }
    if (!host_context_ready ||
        goth_um_linux_thread_id() != host_thread_id) {
        return GOTH_STATUS_BAD_STATE;
    }

    goth_um_linux_release_context(task);
    return GOTH_STATUS_OK;
}

void goth_um_context_switch(goth_task_t *previous, goth_task_t *next)
{
    goth_um_linux_context_t *previous_context;
    ucontext_t *source;
    ucontext_t *destination;

    if (!um_running || !host_context_ready ||
        goth_um_linux_thread_id() != host_thread_id) {
        return;
    }

    previous_context = previous != NULL
                           ? (goth_um_linux_context_t *)previous->port_context
                           : NULL;
    source = previous_context != NULL ? &previous_context->context
                                      : &host_context;

    if (next == NULL) {
        destination = &host_context;
    } else if (next->port_context != NULL) {
        destination =
            &((goth_um_linux_context_t *)next->port_context)->context;
    } else {
        return;
    }

    if (destination == source) {
        return;
    }
    (void)swapcontext(source, destination);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ms = goth_um_linux_now_ms();
}

uint32_t goth_um_collect_ticks(void)
{
    uint64_t now;
    uint64_t elapsed;
    uint64_t ticks;

    if (!um_running || um_tick_period_ms == 0U) {
        return 0U;
    }

    now = goth_um_linux_now_ms();
    if (now < um_last_tick_ms) {
        um_last_tick_ms = now;
        return 0U;
    }

    elapsed = now - um_last_tick_ms;
    ticks = elapsed / um_tick_period_ms;
    um_last_tick_ms += ticks * um_tick_period_ms;
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
            if (!goth_kernel_has_sleeping_tasks()) {
                break;
            }
            goth_um_linux_sleep_ms(tick_period_ms);
            {
                uint32_t elapsed_ticks = goth_um_collect_ticks();

                if (elapsed_ticks == 0U) {
                    elapsed_ticks = 1U;
                    um_last_tick_ms = goth_um_linux_now_ms();
                }
                while (elapsed_ticks > 0U) {
                    elapsed_ticks--;
                    (void)goth_kernel_tick();
                }
            }
        }
    }
    um_running = false;
    return GOTH_STATUS_OK;
}

static goth_pe_status_t goth_pe_linux_initialize(goth_pe_info_t *info)
{
    long cpu_count;

    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }

    cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    info->platform_name = "Linux user mode";
    info->logical_cpu_count = cpu_count > 0 ? (uint32_t)cpu_count : 1U;

#if defined(__x86_64__) || defined(__amd64__)
    info->architecture = "x86_64";
    info->pointer_width = 64U;
#elif defined(__i386__) || defined(__i686__)
    info->architecture = "x86";
    info->pointer_width = 32U;
#elif defined(__aarch64__)
    info->architecture = "aarch64";
    info->pointer_width = 64U;
#elif defined(__arm__)
    info->architecture = "arm";
    info->pointer_width = 32U;
#elif defined(__riscv) && __riscv_xlen == 64
    info->architecture = "riscv64";
    info->pointer_width = 64U;
#elif defined(__riscv)
    info->architecture = "riscv";
    info->pointer_width = 32U;
#else
    info->architecture = "unknown";
    info->pointer_width = (uint8_t)(sizeof(void *) * CHAR_BIT);
#endif

    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t goth_pe_linux_time_ns(uint64_t *time_ns)
{
    struct timespec now;
    uint64_t seconds;
    uint64_t nanoseconds;

    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    seconds = (uint64_t)now.tv_sec;
    nanoseconds = (uint64_t)now.tv_nsec;
    if (seconds > (UINT64_MAX - nanoseconds) / UINT64_C(1000000000)) {
        return GOTH_PE_STATUS_TIME_RANGE;
    }

    *time_ns = seconds * UINT64_C(1000000000) + nanoseconds;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t goth_pe_linux_console_write(const char *text,
                                                    size_t length)
{
    size_t written_total = 0U;

    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }

    while (written_total < length) {
        size_t remaining = length - written_total;
        size_t chunk = remaining > (size_t)SSIZE_MAX
                           ? (size_t)SSIZE_MAX
                           : remaining;
        ssize_t written = write(STDOUT_FILENO, text + written_total, chunk);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return GOTH_PE_STATUS_PLATFORM_ERROR;
        }
        if (written == 0) {
            return GOTH_PE_STATUS_PLATFORM_ERROR;
        }
        written_total += (size_t)written;
    }

    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        goth_pe_linux_initialize,
        goth_pe_linux_time_ns,
        goth_pe_linux_console_write
    };

    return &platform_ops;
}
