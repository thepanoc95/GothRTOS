#include "gothrtos/userapi.h"
#include "gothrtos/kernel.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/syscall.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static goth_userapi_t userapi;

static void test_users(void)
{
    goth_userapi_init(&userapi);

    assert(userapi.user_count == 1U);
    assert(goth_userapi_is_root(&userapi));
    assert(goth_userapi_current_user(&userapi) != NULL);
    assert(strcmp(goth_userapi_current_user(&userapi)->name, "root") == 0);
    assert(goth_userapi_current_user(&userapi)->uid == 0U);
    assert(goth_userapi_current_user(&userapi)->gid == 0U);
    assert(strcmp(goth_userapi_current_user(&userapi)->home, "/root") == 0);

    assert(goth_userapi_get_user(&userapi, "nobody") == NULL);
    assert(goth_userapi_set_user(&userapi, "nobody") == GOTH_STATUS_EMPTY);

    assert(goth_userapi_add_user(&userapi, "goth", 1000U, 1000U,
                                 "/home/goth") == GOTH_STATUS_OK);
    assert(goth_userapi_add_user(&userapi, "goth", 1000U, 1000U, NULL) ==
           GOTH_STATUS_BAD_STATE);
    assert(goth_userapi_get_user(&userapi, "goth") != NULL);
    assert(goth_userapi_get_user(&userapi, "goth")->uid == 1000U);
    assert(strcmp(goth_userapi_get_user(&userapi, "goth")->home,
                  "/home/goth") == 0);

    assert(goth_userapi_set_user(&userapi, "goth") == GOTH_STATUS_OK);
    assert(!goth_userapi_is_root(&userapi));
    assert(goth_userapi_current_user(&userapi)->uid == 1000U);
    assert(strcmp(goth_userapi_get_env(&userapi, "USER"), "goth") == 0);
    assert(strcmp(goth_userapi_get_env(&userapi, "HOME"), "/home/goth") == 0);

    assert(goth_userapi_remove_user(&userapi, "root") == GOTH_STATUS_BAD_STATE);
    assert(goth_userapi_remove_user(&userapi, "goth") == GOTH_STATUS_BAD_STATE);
    assert(goth_userapi_set_user(&userapi, "root") == GOTH_STATUS_OK);
    assert(goth_userapi_remove_user(&userapi, "goth") == GOTH_STATUS_OK);
    assert(goth_userapi_remove_user(&userapi, "goth") == GOTH_STATUS_EMPTY);
    assert(userapi.user_count == 1U);

    assert(goth_userapi_add_user(&userapi, "", 1U, 1U, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_environment(void)
{
    goth_userapi_init(&userapi);

    assert(strcmp(goth_userapi_get_env(&userapi, "PATH"), "/bin:/usr/bin") ==
           0);
    assert(goth_userapi_get_env(&userapi, "MISSING") == NULL);

    assert(goth_userapi_set_env(&userapi, "FOO", "bar") == GOTH_STATUS_OK);
    assert(strcmp(goth_userapi_get_env(&userapi, "FOO"), "bar") == 0);
    assert(goth_userapi_set_env(&userapi, "FOO", "baz") == GOTH_STATUS_OK);
    assert(strcmp(goth_userapi_get_env(&userapi, "FOO"), "baz") == 0);
    assert(goth_userapi_set_env(&userapi, "FOO", NULL) == GOTH_STATUS_OK);
    assert(goth_userapi_get_env(&userapi, "FOO") == NULL);
    assert(goth_userapi_set_env(&userapi, "", "x") ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_processes(void)
{
    goth_task_id_t first = 0U;
    goth_task_id_t second = 0U;
    goth_process_t listing[8];
    size_t count;

    goth_userapi_init(&userapi);

    assert(goth_userapi_spawn(&userapi, "sh", &first) == GOTH_STATUS_OK);
    assert(first != 0U);
    assert(goth_userapi_spawn(&userapi, "ls", &second) == GOTH_STATUS_OK);
    assert(second == first + 1U);
    assert(userapi.process_count == 2U);

    assert(goth_userapi_get_process(&userapi, first) != NULL);
    assert(strcmp(goth_userapi_get_process(&userapi, first)->program, "sh") ==
           0);
    assert(goth_userapi_get_process(&userapi, first)->state ==
           GOTH_PROCESS_READY);
    assert(goth_userapi_get_process(&userapi, 999U) == NULL);

    count = goth_userapi_list_processes(&userapi, listing, 8U);
    assert(count == 2U);
    assert(listing[0].id == first);
    assert(listing[1].id == second);

    assert(goth_userapi_reap(&userapi, first) == GOTH_STATUS_BAD_STATE);
    assert(goth_userapi_kill(&userapi, first) == GOTH_STATUS_OK);
    assert(goth_userapi_get_process(&userapi, first)->state ==
           GOTH_PROCESS_TERMINATED);
    assert(goth_userapi_reap(&userapi, first) == GOTH_STATUS_OK);
    assert(goth_userapi_get_process(&userapi, first) == NULL);
    assert(userapi.process_count == 1U);
    assert(goth_userapi_reap(&userapi, first) == GOTH_STATUS_EMPTY);
    assert(goth_userapi_kill(&userapi, 999U) == GOTH_STATUS_EMPTY);

    assert(goth_userapi_spawn(&userapi, "", NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_task_syscalls(void)
{
    uint64_t ticks = 0U;

    goth_kernel_init();
    assert(goth_kernel_is_initialized());

    assert(goth_userapi_get_ticks(&userapi, &ticks) == GOTH_STATUS_OK);
    assert(ticks == 0U);
    assert(goth_userapi_get_ticks(&userapi, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    /* No task is running, so scheduling operations report the kernel state. */
    assert(goth_userapi_sleep(&userapi, 0U) == GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_userapi_yield(&userapi) == GOTH_STATUS_BAD_STATE);
    assert(goth_userapi_exit(&userapi, 0) == GOTH_STATUS_OK);
}

static void test_console_and_time(void)
{
    uint64_t time_ns = 0U;

    assert(goth_pe_init() == GOTH_PE_STATUS_OK);
    assert(goth_userapi_time_ns(&userapi, &time_ns) == GOTH_STATUS_OK);
    assert(time_ns > 0U);
    assert(goth_userapi_time_ns(&userapi, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    assert(goth_userapi_console_write(&userapi, "userapi: console ok\n") ==
           GOTH_STATUS_OK);
    assert(goth_userapi_console_write(&userapi, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_endpoints(void)
{
    uint32_t first = 0U;
    uint32_t second = 0U;
    goth_message_t message;

    goth_userapi_init(&userapi);

    assert(goth_userapi_endpoint_create(&userapi, &first) == GOTH_STATUS_OK);
    assert(first == 1U);
    assert(goth_userapi_endpoint_create(&userapi, &second) == GOTH_STATUS_OK);
    assert(second == 2U);

    assert(goth_userapi_endpoint_receive(&userapi, first, &message) ==
           GOTH_STATUS_EMPTY);
    assert(goth_userapi_endpoint_send(&userapi, first, 7U, 11U, 13U) ==
           GOTH_STATUS_OK);
    assert(goth_userapi_endpoint_receive(&userapi, first, &message) ==
           GOTH_STATUS_OK);
    assert(message.type == 7U);
    assert(message.value[0] == 11U);
    assert(message.value[1] == 13U);

    assert(goth_userapi_endpoint_send(&userapi, 999U, 1U, 0U, 0U) ==
           GOTH_STATUS_EMPTY);
    assert(goth_userapi_endpoint_receive(&userapi, first, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    assert(goth_userapi_endpoint_destroy(&userapi, second) == GOTH_STATUS_OK);
    assert(goth_userapi_endpoint_destroy(&userapi, second) == GOTH_STATUS_EMPTY);
    assert(goth_userapi_endpoint_create(&userapi, &second) == GOTH_STATUS_OK);
    assert(second == 2U);
}

int main(void)
{
    test_users();
    test_environment();
    test_processes();
    test_endpoints();
    test_task_syscalls();
    test_console_and_time();
    return 0;
}
