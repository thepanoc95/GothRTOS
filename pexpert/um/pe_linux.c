#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pe_internal.h"
#include "um_internal.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

typedef struct {
    ucontext_t context;
    void *mapping;
    void *stack;
    size_t stack_size;
    bool guard_page;
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

static void goth_um_linux_preempt_block(void);
static void goth_um_linux_preempt_unblock(void);

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
    goth_um_linux_preempt_unblock();
    task->entry(task->argument);
    goth_um_linux_preempt_block();
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
    free(context->mapping);
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

/* Preemptive preemption for user mode.
 *
 * The kernel exposes goth_kernel_tick() as the only scheduling entry point a
 * platform timer needs. On Linux we drive it from an interval timer. The
 * handler refuses to run while the scheduler is manipulating its own state;
 * a lost tick is harmless because the next one is only a millisecond away.
 * The timer is stopped whenever the run loop parks because no task is ready,
 * which keeps the host's saved context valid. */
static volatile sig_atomic_t preempt_timer_active;

static void goth_um_linux_preempt_block(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    (void)sigprocmask(SIG_BLOCK, &mask, NULL);
}

static void goth_um_linux_preempt_unblock(void)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    (void)sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

static void goth_um_linux_preempt_handler(int signal_number)
{
    (void)signal_number;

    if (!um_running || !preempt_timer_active ||
        !goth_kernel_async_preempt_enabled() ||
        !goth_kernel_preempt_allowed()) {
        return;
    }

    (void)goth_kernel_tick();
}

static goth_status_t goth_um_linux_preempt_start(uint32_t period_ms)
{
    struct sigaction action = {0};
    struct itimerval timer = {{0, 0}, {0, 0}};
    long milliseconds = period_ms > 0U ? (long)period_ms : 1L;

    action.sa_handler = goth_um_linux_preempt_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &action, NULL) != 0) {
        return GOTH_STATUS_PLATFORM_ERROR;
    }

    timer.it_interval.tv_sec = milliseconds / 1000L;
    timer.it_interval.tv_usec = (milliseconds % 1000L) * 1000L;
    timer.it_value = timer.it_interval;
    preempt_timer_active = 1;
    if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
        preempt_timer_active = 0;
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    return GOTH_STATUS_OK;
}

static void goth_um_linux_preempt_stop(void)
{
    struct itimerval timer = {{0, 0}, {0, 0}};

    if (!preempt_timer_active) {
        return;
    }
    preempt_timer_active = 0;
    (void)setitimer(ITIMER_REAL, &timer, NULL);
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

static size_t goth_um_linux_page_size(void)
{
    long page = sysconf(_SC_PAGESIZE);

    return page > 0 ? (size_t)page : 4096U;
}

goth_status_t goth_um_task_create(goth_task_t *task)
{
    goth_um_linux_context_t *context;
    size_t stack_size;
    size_t page_size;

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

    /* Put a PROT_NONE page below the host stack. An overflow then faults in
     * the host process instead of silently corrupting its neighbour. If the
     * alignment or mprotect call is unavailable, fall back to a plain heap
     * stack; the canary still catches the low-water mark. */
    page_size = goth_um_linux_page_size();
    stack_size = (stack_size + page_size - 1U) & ~(page_size - 1U);
    context->mapping = NULL;
    if (posix_memalign(&context->mapping, page_size,
                       stack_size + page_size) == 0) {
        if (mprotect(context->mapping, page_size, PROT_NONE) == 0) {
            context->stack = (uint8_t *)context->mapping + page_size;
            context->guard_page = true;
        } else {
            free(context->mapping);
            context->mapping = NULL;
        }
    }
    if (context->mapping == NULL) {
        context->mapping = malloc(stack_size);
        if (context->mapping == NULL) {
            free(context);
            return GOTH_STATUS_PLATFORM_ERROR;
        }
        context->stack = context->mapping;
        context->guard_page = false;
    }
    context->stack_size = stack_size;

    if (getcontext(&context->context) != 0) {
        free(context->mapping);
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

goth_status_t goth_um_task_guard(goth_task_t *task)
{
    goth_um_linux_context_t *context;
    uint32_t *words;

    if (task == NULL || task->port_context == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    context = (goth_um_linux_context_t *)task->port_context;
    if (context->stack == NULL || context->stack_size < 8U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    words = (uint32_t *)context->stack;
    words[0] = GOTH_UM_GUARD_WORD;
    words[1] = GOTH_UM_GUARD_WORD ^ GOTH_UM_GUARD_XOR;
    return GOTH_STATUS_OK;
}

bool goth_um_task_guard_valid(goth_task_t *task)
{
    goth_um_linux_context_t *context;
    const uint32_t *words;

    if (task == NULL || task->port_context == NULL) {
        return false;
    }
    context = (goth_um_linux_context_t *)task->port_context;
    if (context->stack == NULL || context->stack_size < 8U) {
        return false;
    }
    words = (const uint32_t *)context->stack;
    return words[0] == GOTH_UM_GUARD_WORD &&
           words[1] == (GOTH_UM_GUARD_WORD ^ GOTH_UM_GUARD_XOR);
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
    goth_um_linux_preempt_block();
    (void)swapcontext(source, destination);
    goth_um_linux_preempt_unblock();
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
    goth_um_linux_preempt_block();
    goth_um_start_clock(tick_period_ms);
    if (goth_kernel_async_preempt_enabled()) {
        (void)goth_um_linux_preempt_start(tick_period_ms);
    }
    for (;;) {
        if (goth_kernel_schedule() == NULL) {
            goth_um_linux_preempt_stop();
            goth_um_linux_preempt_block();
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
            if (goth_kernel_async_preempt_enabled()) {
                (void)goth_um_linux_preempt_start(tick_period_ms);
            }
        }
    }
    goth_um_linux_preempt_stop();
    goth_um_linux_preempt_unblock();
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

static goth_pe_status_t goth_pe_linux_console_read(char *buffer,
                                                   size_t capacity,
                                                   size_t *read_length)
{
    ssize_t read_result;

    if (buffer == NULL || read_length == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    *read_length = 0U;
    if (capacity == 0U) {
        return GOTH_PE_STATUS_OK;
    }

    for (;;) {
        read_result = read(STDIN_FILENO, buffer, capacity);
        if (read_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return GOTH_PE_STATUS_PLATFORM_ERROR;
        }
        break;
    }

    *read_length = (size_t)read_result;
    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        goth_pe_linux_initialize,
        goth_pe_linux_time_ns,
        goth_pe_linux_console_write,
        goth_pe_linux_console_read,
        NULL
    };

    return &platform_ops;
}
