#include "gothrtos/userapi.h"

#include "gothrtos/kernel.h"
#include "gothrtos/mem.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/syscall.h"

#include <limits.h>

/* Local freestanding string helpers: rtos/ is shared with the bare-metal
   ARM32 build, which does not link a C library. */

static size_t userapi_length(const char *text)
{
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool userapi_copy(char *destination,
                         size_t capacity,
                         const char *source)
{
    size_t index;

    if (destination == NULL || capacity == 0U) {
        return false;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return true;
    }

    for (index = 0U; index + 1U < capacity && source[index] != '\0'; index++) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
    return true;
}

static bool userapi_equals(const char *first, const char *second)
{
    size_t index = 0U;

    if (first == NULL || second == NULL) {
        return first == second;
    }
    while (first[index] != '\0' && first[index] == second[index]) {
        index++;
    }
    return first[index] == second[index];
}

static goth_syscall_result_t userapi_syscall(uint32_t number,
                                             uintptr_t argument0,
                                             uintptr_t argument1,
                                             uintptr_t argument2,
                                             uintptr_t argument3)
{
    goth_syscall_request_t request = {0};

    request.number = number;
    request.arguments[0] = argument0;
    request.arguments[1] = argument1;
    request.arguments[2] = argument2;
    request.arguments[3] = argument3;
    return goth_syscall_dispatch(&request);
}

static goth_status_t userapi_task_call(uint32_t number,
                                       uintptr_t argument)
{
    return userapi_syscall(number, argument, 0U, 0U, 0U).status;
}

static goth_user_t *userapi_user_at(goth_userapi_t *userapi, size_t index)
{
    if (index >= userapi->user_count) {
        return NULL;
    }
    return &userapi->users[index];
}

static goth_user_t *userapi_find_user(goth_userapi_t *userapi,
                                      const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < userapi->user_count; index++) {
        if (userapi_equals(userapi->users[index].name, name)) {
            return &userapi->users[index];
        }
    }
    return NULL;
}

static goth_env_entry_t *userapi_find_env(goth_userapi_t *userapi,
                                          const char *key)
{
    size_t index;

    if (key == NULL) {
        return NULL;
    }
    for (index = 0U; index < userapi->environment_count; index++) {
        if (userapi_equals(userapi->environment[index].key, key)) {
            return &userapi->environment[index];
        }
    }
    return NULL;
}

static void userapi_reset_process(goth_process_t *process)
{
    process->id = 0U;
    process->task_id = 0U;
    process->owner = 0U;
    process->state = GOTH_PROCESS_UNUSED;
    process->exit_code = 0;
    process->start_ticks = 0U;
    process->program[0] = '\0';
}

void goth_userapi_init(goth_userapi_t *userapi)
{
    size_t index;

    if (userapi == NULL) {
        return;
    }

    userapi->current_user = 0U;
    userapi->next_process_id = 1U;
    userapi->user_count = 0U;
    userapi->process_count = 0U;
    userapi->environment_count = 0U;

    for (index = 0U; index < GOTH_USERAPI_MAX_USERS; index++) {
        userapi->users[index].uid = 0U;
        userapi->users[index].gid = 0U;
        userapi->users[index].name[0] = '\0';
        userapi->users[index].home[0] = '\0';
        userapi->users[index].shell[0] = '\0';
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_PROCESSES; index++) {
        userapi_reset_process(&userapi->processes[index]);
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_ENV; index++) {
        userapi->environment[index].key[0] = '\0';
        userapi->environment[index].value[0] = '\0';
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_ENDPOINTS; index++) {
        userapi->endpoint_used[index] = false;
        userapi->endpoints[index].storage = NULL;
        userapi->endpoints[index].capacity = 0U;
        userapi->endpoints[index].head = 0U;
        userapi->endpoints[index].count = 0U;
    }

    (void)goth_userapi_add_user(userapi, "root", 0U, 0U, "/root");
    userapi->current_user = 0U;
    (void)goth_userapi_set_env(userapi, "PATH", "/bin:/usr/bin");
    (void)goth_userapi_set_env(userapi, "HOME", "/root");
    (void)goth_userapi_set_env(userapi, "USER", "root");
    (void)goth_userapi_set_env(userapi, "SHELL", "/bin/sh");

    /* The userland tables are reachable from the syscall gate, but they are
     * not kernel memory: mark them as a shared user range so the protection
     * layer can enforce that distinction. A duplicate registration (repeated
     * init, or an init before the kernel) is harmless. */
    (void)goth_mem_register(userapi, sizeof(*userapi),
                            GOTH_MEM_READ | GOTH_MEM_WRITE |
                                GOTH_MEM_USER_ACCESS,
                            GOTH_MEM_USER, 0U, "userapi tables");
}

goth_status_t goth_userapi_console_write(goth_userapi_t *userapi,
                                         const char *text)
{
    goth_pe_status_t status;

    (void)userapi;
    if (text == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    status = goth_pe_console_write(text, userapi_length(text));
    return status == GOTH_PE_STATUS_OK ? GOTH_STATUS_OK
                                       : GOTH_STATUS_PLATFORM_ERROR;
}

goth_status_t goth_userapi_console_read(goth_userapi_t *userapi,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *read_length)
{
    goth_pe_status_t status;

    (void)userapi;
    if (buffer == NULL || read_length == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    status = goth_pe_console_read(buffer, capacity, read_length);
    return status == GOTH_PE_STATUS_OK ? GOTH_STATUS_OK
                                       : GOTH_STATUS_PLATFORM_ERROR;
}

goth_status_t goth_userapi_sleep(goth_userapi_t *userapi, uint32_t ticks)
{
    (void)userapi;
    if (ticks == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return userapi_task_call(GOTH_SYSCALL_TASK_SLEEP, ticks);
}

goth_status_t goth_userapi_yield(goth_userapi_t *userapi)
{
    (void)userapi;
    return userapi_task_call(GOTH_SYSCALL_TASK_YIELD, 0U);
}

goth_status_t goth_userapi_exit(goth_userapi_t *userapi, int code)
{
    goth_status_t status;

    (void)userapi;
    (void)code;
    status = userapi_task_call(GOTH_SYSCALL_TASK_EXIT, 0U);
    if (status == GOTH_STATUS_BAD_STATE && goth_kernel_current_task() == NULL) {
        /* No running task to terminate: exit is still a valid request. */
        return GOTH_STATUS_OK;
    }
    return status;
}

goth_status_t goth_userapi_get_ticks(goth_userapi_t *userapi, uint64_t *ticks)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (ticks == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    result = userapi_syscall(GOTH_SYSCALL_KERNEL_TICKS, 0U, 0U, 0U, 0U);
    if (result.status == GOTH_STATUS_OK) {
        *ticks = result.value;
    }
    return result.status;
}

goth_status_t goth_userapi_time_ns(goth_userapi_t *userapi, uint64_t *time_ns)
{
    goth_pe_status_t status;

    (void)userapi;
    if (time_ns == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    status = goth_pe_time_ns(time_ns);
    return status == GOTH_PE_STATUS_OK ? GOTH_STATUS_OK
                                       : GOTH_STATUS_PLATFORM_ERROR;
}

goth_status_t goth_userapi_set_scheduler(goth_userapi_t *userapi,
                                         goth_sched_policy_t policy)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (policy != GOTH_SCHED_COOPERATIVE &&
        policy != GOTH_SCHED_PREEMPTIVE) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    result = userapi_syscall(GOTH_SYSCALL_KERNEL_SET_SCHED,
                             (uintptr_t)policy, 0U, 0U, 0U);
    return result.status;
}

goth_status_t goth_userapi_set_preemptive(goth_userapi_t *userapi,
                                          bool enabled)
{
    return goth_userapi_set_scheduler(userapi,
                                      enabled ? GOTH_SCHED_PREEMPTIVE
                                              : GOTH_SCHED_COOPERATIVE);
}

goth_sched_policy_t goth_userapi_scheduler(const goth_userapi_t *userapi)
{
    goth_syscall_result_t result;

    (void)userapi;
    result = userapi_syscall(GOTH_SYSCALL_KERNEL_GET_SCHED, 0U, 0U, 0U, 0U);
    if (result.status != GOTH_STATUS_OK) {
        return GOTH_SCHED_COOPERATIVE;
    }
    return result.value == GOTH_SCHED_PREEMPTIVE ? GOTH_SCHED_PREEMPTIVE
                                                 : GOTH_SCHED_COOPERATIVE;
}

bool goth_userapi_is_preemptive(const goth_userapi_t *userapi)
{
    return goth_userapi_scheduler(userapi) == GOTH_SCHED_PREEMPTIVE;
}

goth_status_t goth_userapi_set_quantum(goth_userapi_t *userapi,
                                       uint32_t ticks)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (ticks == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    result = userapi_syscall(GOTH_SYSCALL_KERNEL_SET_QUANTUM, ticks,
                             0U, 0U, 0U);
    return result.status;
}

uint32_t goth_userapi_quantum(const goth_userapi_t *userapi)
{
    goth_syscall_result_t result;

    (void)userapi;
    result = userapi_syscall(GOTH_SYSCALL_KERNEL_GET_QUANTUM, 0U, 0U, 0U, 0U);
    return result.status == GOTH_STATUS_OK ? (uint32_t)result.value : 1U;
}

goth_status_t goth_userapi_system_info(goth_userapi_t *userapi,
                                       goth_system_info_t *info)
{
    goth_syscall_result_t result;
    size_t index;

    if (userapi == NULL || info == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    info->abi_version = GOTH_USERAPI_ABI_VERSION;
    info->scheduler = goth_userapi_scheduler(userapi);
    info->quantum = goth_userapi_quantum(userapi);
    info->ticks = 0U;
    (void)goth_userapi_get_ticks(userapi, &info->ticks);

    result = userapi_syscall(GOTH_SYSCALL_KERNEL_TASK_COUNT, 0U, 0U, 0U, 0U);
    info->tasks = result.status == GOTH_STATUS_OK ? (size_t)result.value : 0U;
    info->processes = userapi->process_count;
    info->users = userapi->user_count;
    info->endpoints = 0U;
    for (index = 0U; index < GOTH_USERAPI_MAX_ENDPOINTS; index++) {
        if (userapi->endpoint_used[index]) {
            info->endpoints++;
        }
    }
    return GOTH_STATUS_OK;
}

goth_task_id_t goth_userapi_self(const goth_userapi_t *userapi)
{
    goth_syscall_result_t result;

    (void)userapi;
    result = userapi_syscall(GOTH_SYSCALL_TASK_SELF, 0U, 0U, 0U, 0U);
    if (result.status != GOTH_STATUS_OK || result.value > UINT32_MAX) {
        return 0U;
    }
    return (goth_task_id_t)result.value;
}

goth_status_t goth_userapi_add_user(goth_userapi_t *userapi,
                                    const char *name,
                                    uint32_t uid,
                                    uint32_t gid,
                                    const char *home)
{
    goth_user_t *user;

    if (userapi == NULL || name == NULL || name[0] == '\0') {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (userapi->user_count >= GOTH_USERAPI_MAX_USERS) {
        return GOTH_STATUS_FULL;
    }
    if (userapi_find_user(userapi, name) != NULL) {
        return GOTH_STATUS_BAD_STATE;
    }

    user = &userapi->users[userapi->user_count];
    (void)userapi_copy(user->name, sizeof(user->name), name);
    user->uid = uid;
    user->gid = gid;
    if (home != NULL && home[0] != '\0') {
        (void)userapi_copy(user->home, sizeof(user->home), home);
    } else {
        (void)userapi_copy(user->home, sizeof(user->home), "/home/");
        {
            size_t base = userapi_length(user->home);
            size_t index = 0U;

            while (base + index + 1U < sizeof(user->home) &&
                   name[index] != '\0') {
                user->home[base + index] = name[index];
                index++;
            }
            user->home[base + index] = '\0';
        }
    }
    (void)userapi_copy(user->shell, sizeof(user->shell), "/bin/sh");
    userapi->user_count++;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_remove_user(goth_userapi_t *userapi,
                                       const char *name)
{
    goth_user_t *user;
    size_t index;

    if (userapi == NULL || name == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    user = userapi_find_user(userapi, name);
    if (user == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    if (user->uid == 0U || user == userapi_user_at(userapi,
                                                   userapi->current_user)) {
        return GOTH_STATUS_BAD_STATE;
    }

    index = (size_t)(user - userapi->users);
    for (; index + 1U < userapi->user_count; index++) {
        userapi->users[index] = userapi->users[index + 1U];
    }
    userapi->user_count--;
    if (userapi->current_user >= userapi->user_count) {
        userapi->current_user = 0U;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_set_user(goth_userapi_t *userapi, const char *name)
{
    goth_user_t *user;

    if (userapi == NULL || name == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    user = userapi_find_user(userapi, name);
    if (user == NULL) {
        return GOTH_STATUS_EMPTY;
    }

    userapi->current_user = (uint32_t)(user - userapi->users);
    (void)goth_userapi_set_env(userapi, "USER", user->name);
    (void)goth_userapi_set_env(userapi, "HOME", user->home);
    (void)goth_userapi_set_env(userapi, "SHELL", user->shell);
    return GOTH_STATUS_OK;
}

const goth_user_t *goth_userapi_current_user(const goth_userapi_t *userapi)
{
    if (userapi == NULL || userapi->user_count == 0U) {
        return NULL;
    }
    return &userapi->users[userapi->current_user];
}

const goth_user_t *goth_userapi_get_user(const goth_userapi_t *userapi,
                                         const char *name)
{
    return userapi_find_user((goth_userapi_t *)userapi, name);
}

bool goth_userapi_is_root(const goth_userapi_t *userapi)
{
    const goth_user_t *user = goth_userapi_current_user(userapi);

    return user != NULL && user->uid == 0U;
}

goth_status_t goth_userapi_set_env(goth_userapi_t *userapi,
                                   const char *key,
                                   const char *value)
{
    goth_env_entry_t *entry;
    size_t index;

    if (userapi == NULL || key == NULL || key[0] == '\0') {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    entry = userapi_find_env(userapi, key);
    if (value == NULL) {
        if (entry == NULL) {
            return GOTH_STATUS_OK;
        }
        index = (size_t)(entry - userapi->environment);
        for (; index + 1U < userapi->environment_count; index++) {
            userapi->environment[index] = userapi->environment[index + 1U];
        }
        userapi->environment_count--;
        return GOTH_STATUS_OK;
    }

    if (entry == NULL) {
        if (userapi->environment_count >= GOTH_USERAPI_MAX_ENV) {
            return GOTH_STATUS_FULL;
        }
        entry = &userapi->environment[userapi->environment_count++];
        (void)userapi_copy(entry->key, sizeof(entry->key), key);
    }
    (void)userapi_copy(entry->value, sizeof(entry->value), value);
    return GOTH_STATUS_OK;
}

const char *goth_userapi_get_env(const goth_userapi_t *userapi,
                                 const char *key)
{
    goth_env_entry_t *entry =
        userapi_find_env((goth_userapi_t *)userapi, key);

    return entry != NULL ? entry->value : NULL;
}

goth_status_t goth_userapi_spawn(goth_userapi_t *userapi,
                                 const char *program,
                                 goth_task_id_t *id)
{
    const goth_user_t *owner;
    goth_process_t *process;
    size_t index;

    if (userapi == NULL || program == NULL || program[0] == '\0') {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (userapi->process_count >= GOTH_USERAPI_MAX_PROCESSES) {
        return GOTH_STATUS_FULL;
    }

    for (index = 0U; index < GOTH_USERAPI_MAX_PROCESSES; index++) {
        if (userapi->processes[index].state == GOTH_PROCESS_UNUSED) {
            break;
        }
    }
    if (index >= GOTH_USERAPI_MAX_PROCESSES) {
        return GOTH_STATUS_FULL;
    }

    process = &userapi->processes[index];
    userapi_reset_process(process);
    process->id = userapi->next_process_id++;
    if (userapi->next_process_id == 0U) {
        userapi->next_process_id = 1U;
    }
    owner = goth_userapi_current_user(userapi);
    process->owner = owner != NULL ? owner->uid : 0U;
    process->state = GOTH_PROCESS_READY;
    process->exit_code = 0;
    process->task_id = 0U;
    (void)goth_userapi_get_ticks(userapi, &process->start_ticks);
    (void)userapi_copy(process->program, sizeof(process->program), program);
    userapi->process_count++;

    if (id != NULL) {
        *id = process->id;
    }
    return GOTH_STATUS_OK;
}

const goth_process_t *goth_userapi_find_process(const goth_userapi_t *userapi,
                                                goth_task_id_t id)
{
    size_t index;

    if (userapi == NULL) {
        return NULL;
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_PROCESSES; index++) {
        if (userapi->processes[index].state != GOTH_PROCESS_UNUSED &&
            userapi->processes[index].id == id) {
            return &userapi->processes[index];
        }
    }
    return NULL;
}

goth_process_t *goth_userapi_get_process(goth_userapi_t *userapi,
                                         goth_task_id_t id)
{
    return (goth_process_t *)goth_userapi_find_process(userapi, id);
}

goth_status_t goth_userapi_set_process_state(goth_userapi_t *userapi,
                                             goth_task_id_t id,
                                             goth_process_state_t state)
{
    goth_process_t *process = goth_userapi_get_process(userapi, id);

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    process->state = state;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_attach_process(goth_userapi_t *userapi,
                                          goth_task_id_t id,
                                          goth_task_id_t task_id)
{
    goth_process_t *process = goth_userapi_get_process(userapi, id);

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    process->task_id = task_id;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_set_exit_code(goth_userapi_t *userapi,
                                         goth_task_id_t id,
                                         int code)
{
    goth_process_t *process = goth_userapi_get_process(userapi, id);

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    process->exit_code = code;
    process->state = GOTH_PROCESS_TERMINATED;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_wait(goth_userapi_t *userapi,
                                goth_task_id_t id,
                                int *exit_code)
{
    const goth_process_t *process = goth_userapi_find_process(userapi, id);

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    if (process->state != GOTH_PROCESS_TERMINATED) {
        return GOTH_STATUS_WOULD_BLOCK;
    }
    if (exit_code != NULL) {
        *exit_code = process->exit_code;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_kill(goth_userapi_t *userapi, goth_task_id_t id)
{
    goth_process_t *process = goth_userapi_get_process(userapi, id);

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    process->state = GOTH_PROCESS_TERMINATED;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_reap(goth_userapi_t *userapi, goth_task_id_t id)
{
    goth_process_t *process = goth_userapi_get_process(userapi, id);
    size_t index;

    if (process == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    if (process->state != GOTH_PROCESS_TERMINATED) {
        return GOTH_STATUS_BAD_STATE;
    }

    index = (size_t)(process - userapi->processes);
    for (; index + 1U < GOTH_USERAPI_MAX_PROCESSES; index++) {
        userapi->processes[index] = userapi->processes[index + 1U];
    }
    userapi_reset_process(&userapi->processes[GOTH_USERAPI_MAX_PROCESSES - 1U]);
    userapi->process_count--;
    return GOTH_STATUS_OK;
}

size_t goth_userapi_list_processes(const goth_userapi_t *userapi,
                                   goth_process_t *out,
                                   size_t capacity)
{
    size_t index;
    size_t count = 0U;

    if (userapi == NULL || out == NULL) {
        return 0U;
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_PROCESSES; index++) {
        if (userapi->processes[index].state == GOTH_PROCESS_UNUSED) {
            continue;
        }
        if (count >= capacity) {
            break;
        }
        out[count++] = userapi->processes[index];
    }
    return count;
}

static goth_endpoint_t *userapi_endpoint_at(goth_userapi_t *userapi,
                                            uint32_t id)
{
    if (userapi == NULL || id == 0U || id > GOTH_USERAPI_MAX_ENDPOINTS) {
        return NULL;
    }
    if (!userapi->endpoint_used[id - 1U]) {
        return NULL;
    }
    return &userapi->endpoints[id - 1U];
}

goth_status_t goth_userapi_endpoint_create(goth_userapi_t *userapi,
                                           uint32_t *id)
{
    uint32_t index;

    if (userapi == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_ENDPOINTS; index++) {
        if (!userapi->endpoint_used[index]) {
            goth_status_t status;

            status = goth_endpoint_init(
                &userapi->endpoints[index],
                userapi->endpoint_storage[index],
                GOTH_USERAPI_ENDPOINT_CAPACITY);
            if (status != GOTH_STATUS_OK) {
                return status;
            }
            userapi->endpoint_used[index] = true;
            if (id != NULL) {
                *id = index + 1U;
            }
            return GOTH_STATUS_OK;
        }
    }
    return GOTH_STATUS_FULL;
}

goth_status_t goth_userapi_endpoint_destroy(goth_userapi_t *userapi,
                                            uint32_t id)
{
    goth_endpoint_t *endpoint = userapi_endpoint_at(userapi, id);

    if (endpoint == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    endpoint->storage = NULL;
    endpoint->capacity = 0U;
    endpoint->head = 0U;
    endpoint->count = 0U;
    userapi->endpoint_used[id - 1U] = false;
    return GOTH_STATUS_OK;
}

goth_status_t goth_userapi_endpoint_send(goth_userapi_t *userapi,
                                         uint32_t id,
                                         uint32_t type,
                                         uintptr_t value0,
                                         uintptr_t value1)
{
    goth_endpoint_t *endpoint = userapi_endpoint_at(userapi, id);

    if (endpoint == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    return goth_endpoint_send(endpoint, type, value0, value1);
}

goth_status_t goth_userapi_endpoint_receive(goth_userapi_t *userapi,
                                            uint32_t id,
                                            goth_message_t *message)
{
    goth_endpoint_t *endpoint = userapi_endpoint_at(userapi, id);

    if (endpoint == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    if (message == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return goth_endpoint_receive(endpoint, message);
}

goth_status_t goth_userapi_endpoint_poll(goth_userapi_t *userapi,
                                         uint32_t id,
                                         goth_message_t *message)
{
    goth_endpoint_t *endpoint = userapi_endpoint_at(userapi, id);

    if (endpoint == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    if (message == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return goth_endpoint_poll(endpoint, message);
}

size_t goth_userapi_endpoint_count(const goth_userapi_t *userapi)
{
    size_t index;
    size_t count = 0U;

    if (userapi == NULL) {
        return 0U;
    }
    for (index = 0U; index < GOTH_USERAPI_MAX_ENDPOINTS; index++) {
        if (userapi->endpoint_used[index]) {
            count++;
        }
    }
    return count;
}

goth_status_t goth_userapi_fs_open(goth_userapi_t *userapi,
                                   const char *path,
                                   uint32_t flags,
                                   uint32_t *fd)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (path == NULL || fd == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    result = userapi_syscall(GOTH_SYSCALL_VFS_OPEN, (uintptr_t)path, flags,
                             0U, 0U);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > UINT32_MAX) {
            return GOTH_STATUS_BAD_STATE;
        }
        *fd = (uint32_t)result.value;
    }
    return result.status;
}

goth_status_t goth_userapi_fs_close(goth_userapi_t *userapi, uint32_t fd)
{
    (void)userapi;
    return userapi_syscall(GOTH_SYSCALL_VFS_CLOSE, fd, 0U, 0U, 0U).status;
}

goth_status_t goth_userapi_fs_read(goth_userapi_t *userapi,
                                   uint32_t fd,
                                   void *buffer,
                                   size_t capacity,
                                   size_t *read_length)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (read_length == NULL || (buffer == NULL && capacity != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    result = userapi_syscall(GOTH_SYSCALL_VFS_READ, fd, (uintptr_t)buffer,
                             capacity, 0U);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > (uint64_t)(size_t)-1) {
            return GOTH_STATUS_BAD_STATE;
        }
        *read_length = (size_t)result.value;
    }
    return result.status;
}

goth_status_t goth_userapi_fs_write(goth_userapi_t *userapi,
                                    uint32_t fd,
                                    const void *data,
                                    size_t length,
                                    size_t *written)
{
    goth_syscall_result_t result;

    (void)userapi;
    if (written == NULL || (data == NULL && length != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    result = userapi_syscall(GOTH_SYSCALL_VFS_WRITE, fd, (uintptr_t)data,
                             length, 0U);
    if (result.status == GOTH_STATUS_OK) {
        if (result.value > (uint64_t)(size_t)-1) {
            return GOTH_STATUS_BAD_STATE;
        }
        *written = (size_t)result.value;
    }
    return result.status;
}

goth_status_t goth_userapi_fs_seek(goth_userapi_t *userapi,
                                   uint32_t fd,
                                   int64_t offset,
                                   goth_vfs_whence_t whence,
                                   uint64_t *position)
{
    goth_syscall_result_t result;

    (void)userapi;
    result = userapi_syscall(GOTH_SYSCALL_VFS_SEEK, fd, (uintptr_t)offset,
                             (uintptr_t)whence, 0U);
    if (result.status == GOTH_STATUS_OK && position != NULL) {
        *position = result.value;
    }
    return result.status;
}

goth_status_t goth_userapi_fs_truncate(goth_userapi_t *userapi,
                                       uint32_t fd,
                                       uint64_t size)
{
    (void)userapi;
    return userapi_syscall(GOTH_SYSCALL_VFS_TRUNCATE, fd, (uintptr_t)size,
                           0U, 0U).status;
}

goth_status_t goth_userapi_fs_stat(goth_userapi_t *userapi,
                                   const char *path,
                                   goth_vfs_stat_t *stat)
{
    (void)userapi;
    if (path == NULL || stat == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return userapi_syscall(GOTH_SYSCALL_VFS_STAT, (uintptr_t)path,
                           (uintptr_t)stat, 0U, 0U).status;
}

goth_status_t goth_userapi_fs_mkdir(goth_userapi_t *userapi,
                                    const char *path)
{
    (void)userapi;
    if (path == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return userapi_syscall(GOTH_SYSCALL_VFS_MKDIR, (uintptr_t)path, 0U, 0U,
                           0U).status;
}

goth_status_t goth_userapi_fs_unlink(goth_userapi_t *userapi,
                                     const char *path)
{
    (void)userapi;
    if (path == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return userapi_syscall(GOTH_SYSCALL_VFS_UNLINK, (uintptr_t)path, 0U, 0U,
                           0U).status;
}

goth_status_t goth_userapi_fs_readdir(goth_userapi_t *userapi,
                                      const char *path,
                                      uint32_t index,
                                      goth_vfs_dirent_t *entry)
{
    (void)userapi;
    if (path == NULL || entry == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    return userapi_syscall(GOTH_SYSCALL_VFS_READDIR, (uintptr_t)path, index,
                           (uintptr_t)entry, 0U).status;
}

goth_status_t goth_userapi_fs_usage(goth_userapi_t *userapi,
                                    uint32_t *used_blocks,
                                    uint32_t *total_blocks)
{
    goth_syscall_result_t result;

    (void)userapi;
    result = userapi_syscall(GOTH_SYSCALL_VFS_USAGE, 0U, 0U, 0U, 0U);
    if (result.status == GOTH_STATUS_OK) {
        if (used_blocks != NULL) {
            *used_blocks = (uint32_t)(result.value >> 32);
        }
        if (total_blocks != NULL) {
            *total_blocks = (uint32_t)(result.value & UINT32_MAX);
        }
    }
    return result.status;
}
