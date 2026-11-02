#include "pe_internal.h"
#include "um_internal.h"

#include <windows.h>

#include <limits.h>

static LARGE_INTEGER counter_frequency;
static DWORD host_thread_id;
static LPVOID host_fiber;
static bool um_running;
static uint32_t um_tick_period_ms;
static ULONGLONG um_last_tick_ms;

static VOID CALLBACK goth_um_fiber_entry(void *argument)
{
    goth_task_t *task = (goth_task_t *)argument;

    task->entry(task->argument);
    (void)goth_task_exit();

    for (;;) {
        SwitchToFiber(host_fiber);
    }
}

goth_status_t goth_um_platform_init(void)
{
    if (host_fiber != NULL) {
        return GetCurrentThreadId() == host_thread_id
                   ? GOTH_STATUS_OK
                   : GOTH_STATUS_BAD_STATE;
    }

    host_thread_id = GetCurrentThreadId();
    if (IsThreadAFiber()) {
        host_fiber = GetCurrentFiber();
    } else {
        host_fiber = ConvertThreadToFiber(NULL);
        if (host_fiber == NULL) {
            host_thread_id = 0U;
            return GOTH_STATUS_PLATFORM_ERROR;
        }
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_platform_reset(goth_task_t *tasks)
{
    goth_task_t *task;

    if (um_running) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (host_fiber != NULL && GetCurrentThreadId() != host_thread_id) {
        return GOTH_STATUS_BAD_STATE;
    }

    for (task = tasks; task != NULL; task = task->next) {
        if (task->port_context != NULL) {
            DeleteFiber(task->port_context);
            task->port_context = NULL;
        }
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_um_task_create(goth_task_t *task)
{
    SIZE_T stack_reserve;

    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (host_fiber == NULL || GetCurrentThreadId() != host_thread_id) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    stack_reserve = (SIZE_T)task->stack_size;
    if (stack_reserve < 64U * 1024U) {
        stack_reserve = 64U * 1024U;
    }
    task->port_context = CreateFiberEx(0U, stack_reserve,
                                       FIBER_FLAG_FLOAT_SWITCH,
                                       goth_um_fiber_entry, task);
    return task->port_context != NULL
               ? GOTH_STATUS_OK
               : GOTH_STATUS_PLATFORM_ERROR;
}

goth_status_t goth_um_task_destroy(goth_task_t *task)
{
    if (task == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (task->port_context == NULL) {
        return GOTH_STATUS_OK;
    }
    if (GetCurrentThreadId() != host_thread_id) {
        return GOTH_STATUS_BAD_STATE;
    }

    if (task->port_context != NULL) {
        DeleteFiber(task->port_context);
        task->port_context = NULL;
    }
    return GOTH_STATUS_OK;
}

void goth_um_context_switch(goth_task_t *previous, goth_task_t *next)
{
    LPVOID destination;

    if (!um_running || host_fiber == NULL ||
        GetCurrentThreadId() != host_thread_id) {
        return;
    }

    destination = next != NULL ? next->port_context : host_fiber;
    if (destination == NULL || destination == GetCurrentFiber()) {
        return;
    }
    (void)previous;
    SwitchToFiber(destination);
}

bool goth_um_is_running(void)
{
    return um_running;
}

void goth_um_start_clock(uint32_t tick_period_ms)
{
    um_tick_period_ms = tick_period_ms;
    um_last_tick_ms = GetTickCount64();
}

uint32_t goth_um_collect_ticks(void)
{
    ULONGLONG now;
    ULONGLONG elapsed;
    ULONGLONG ticks;

    if (!um_running || um_tick_period_ms == 0U) {
        return 0U;
    }

    now = GetTickCount64();
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
            Sleep(tick_period_ms);
            {
                uint32_t elapsed_ticks = goth_um_collect_ticks();

                if (elapsed_ticks == 0U) {
                    elapsed_ticks = 1U;
                    um_last_tick_ms = GetTickCount64();
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

static goth_pe_status_t goth_pe_w64_initialize(goth_pe_info_t *info)
{
    SYSTEM_INFO system_info;

    if (info == NULL || !QueryPerformanceFrequency(&counter_frequency) ||
        counter_frequency.QuadPart <= 0) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    GetSystemInfo(&system_info);
    info->platform_name = "Windows user mode";
    info->logical_cpu_count = system_info.dwNumberOfProcessors;

#if defined(_M_X64) || defined(__x86_64__)
    info->architecture = "x86_64";
    info->pointer_width = 64U;
#elif defined(_M_IX86) || defined(__i386__)
    info->architecture = "x86";
    info->pointer_width = 32U;
#elif defined(_M_ARM64) || defined(__aarch64__)
    info->architecture = "aarch64";
    info->pointer_width = 64U;
#elif defined(_M_ARM) || defined(__arm__)
    info->architecture = "arm";
    info->pointer_width = 32U;
#else
    info->architecture = "unknown";
    info->pointer_width = (uint8_t)(sizeof(void *) * CHAR_BIT);
#endif

    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t goth_pe_w64_time_ns(uint64_t *time_ns)
{
    LARGE_INTEGER counter;
    uint64_t ticks;
    uint64_t frequency;
    uint64_t seconds;
    uint64_t remainder;

    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (!QueryPerformanceCounter(&counter) || counter.QuadPart < 0 ||
        counter_frequency.QuadPart <= 0) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    ticks = (uint64_t)counter.QuadPart;
    frequency = (uint64_t)counter_frequency.QuadPart;
    seconds = ticks / frequency;
    remainder = ticks % frequency;

    if (seconds > UINT64_MAX / UINT64_C(1000000000) ||
        remainder > UINT64_MAX / UINT64_C(1000000000)) {
        return GOTH_PE_STATUS_TIME_RANGE;
    }

    *time_ns = seconds * UINT64_C(1000000000) +
               (remainder * UINT64_C(1000000000)) / frequency;
    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t goth_pe_w64_console_write(const char *text,
                                                   size_t length)
{
    HANDLE output;
    size_t written_total = 0U;

    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }

    output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == NULL || output == INVALID_HANDLE_VALUE) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    while (written_total < length) {
        size_t remaining = length - written_total;
        DWORD chunk = remaining > (size_t)MAXDWORD
                          ? MAXDWORD
                          : (DWORD)remaining;
        DWORD written = 0U;

        if (!WriteFile(output, text + written_total, chunk, &written, NULL) ||
            written == 0U) {
            return GOTH_PE_STATUS_PLATFORM_ERROR;
        }
        written_total += written;
    }

    return GOTH_PE_STATUS_OK;
}

static goth_pe_status_t goth_pe_w64_console_read(char *buffer,
                                                 size_t capacity,
                                                 size_t *read_length)
{
    HANDLE input;
    DWORD chunk;
    DWORD read_count = 0U;

    if (buffer == NULL || read_length == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    *read_length = 0U;
    if (capacity == 0U) {
        return GOTH_PE_STATUS_OK;
    }

    input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == NULL || input == INVALID_HANDLE_VALUE) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    chunk = capacity > (size_t)MAXDWORD ? MAXDWORD : (DWORD)capacity;
    if (!ReadFile(input, buffer, chunk, &read_count, NULL)) {
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    *read_length = (size_t)read_count;
    return GOTH_PE_STATUS_OK;
}

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void)
{
    static const goth_pe_platform_ops_t platform_ops = {
        goth_pe_w64_initialize,
        goth_pe_w64_time_ns,
        goth_pe_w64_console_write,
        goth_pe_w64_console_read
    };

    return &platform_ops;
}