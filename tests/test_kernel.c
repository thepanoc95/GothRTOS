#include "gothrtos/kernel.h"
#include "gothrtos/kapi.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/syscall.h"
#include "gothrtos/um.h"

#include <assert.h>
#include <stdint.h>

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
    assert(goth_task_find(task_a.id) == &task_a);
    assert(goth_task_find(task_b.id) == &task_b);
    assert(goth_task_find(0U) == NULL);
    assert(goth_task_find(UINT32_MAX) == NULL);

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
    assert(goth_task_set_priority(&task_c, 1U) == GOTH_STATUS_BAD_STATE);
    assert(goth_task_destroy(&task_c) == GOTH_STATUS_OK);
    assert(task_c.state == GOTH_TASK_UNUSED);
    assert(goth_task_find(task_c.id) == NULL);
}

static void test_task_management(void)
{
    goth_task_id_t task_a_id;

    goth_kernel_init();
    assert(goth_task_create(&task_a, stack_a, sizeof(stack_a), 1U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    assert(goth_task_create(&task_b, stack_b, sizeof(stack_b), 2U,
                            empty_task, NULL) == GOTH_STATUS_OK);
    task_a_id = task_a.id;
    assert(goth_kernel_schedule() == &task_b);
    assert(goth_task_set_priority(&task_a, 3U) == GOTH_STATUS_OK);
    assert(goth_kernel_schedule() == &task_a);
    assert(goth_task_exit() == &task_b);
    assert(goth_task_find(task_a_id) == &task_a);
    assert(goth_task_destroy(&task_a) == GOTH_STATUS_OK);
    assert(goth_task_find(task_a_id) == NULL);
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
    request.number = GOTH_SYSCALL_GET_ABI_VERSION;
    result = goth_syscall_dispatch(&request);
    assert(result.status == GOTH_STATUS_OK);
    assert(result.value == GOTH_SYSCALL_ABI_VERSION);

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

int main(void)
{
    test_scheduler();
    test_task_management();
    test_endpoints();
    test_platform_expert();
    test_syscalls();
    test_um_fibers();
    test_kapi();
    return 0;
}
