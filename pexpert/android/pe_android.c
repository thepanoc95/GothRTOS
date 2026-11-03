/*
 * pexpert/android/pe_android.c -- GothRTOS platform expert for Android.
 *
 * Android's C library (bionic) provides POSIX threads but not the
 * makecontext/swapcontext family, so the scheduler contexts used by the Linux
 * user-mode port (pexpert/um/pe_linux.c) cannot be used here.  This port
 * implements the same um_internal.h contract with one pthread per GothRTOS
 * task and a single "token": only the context named by scheduler_running may
 * execute, so kernel state is still only touched by one thread at a time.
 *
 * The Android process runs on Android's Linux kernel; GothRTOS itself is the
 * scheduler and IPC layer inside that process.  Framebuffer and touch I/O are
 * userspace policy and stay in libgothgfx.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "pe_internal.h"
#include "um_internal.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pthread_t thread;
    goth_task_t *task;
    bool thread_created;
    bool exit_requested;
    bool finished;
} goth_android_context_t;

/* Only the context stored here may run.  NULL means the host thread (the one
 * that called goth_um_run) is running. */
static pthread_mutex_t scheduler_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scheduler_cond = PTHREAD_COND_INITIALIZER;
static goth_android_context_t *scheduler_running;

static pthread_t host_thread;
static bool host_thread_known;
static bool um_running;
static uint32_t um_tick_period_ms;
static uint64_t um_last_tick_ms;

/* NULL on the host thread, the task context on task threads. */
static __thread goth_android_context_t *current_context;

/* Lua needs a healthy stack, and the kernel's own 8 KiB boot stack is far too
 * small for a pthread.  Callers may ask for more. */
#define GOTH_ANDROID_MIN_STACK_SIZE (512U * 1024U)
#define GOTH_ANDROID_PAGE_SIZE 4096U

static uint64_t goth_android_now_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0U;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)(now.tv_nsec / UINT64_C(1000000));
}

static void goth_android_sleep_ms(uint32_t milliseconds)
{
    struct timespec delay;

    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static size_t goth_android_stack_size(size_t requested)
{
    size_t size = requested < GOTH_ANDROID_MIN_STACK_SIZE
                      ? GOTH_ANDROID_MIN_STACK_SIZE
                      : requested;

    size = (size + (GOTH_ANDROID_PAGE_SIZE - 1U)) &
           ~((size_t)GOTH_ANDROID_PAGE_SIZE - 1U);
    return size;
}

static void *goth_android_task_thread(void *argument)
{
    goth_android_context_t *context = (goth_android_context_t *)argument;

    current_context = context;

    pthread_mutex_lock(&scheduler_lock);
    while (scheduler_running != context && !context->exit_requested) {
        pthread_cond_wait(&scheduler_cond, &scheduler_lock);
    }
    if (context->exit_requested) {
        pthread_mutex_unlock(&scheduler_lock);
        return NULL;
    }
    pthread_mutex_unlock(&scheduler_lock);

    context->task->entry(context->task->argument);

    /* A task normally terminates itself through goth_task_exit(), which never
     * returns.  If the entry point does return, hand control back to the host
     * so the run loop can continue. */
    pthread_mutex_lock(&scheduler_lock);
    context->finished = true;
    if (scheduler_running == context) {
        scheduler_running = NULL;
    }
    pthread_cond_broadcast(&scheduler_cond);
    pthread_mutex_unlock(&scheduler_lock);
    return NULL;
}

/* Waits until the token names this context.  A context that is being torn
 * down is woken through exit_requested by goth_um_task_destroy(); terminated
 * tasks never reach this function because goth_um_context_switch() exits
 * their pthread directly. */
static void goth_android_wait_for_turn(goth_android_context_t *context)
{
    while (scheduler_running != context) {
        if (context->exit_requested) {
            context->finished = true;
            pthread_mutex_unlock(&scheduler_lock);
            pthread_exit(NULL);
        }
        pthread_cond_wait(&scheduler_cond, &scheduler_lock);
    }
}

static void goth_android_release_context(goth_task_t *task)
{
    goth_android_context_t *context;

    if (task == NULL || task->port_context == NULL) {
        return;
    }

    context = (goth_android_context_t *)task->port_context;
    pthread_mutex_lock(&scheduler_lock);
    context->exit_requested = true;
    pthread_cond_broadcast(&scheduler_cond);
    pthread_mutex_unlock(&scheduler_lock);

    if (context->thread_created &&
        !pthread_equal(pthread_self(), context->thread)) {
        (void)pthread_join(context->thread, NULL);
    }
    free(context);
    task->port_context = NULL;
}

goth_status_t goth_um_platform_init(void)
{
    if (host_thread_known) {
        return pthread_equal(pthread_self(), host_thread)
                   ? GOTH_STATUS_OK
                   : GOTH_STATUS_BAD_STATE;
    }

    host_thread = pthread_self();
    host_thread_known = true;
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_platform_reset(goth_task_t *tasks)
{
    goth_task_t *task;

    if (um_running) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (host_thread_known &&
        !pthread_equal(pthread_self(), host_thread)) {
        return GOTH_STATUS_BAD_STATE;
    }

    for (task = tasks; task != NULL; task = task->next) {
        goth_android_release_context(task);
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_create(goth_task_t *task)
{
    goth_android_context_t *context;
    pthread_attr_t attributes;
    int result;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (!host_thread_known ||
        !pthread_equal(pthread_self(), host_thread)) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }

    context = (goth_android_context_t *)calloc(1U, sizeof(*context));
    if (context == NULL) {
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    context->task = task;

    if (pthread_attr_init(&attributes) != 0) {
        free(context);
        return GOTH_STATUS_PLATFORM_ERROR;
    }
    (void)pthread_attr_setstacksize(&attributes,
                                    goth_android_stack_size(task->stack_size));
    result = pthread_create(&context->thread, &attributes,
                            goth_android_task_thread, context);
    (void)pthread_attr_destroy(&attributes);
    if (result != 0) {
        free(context);
        return GOTH_STATUS_PLATFORM_ERROR;
    }

    context->thread_created = true;
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
    if (!host_thread_known ||
        !pthread_equal(pthread_self(), host_thread)) {
        return GOTH_STATUS_BAD_STATE;
    }

    goth_android_release_context(task);
    return GOTH_STATUS_OK;
}

void goth_um_context_switch(goth_task_t *previous, goth_task_t *next)
{
    goth_android_context_t *self = current_context;
    goth_android_context_t *target =
        next != NULL ? (goth_android_context_t *)next->port_context : NULL;
    bool self_terminated =
        self != NULL && previous == self->task &&
        previous->state == GOTH_TASK_TERMINATED;

    if (!um_running || !host_thread_known) {
        return;
    }
    if (target == self) {
        return;
    }

    pthread_mutex_lock(&scheduler_lock);
    scheduler_running = target;
    pthread_cond_broadcast(&scheduler_cond);

    if (self == NULL) {
        /* Host thread: sleep until a task hands control back (next == NULL)
         * or switches to another task that eventually does. */
        while (scheduler_running != NULL) {
            pthread_cond_wait(&scheduler_cond, &scheduler_lock);
        }
        pthread_mutex_unlock(&scheduler_lock);
        return;
    }

    if (self_terminated) {
        /* goth_task_exit() marked this task TERMINATED and asked for the next
         * one.  Hand over the token and leave the pthread; the host joins it
         * in goth_um_task_destroy(). */
        self->finished = true;
        pthread_mutex_unlock(&scheduler_lock);
        pthread_exit(NULL);
    }

    goth_android_wait_for_turn(self);
    pthread_mutex_unlock(&scheduler_lock);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ms = goth_android_now_ms();
}

uint32_t goth_um_collect_ticks(void)
{
    uint64_t now;
    uint64_t elapsed;
    uint64_t ticks;

    if (!um_running || um_tick_period_ms == 0U) {
        return 0U;
    }

    now = goth_android_now_ms();
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
            goth_android_sleep_ms(tick_period_ms);
            {
                uint32_t elapsed_ticks = goth_um_collect_ticks();

                if (elapsed_ticks == 0U) {
                    elapsed_ticks = 1U;
                    um_last_tick_ms = goth_android_now_ms();
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

static goth_pe_status_t goth_pe_android_initialize(goth_pe_info_t *info)
{
    long cpu_count;

    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }

    cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    info->platform_name = "Android user mode";
    info->logical_cpu_count = cpu_count > 0 ? (uint32_t)cpu_count : 1U;

#if defined(__aarch64__)
    info->architecture = "aarch64";
    info->pointer_width = 64U;
#elif defined(__arm__)
    info->architecture = "arm";
    info->pointer_width = 32U;
#elif defined(__x86_64__) || defined(__amd64__)
    info->architecture = "x86_64";
    info->pointer_width = 64U;
#elif defined(__i386__) || defined(__i686__)
    info->architecture = "x86";
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

static goth_pe_status_t goth_pe_android_time_ns(uint64_t *time_ns)
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

/* bionic does not define SSIZE_MAX. */
#define GOTH_ANDROID_WRITE_MAX ((size_t)(~(size_t)0 >> 1))

static goth_pe_status_t goth_pe_android_console_write(const char *text,
                                                      size_t length)
{
    size_t written_total = 0U;

    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }

    while (written_total < length) {
        size_t remaining = length - written_total;
        size_t chunk = remaining > GOTH_ANDROID_WRITE_MAX
                           ? GOTH_ANDROID_WRITE_MAX
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

static goth_pe_status_t goth_pe_android_console_read(char *buffer,
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
        goth_pe_android_initialize,
        goth_pe_android_time_ns,
        goth_pe_android_console_write,
        goth_pe_android_console_read
    };

    return &platform_ops;
}
