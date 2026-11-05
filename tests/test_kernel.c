#include "gothrtos/kernel.h"
#include "gothrtos/kapi.h"
#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/syscall.h"
#include "gothrtos/um.h"
#include "gothrtos/vfs.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint8_t stack_a[256];
static uint8_t stack_b[256];
static uint8_t stack_c[256];
static goth_task_t task_a = {0};
static goth_task_t task_b = {0};
static goth_task_t task_c = {0};
static uint8_t syscall_stack_a[256];
static uint8_t syscall_stack_b[256];
static goth_task_t syscall_task_a = {0};
static goth_task_t syscall_task_b = {0};
static uint8_t um_stack_a[256];
static uint8_t um_stack_b[256];
static goth_task_t um_task_a = {0};
static goth_task_t um_task_b = {0};
static uint32_t um_task_log[4];
static size_t um_task_log_count;
static uint32_t um_sleep_log[2];
static size_t um_sleep_log_count;
static uint8_t kapi_stack[256];
static goth_task_t kapi_task = {0};
static uint32_t kapi_program_calls;
static uint8_t guarded_buffer[64];
static char vfs_buffer[700];

static void kapi_program_entry(void *argument)
{
    uint32_t *calls = (uint32_t *)argument;

    (*calls)++;
}


static void empty_task(void *argument)
{
    (void)argument;
}

static void um_task_a_entry(void *argument)
{
    (void)argument;
    um_task_log[um_task_log_count++] = 1U;
    (void)goth_task_yield();
    um_task_log[um_task_log_count++] = 3U;
}

static void um_task_b_entry(void *argument)
{
    (void)argument;
    um_task_log[um_task_log_count++] = 2U;
    (void)goth_task_yield();
    um_task_log[um_task_log_count++] = 4U;
}

static void um_sleep_entry(void *argument)
{
    (void)argument;
    um_sleep_log[um_sleep_log_count++] = 1U;
    (void)goth_task_sleep(2U);
    um_sleep_log[um_sleep_log_count++] = 2U;
}

static void test_scheduler(void)
{
    goth_task_t *selected;

    goth_kernel_init();
    assert(goth_task_resume(&task_a) == GOTH_STATUS_BAD_STATE);
    assert(goth_task_create(&task_a, stack_a, sizeof(stack_a), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_task_create(&task_b, stack_b, sizeof(stack_b), 2U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_task_create(&task_c, stack_c, sizeof(stack_c), 2U,
                            empty_task, NULL) == GOTH_STATUS_OK);

    selected = goth_kernel_schedule();
    assert(selected == &task_b);
    assert(goth_kernel_current_task() == &task_b);
    assert(goth_task_yield() == &task_c);
    assert(goth_task_sleep(2U) == &task_b);

    selected = goth_kernel_tick();
    assert(selected == &task_b);
    selected = goth_kernel_tick();
    assert(selected == &task_c);
    assert(goth_kernel_ticks() == 2U);

    assert(goth_task_suspend(&task_b) == GOTH_STATUS_OK);
    assert(goth_task_resume(&task_b) == GOTH_STATUS_OK);
    assert(goth_task_exit() == &task_b);
    assert(task_c.state == GOTH_TASK_TERMINATED);
    assert(goth_task_destroy(&task_c) == GOTH_STATUS_OK);
    assert(task_c.state == GOTH_TASK_UNUSED);
}

static void test_scheduler_modes(void)
{
    goth_kernel_init();
    assert(goth_kernel_get_scheduler() == GOTH_SCHED_PREEMPTIVE);
    assert(goth_kernel_is_preemptive());
    assert(goth_kernel_quantum() == 1U);
    assert(!goth_kernel_async_preempt_enabled());
    assert(goth_kernel_preempt_allowed());

    assert(goth_kernel_set_quantum(0U) == GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_kernel_set_quantum(3U) == GOTH_STATUS_OK);
    assert(goth_kernel_quantum() == 3U);
    assert(goth_kernel_set_scheduler((goth_sched_policy_t)9) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    assert(goth_task_create(&task_a, stack_a, sizeof(stack_a), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_task_create(&task_b, stack_b, sizeof(stack_b), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);

    assert(goth_kernel_schedule() == &task_a);
    assert(goth_kernel_task_count() == 2U);
    assert(goth_kernel_find_task(task_a.id) == &task_a);
    assert(goth_kernel_find_task(999U) == NULL);

    /* The three-tick quantum keeps the current task for the first two
       ticks and rotates to the peer on the third. */
    assert(goth_kernel_tick() == &task_a);
    assert(goth_kernel_tick() == &task_a);
    assert(goth_kernel_tick() == &task_b);

    /* Cooperative mode never moves the CPU on a tick. */
    assert(goth_kernel_set_scheduler(GOTH_SCHED_COOPERATIVE) ==
           GOTH_STATUS_OK);
    assert(!goth_kernel_is_preemptive());
    assert(goth_kernel_tick() == &task_b);
    assert(goth_kernel_current_task() == &task_b);

    /* A forced preemption still works in cooperative mode. */
    assert(goth_kernel_preempt() == &task_a);

    goth_kernel_set_preemptive(true);
    assert(goth_kernel_get_scheduler() == GOTH_SCHED_PREEMPTIVE);
    assert(goth_kernel_async_preempt_enabled());
    goth_kernel_set_preemptive(false);
    assert(!goth_kernel_async_preempt_enabled());
}

static void test_scheduler_syscalls(void)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;

    goth_kernel_init();
    assert(goth_task_create(&syscall_task_a, syscall_stack_a,
                            sizeof(syscall_stack_a), 1U, empty_task, NULL) ==
           GOTH_STATUS_OK);
    assert(goth_kernel_schedule() == &syscall_task_a);

    request.number = GOTH_SYSCALL_TASK_SELF;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == syscall_task_a.id);

    request.number = GOTH_SYSCALL_KERNEL_GET_SCHED;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == GOTH_SCHED_PREEMPTIVE);

    request.number = GOTH_SYSCALL_KERNEL_SET_SCHED;
    request.arguments[0] = GOTH_SCHED_COOPERATIVE;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == GOTH_SCHED_COOPERATIVE);

    request.number = GOTH_SYSCALL_KERNEL_SET_QUANTUM;
    request.arguments[0] = 4U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == 4U);

    request.number = GOTH_SYSCALL_KERNEL_GET_QUANTUM;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == 4U);

    request.number = GOTH_SYSCALL_KERNEL_TASK_COUNT;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == 1U);

    request.number = GOTH_SYSCALL_KERNEL_SET_QUANTUM;
    request.arguments[0] = 0U;
    assert(goth_syscall_dispatch(&request).status ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_endpoints(void)
{
    goth_endpoint_t endpoint = {0};
    goth_message_t storage[2];
    goth_message_t received;

    goth_kernel_init();
    assert(goth_endpoint_init(&endpoint, storage, 2U) == GOTH_STATUS_OK);
    assert(goth_endpoint_receive(&endpoint, &received) == GOTH_STATUS_EMPTY);
    assert(goth_endpoint_send(&endpoint, 7U, 11U, 13U) == GOTH_STATUS_OK);
    assert(goth_endpoint_send(&endpoint, 8U, 17U, 19U) == GOTH_STATUS_OK);
    assert(goth_endpoint_send(&endpoint, 9U, 23U, 29U) == GOTH_STATUS_FULL);

    assert(goth_endpoint_receive(&endpoint, &received) == GOTH_STATUS_OK);
    assert(received.sender == 0U);
    assert(received.type == 7U);
    assert(received.value[0] == 11U);
    assert(received.value[1] == 13U);
    assert(goth_endpoint_send(&endpoint, 9U, 23U, 29U) == GOTH_STATUS_OK);
    assert(goth_endpoint_receive(&endpoint, &received) == GOTH_STATUS_OK);
    assert(received.type == 8U);
    assert(goth_endpoint_receive(&endpoint, &received) == GOTH_STATUS_OK);
    assert(received.type == 9U);
}

static void test_platform_expert(void)
{
    goth_pe_info_t info;
    uint64_t first_time;
    uint64_t second_time;

    assert(goth_pe_get_info(&info) == GOTH_PE_STATUS_NOT_INITIALIZED);
    assert(goth_pe_init() == GOTH_PE_STATUS_OK);
    assert(goth_pe_get_info(&info) == GOTH_PE_STATUS_OK);
    assert(info.platform_name != NULL);
    assert(info.architecture != NULL);
    assert(info.logical_cpu_count > 0U);
    assert(info.pointer_width == 32U || info.pointer_width == 64U);
    assert(goth_pe_time_ns(&first_time) == GOTH_PE_STATUS_OK);
    assert(goth_pe_time_ns(&second_time) == GOTH_PE_STATUS_OK);
    assert(second_time >= first_time);
}

static void test_syscalls(void)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;
    goth_endpoint_t endpoint = {0};
    goth_message_t storage[2];
    goth_message_t received;

    goth_kernel_init();
    assert(goth_task_create(&syscall_task_a, syscall_stack_a,
                            sizeof(syscall_stack_a), 2U, empty_task, NULL) ==
           GOTH_STATUS_OK);
    assert(goth_task_create(&syscall_task_b, syscall_stack_b,
                            sizeof(syscall_stack_b), 2U, empty_task, NULL) ==
           GOTH_STATUS_OK);
    assert(goth_kernel_schedule() == &syscall_task_a);

    request.number = GOTH_SYSCALL_TASK_YIELD;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == syscall_task_b.id);

    request.number = GOTH_SYSCALL_TASK_SLEEP;
    request.arguments[0] = 3U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == syscall_task_a.id);

    request.number = GOTH_SYSCALL_KERNEL_TICKS;
    request.arguments[0] = 0U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == 0U);

    assert(goth_endpoint_init(&endpoint, storage, 2U) == GOTH_STATUS_OK);
    request.number = GOTH_SYSCALL_ENDPOINT_SEND;
    request.arguments[0] = (uintptr_t)&endpoint;
    request.arguments[1] = 42U;
    request.arguments[2] = 7U;
    request.arguments[3] = 9U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);

    request.number = GOTH_SYSCALL_ENDPOINT_RECEIVE;
    request.arguments[1] = (uintptr_t)&received;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(received.sender == syscall_task_a.id);
    assert(received.type == 42U);
    assert(received.value[0] == 7U);
    assert(received.value[1] == 9U);

    assert(goth_kernel_tick() == &syscall_task_a);
    assert(goth_kernel_tick() == &syscall_task_a);
    assert(goth_kernel_tick() == &syscall_task_b);
    request.number = UINT32_MAX;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_INVALID_SYSCALL);
    assert(result.value == 0U);
    assert(goth_syscall_dispatch(NULL).status == GOTH_STATUS_INVALID_ARGUMENT);

    request.number = GOTH_SYSCALL_TASK_EXIT;
    request.arguments[0] = 0U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == syscall_task_a.id);
}

static void test_um_fibers(void)
{
    goth_kernel_init();
    um_task_log_count = 0U;
    assert(goth_task_create(&um_task_a, um_stack_a, sizeof(um_stack_a), 1U,
                            um_task_a_entry, NULL) == GOTH_STATUS_OK);
    assert(goth_task_create(&um_task_b, um_stack_b, sizeof(um_stack_b), 1U,
                            um_task_b_entry, NULL) == GOTH_STATUS_OK);
    assert(goth_um_run(1U) == GOTH_STATUS_OK);
    assert(um_task_log_count == 4U);
    assert(um_task_log[0] == 1U);
    assert(um_task_log[1] == 2U);
    assert(um_task_log[2] == 3U);
    assert(um_task_log[3] == 4U);

    goth_kernel_init();
    um_sleep_log_count = 0U;
    assert(goth_task_create(&task_a, stack_a, sizeof(stack_a), 1U,
                            um_sleep_entry, NULL) == GOTH_STATUS_OK);
    assert(goth_um_run(1U) == GOTH_STATUS_OK);
    assert(um_sleep_log_count == 2U);
    assert(um_sleep_log[0] == 1U);
    assert(um_sleep_log[1] == 2U);
}

static void test_kapi(void)
{
    goth_kapi_program_t program = {
        "kapi-test",
        &kapi_task,
        kapi_stack,
        sizeof(kapi_stack),
        1U,
        kapi_program_entry,
        &kapi_program_calls
    };
    goth_endpoint_t endpoint = {0};
    goth_message_t storage[1];
    goth_message_t received;
    goth_task_id_t next_task;
    uint64_t ticks;
    size_t started_count;

    goth_kernel_init();
    assert(goth_kapi_get_ticks(&ticks) == GOTH_STATUS_OK);
    assert(ticks == 0U);
    assert(goth_kapi_get_ticks(NULL) == GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_endpoint_init(&endpoint, storage, 1U) == GOTH_STATUS_OK);
    assert(goth_kapi_endpoint_receive(&endpoint, &received) ==
           GOTH_STATUS_EMPTY);
    assert(goth_kapi_endpoint_send(&endpoint, 17U, 23U, 31U) ==
           GOTH_STATUS_OK);
    assert(goth_kapi_endpoint_receive(&endpoint, &received) ==
           GOTH_STATUS_OK);
    assert(received.type == 17U);
    assert(received.value[0] == 23U);
    assert(received.value[1] == 31U);

    assert(goth_kapi_start_programs(&program, 1U, &started_count) ==
           GOTH_STATUS_OK);
    assert(started_count == 1U);
    assert(goth_um_run(1U) == GOTH_STATUS_OK);
    assert(kapi_program_calls == 1U);
    assert(goth_kapi_yield(&next_task) == GOTH_STATUS_BAD_STATE);
    assert(goth_kapi_start_programs(NULL, 1U, &started_count) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(started_count == 0U);
}

static void test_klog(void)
{
    char line[256];
    size_t index;

    goth_kernel_init();
    goth_klog_clear();
    assert(goth_klog_count() == 0U);

    goth_klog_emit(GOTH_LOG_INFO, "hello klog");
    goth_klog_printf(GOTH_LOG_WARNING, "value=%u path=%s", 42U, "/x");
    assert(goth_klog_count() == 2U);
    assert(goth_klog_dropped() == 0U);

    assert(goth_klog_at(0) != NULL);
    assert(goth_klog_at(0)->level == GOTH_LOG_INFO);
    assert(strcmp(goth_klog_at(0)->message, "hello klog") == 0);
    assert(goth_klog_at(1)->level == GOTH_LOG_WARNING);
    assert(strcmp(goth_klog_at(1)->message, "value=42 path=/x") == 0);
    assert(goth_klog_at(2) == NULL);

    assert(goth_klog_line(1, line, sizeof(line)) > 0U);
    assert(line[0] == '[');
    assert(strstr(line, "WARNING: value=42 path=/x") != NULL);

    assert(goth_klog_level_name(GOTH_LOG_ERR) != NULL);
    assert(strcmp(goth_klog_level_name(GOTH_LOG_ERR), "err") == 0);

    for (index = 0U; index < GOTH_KLOG_RECORDS + 8U; index++) {
        goth_klog_emit(GOTH_LOG_DEBUG, "filler");
    }
    assert(goth_klog_count() == GOTH_KLOG_RECORDS);
    /* Two messages before the filler plus the filler, minus the ring. */
    assert(goth_klog_dropped() == 2U + 8U);
    assert(goth_klog_at(GOTH_KLOG_RECORDS) == NULL);

    goth_klog_clear();
    assert(goth_klog_count() == 0U);
    assert(goth_klog_dropped() == 0U);
}

static void test_memory_protection(void)
{
    size_t before;
    uint64_t violations;

    goth_kernel_init();
    before = goth_mem_region_count();
    assert(before >= 5U);

    assert(goth_task_create(&task_a, stack_a, sizeof(stack_a), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_mem_region_count() == before + 1U);
    assert(goth_mem_check_user(stack_a, sizeof(stack_a),
                               GOTH_MEM_READ | GOTH_MEM_WRITE));
    assert(goth_mem_check_task_stack(&task_a));

    assert(goth_mem_register(guarded_buffer, sizeof(guarded_buffer),
                             GOTH_MEM_READ | GOTH_MEM_WRITE,
                             GOTH_MEM_KERNEL, 0U, "guard test") ==
           GOTH_STATUS_OK);
    assert(!goth_mem_check_user(guarded_buffer, 8U, GOTH_MEM_READ));
    assert(goth_mem_check_range(guarded_buffer, 8U,
                                GOTH_MEM_READ | GOTH_MEM_WRITE));
    assert(goth_mem_protect(guarded_buffer, GOTH_MEM_READ) == GOTH_STATUS_OK);
    assert(!goth_mem_check_range(guarded_buffer, 8U, GOTH_MEM_WRITE));
    assert(goth_mem_check_range(guarded_buffer, 8U, GOTH_MEM_READ));
    assert(goth_mem_unregister(guarded_buffer) == GOTH_STATUS_OK);
    assert(goth_mem_unregister(guarded_buffer) == GOTH_STATUS_EMPTY);

    /* Overflowing ranges are rejected instead of wrapping around. */
    assert(!goth_mem_check_range((const void *)(UINTPTR_MAX - 1U), 8U,
                                 GOTH_MEM_READ));

    /* A task cannot reach another task's registered stack. */
    assert(goth_task_create(&task_b, stack_b, sizeof(stack_b), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_kernel_schedule() == &task_a);
    assert(goth_mem_check_user(stack_a, 8U,
                               GOTH_MEM_READ | GOTH_MEM_WRITE));
    assert(!goth_mem_check_user(stack_b, 8U,
                                GOTH_MEM_READ | GOTH_MEM_WRITE));

    violations = goth_mem_violations();
    goth_mem_note_violation("test violation");
    assert(goth_mem_violations() == violations + 1U);

    assert(goth_task_destroy(&task_b) == GOTH_STATUS_OK);
}

static void test_ram_vfs(void)
{
    goth_vfs_stat_t stat;
    goth_vfs_dirent_t entry;
    uint32_t fd = 0U;
    uint32_t used = 0U;
    uint32_t total = 0U;
    uint64_t position = 0U;
    size_t amount = 0U;
    size_t index;

    goth_kernel_init();
    assert(goth_vfs_is_initialized());
    assert(strcmp(goth_vfs_backend_name(), "ramfs") == 0);
    assert(strcmp(goth_vfs_mount_point(), "/") == 0);
    assert(goth_vfs_usage(&used, &total) == GOTH_STATUS_OK);
    assert(total == GOTH_VFS_BLOCK_COUNT);
    assert(used == 0U);

    assert(goth_vfs_mkdir("/etc") == GOTH_STATUS_OK);
    assert(goth_vfs_mkdir("/etc") == GOTH_STATUS_BAD_STATE);
    assert(goth_vfs_mkdir("/etc/sub") == GOTH_STATUS_OK);
    assert(goth_vfs_stat("/", &stat) == GOTH_STATUS_OK);
    assert(stat.type == GOTH_VFS_NODE_DIRECTORY);
    assert(goth_vfs_stat("/etc", &stat) == GOTH_STATUS_OK);
    assert(stat.type == GOTH_VFS_NODE_DIRECTORY);
    assert(goth_vfs_stat("/missing", &stat) == GOTH_STATUS_EMPTY);
    assert(goth_vfs_stat("relative", &stat) == GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_vfs_open("/etc", GOTH_VFS_OPEN_READ, &fd) ==
           GOTH_STATUS_BAD_STATE);
    assert(goth_vfs_open("/missing", GOTH_VFS_OPEN_READ, &fd) ==
           GOTH_STATUS_EMPTY);
    assert(goth_vfs_open("/missing", 0U, &fd) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    assert(goth_vfs_open("/etc/motd",
                         GOTH_VFS_OPEN_READ | GOTH_VFS_OPEN_WRITE |
                             GOTH_VFS_OPEN_CREATE,
                         &fd) == GOTH_STATUS_OK);
    assert(goth_vfs_write(fd, "hello", 5U, &amount) == GOTH_STATUS_OK);
    assert(amount == 5U);
    assert(goth_vfs_seek(fd, 0, GOTH_VFS_SEEK_SET, &position) ==
           GOTH_STATUS_OK);
    assert(position == 0U);
    assert(goth_vfs_read(fd, vfs_buffer, sizeof(vfs_buffer), &amount) ==
           GOTH_STATUS_OK);
    assert(amount == 5U);
    assert(memcmp(vfs_buffer, "hello", 5U) == 0);
    assert(goth_vfs_close(fd) == GOTH_STATUS_OK);

    assert(goth_vfs_open("/etc/motd",
                         GOTH_VFS_OPEN_WRITE | GOTH_VFS_OPEN_APPEND,
                         &fd) == GOTH_STATUS_OK);
    assert(goth_vfs_write(fd, " world", 6U, &amount) == GOTH_STATUS_OK);
    assert(amount == 6U);
    assert(goth_vfs_close(fd) == GOTH_STATUS_OK);
    assert(goth_vfs_stat("/etc/motd", &stat) == GOTH_STATUS_OK);
    assert(stat.type == GOTH_VFS_NODE_FILE);
    assert(stat.size == 11U);

    assert(goth_vfs_open("/etc/motd", GOTH_VFS_OPEN_READ, &fd) ==
           GOTH_STATUS_OK);
    assert(goth_vfs_read(fd, vfs_buffer, sizeof(vfs_buffer), &amount) ==
           GOTH_STATUS_OK);
    assert(amount == 11U);
    assert(memcmp(vfs_buffer, "hello world", 11U) == 0);
    assert(goth_vfs_close(fd) == GOTH_STATUS_OK);

    assert(goth_vfs_readdir("/etc", 0U, &entry) == GOTH_STATUS_OK);
    assert(strcmp(entry.name, "motd") == 0);
    assert(entry.type == GOTH_VFS_NODE_FILE);
    assert(entry.size == 11U);
    assert(goth_vfs_readdir("/etc", 1U, &entry) == GOTH_STATUS_OK);
    assert(strcmp(entry.name, "sub") == 0);
    assert(goth_vfs_readdir("/etc", 2U, &entry) == GOTH_STATUS_EMPTY);
    assert(goth_vfs_readdir("/etc/motd", 0U, &entry) ==
           GOTH_STATUS_BAD_STATE);

    /* Path normalisation and the `.`/`..` components. */
    assert(goth_vfs_stat("/etc/./motd", &stat) == GOTH_STATUS_OK);
    assert(goth_vfs_stat("/etc/sub/../motd", &stat) == GOTH_STATUS_OK);
    assert(stat.size == 11U);

    /* Exercise a file that spans several blocks. */
    for (index = 0U; index < sizeof(vfs_buffer); index++) {
        vfs_buffer[index] = (char)(index & 0xFFU);
    }
    assert(goth_vfs_open("/etc/blob",
                         GOTH_VFS_OPEN_READ | GOTH_VFS_OPEN_WRITE |
                             GOTH_VFS_OPEN_CREATE | GOTH_VFS_OPEN_TRUNCATE,
                         &fd) == GOTH_STATUS_OK);
    assert(goth_vfs_write(fd, vfs_buffer, 600U, &amount) == GOTH_STATUS_OK);
    assert(amount == 600U);
    assert(goth_vfs_seek(fd, 0, GOTH_VFS_SEEK_SET, &position) ==
           GOTH_STATUS_OK);
    memset(vfs_buffer, 0, sizeof(vfs_buffer));
    assert(goth_vfs_read(fd, vfs_buffer, sizeof(vfs_buffer), &amount) ==
           GOTH_STATUS_OK);
    assert(amount == 600U);
    for (index = 0U; index < 600U; index++) {
        assert((uint8_t)vfs_buffer[index] == (uint8_t)(index & 0xFFU));
    }
    assert(goth_vfs_seek(fd, (int64_t)GOTH_VFS_MAX_FILE_SIZE + 1,
                         GOTH_VFS_SEEK_SET, &position) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_vfs_close(fd) == GOTH_STATUS_OK);

    assert(goth_vfs_open("/etc/motd", GOTH_VFS_OPEN_WRITE, &fd) ==
           GOTH_STATUS_OK);
    assert(goth_vfs_truncate(fd, 5U) == GOTH_STATUS_OK);
    assert(goth_vfs_close(fd) == GOTH_STATUS_OK);
    assert(goth_vfs_stat("/etc/motd", &stat) == GOTH_STATUS_OK);
    assert(stat.size == 5U);

    assert(goth_vfs_unlink("/etc") == GOTH_STATUS_BAD_STATE);
    assert(goth_vfs_unlink("/etc/motd") == GOTH_STATUS_OK);
    assert(goth_vfs_unlink("/etc/blob") == GOTH_STATUS_OK);
    assert(goth_vfs_unlink("/etc/sub") == GOTH_STATUS_OK);
    assert(goth_vfs_unlink("/etc") == GOTH_STATUS_OK);
    assert(goth_vfs_stat("/etc/motd", &stat) == GOTH_STATUS_EMPTY);
    assert(goth_vfs_unlink("/etc") == GOTH_STATUS_EMPTY);
    assert(goth_vfs_usage(&used, &total) == GOTH_STATUS_OK);
    assert(used == 0U);
}

static void test_vfs_syscalls(void)
{
    goth_syscall_request_t request = {0};
    goth_syscall_result_t result;
    goth_vfs_stat_t stat;
    uint32_t fd = 0U;
    uint64_t violations;
    size_t amount = 0U;

    goth_kernel_init();
    assert(goth_kapi_vfs_mkdir("/sys") == GOTH_STATUS_OK);
    assert(goth_kapi_vfs_open("/sys/node",
                              GOTH_VFS_OPEN_READ | GOTH_VFS_OPEN_WRITE |
                                  GOTH_VFS_OPEN_CREATE,
                              &fd) == GOTH_STATUS_OK);
    assert(goth_kapi_vfs_write(fd, "abc", 3U, &amount) == GOTH_STATUS_OK);
    assert(amount == 3U);
    assert(goth_kapi_vfs_close(fd) == GOTH_STATUS_OK);
    assert(goth_kapi_vfs_stat("/sys/node", &stat) == GOTH_STATUS_OK);
    assert(stat.size == 3U);

    assert(goth_kapi_vfs_open("/sys/node", GOTH_VFS_OPEN_READ, &fd) ==
           GOTH_STATUS_OK);
    assert(goth_kapi_vfs_read(fd, vfs_buffer, sizeof(vfs_buffer), &amount) ==
           GOTH_STATUS_OK);
    assert(amount == 3U);
    assert(memcmp(vfs_buffer, "abc", 3U) == 0);
    assert(goth_kapi_vfs_close(fd) == GOTH_STATUS_OK);

    assert(goth_kapi_vfs_readdir("/sys", 0U,
                                 (goth_vfs_dirent_t *)vfs_buffer) ==
           GOTH_STATUS_OK);

    /* A kernel-only range must not be usable as a syscall buffer. */
    assert(goth_mem_register(guarded_buffer, sizeof(guarded_buffer),
                             GOTH_MEM_READ | GOTH_MEM_WRITE,
                             GOTH_MEM_KERNEL, 0U, "guarded") ==
           GOTH_STATUS_OK);
    assert(goth_kapi_vfs_open("/sys/node", GOTH_VFS_OPEN_READ, &fd) ==
           GOTH_STATUS_OK);
    violations = goth_mem_violations();

    request.number = GOTH_SYSCALL_VFS_READ;
    request.arguments[0] = fd;
    request.arguments[1] = (uintptr_t)guarded_buffer;
    request.arguments[2] = 8U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_FAULT);
    assert(goth_mem_violations() == violations + 1U);
    assert(goth_kapi_vfs_close(fd) == GOTH_STATUS_OK);

    request.number = GOTH_SYSCALL_KLOG_LINE;
    request.arguments[0] = 0U;
    request.arguments[1] = (uintptr_t)guarded_buffer;
    request.arguments[2] = 8U;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_FAULT);

    request.number = GOTH_SYSCALL_MEM_CHECK;
    request.arguments[0] = (uintptr_t)guarded_buffer;
    request.arguments[1] = 4U;
    request.arguments[2] = GOTH_MEM_READ;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == 0U);

    assert(goth_mem_unregister(guarded_buffer) == GOTH_STATUS_OK);
}

int main(void)
{
    test_scheduler();
    test_scheduler_modes();
    test_scheduler_syscalls();
    test_endpoints();
    test_platform_expert();
    test_syscalls();
    test_um_fibers();
    test_kapi();
    test_klog();
    test_memory_protection();
    test_ram_vfs();
    test_vfs_syscalls();
    return 0;
}
