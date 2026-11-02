#ifndef GOTHRTOS_USERAPI_H
#define GOTHRTOS_USERAPI_H

#include "gothrtos/kernel.h"

/* GothRTOS userland API.
 *
 * The GothRTOS nanokernel is a usermode-oriented system: the kernel only
 * schedules cooperative tasks and moves messages between endpoints, while
 * every piece of operating-system policy (console, time, identity, environment
 * and the process table) lives here in the userland library. There is
 * deliberately no POSIX or UNIX surface: programs call goth_userapi_* and the
 * Lua binding in os/init/gothlib.c exposes the same functions to scripts.
 *
 * The console and clock are delegated to pexpert; the task operations are
 * delegated to the syscall layer; the process, user and environment tables are
 * owned by this module. */

#define GOTH_USERAPI_NAME_SIZE 32U
#define GOTH_USERAPI_PATH_SIZE 128U
#define GOTH_USERAPI_VALUE_SIZE 128U
#define GOTH_USERAPI_MAX_USERS 16U
#define GOTH_USERAPI_MAX_PROCESSES 64U
#define GOTH_USERAPI_MAX_ENV 32U
#define GOTH_USERAPI_MAX_ENDPOINTS 16U
#define GOTH_USERAPI_ENDPOINT_CAPACITY 8U

typedef enum {
    GOTH_PROCESS_UNUSED = 0,
    GOTH_PROCESS_READY,
    GOTH_PROCESS_RUNNING,
    GOTH_PROCESS_TERMINATED
} goth_process_state_t;

typedef struct {
    goth_task_id_t id;
    uint32_t owner;
    goth_process_state_t state;
    int exit_code;
    char program[GOTH_USERAPI_NAME_SIZE];
} goth_process_t;

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char name[GOTH_USERAPI_NAME_SIZE];
    char home[GOTH_USERAPI_PATH_SIZE];
    char shell[GOTH_USERAPI_PATH_SIZE];
} goth_user_t;

typedef struct {
    char key[GOTH_USERAPI_NAME_SIZE];
    char value[GOTH_USERAPI_VALUE_SIZE];
} goth_env_entry_t;

typedef struct {
    uint32_t current_user;
    uint32_t next_process_id;
    goth_user_t users[GOTH_USERAPI_MAX_USERS];
    size_t user_count;
    goth_process_t processes[GOTH_USERAPI_MAX_PROCESSES];
    size_t process_count;
    goth_env_entry_t environment[GOTH_USERAPI_MAX_ENV];
    size_t environment_count;
    goth_endpoint_t endpoints[GOTH_USERAPI_MAX_ENDPOINTS];
    goth_message_t endpoint_storage[GOTH_USERAPI_MAX_ENDPOINTS]
                                  [GOTH_USERAPI_ENDPOINT_CAPACITY];
    bool endpoint_used[GOTH_USERAPI_MAX_ENDPOINTS];
} goth_userapi_t;

typedef struct {
    uint32_t id;
    bool used;
} goth_userapi_endpoint_t;

void goth_userapi_init(goth_userapi_t *userapi);

goth_status_t goth_userapi_console_write(goth_userapi_t *userapi,
                                         const char *text);
goth_status_t goth_userapi_console_read(goth_userapi_t *userapi,
                                        char *buffer,
                                        size_t capacity,
                                        size_t *read_length);
goth_status_t goth_userapi_sleep(goth_userapi_t *userapi, uint32_t ticks);
goth_status_t goth_userapi_yield(goth_userapi_t *userapi);
goth_status_t goth_userapi_exit(goth_userapi_t *userapi, int code);
goth_status_t goth_userapi_get_ticks(goth_userapi_t *userapi, uint64_t *ticks);
goth_status_t goth_userapi_time_ns(goth_userapi_t *userapi, uint64_t *time_ns);

goth_status_t goth_userapi_add_user(goth_userapi_t *userapi,
                                    const char *name,
                                    uint32_t uid,
                                    uint32_t gid,
                                    const char *home);
goth_status_t goth_userapi_remove_user(goth_userapi_t *userapi,
                                       const char *name);
goth_status_t goth_userapi_set_user(goth_userapi_t *userapi, const char *name);
const goth_user_t *goth_userapi_current_user(const goth_userapi_t *userapi);
const goth_user_t *goth_userapi_get_user(const goth_userapi_t *userapi,
                                         const char *name);
bool goth_userapi_is_root(const goth_userapi_t *userapi);

goth_status_t goth_userapi_set_env(goth_userapi_t *userapi,
                                   const char *key,
                                   const char *value);
const char *goth_userapi_get_env(const goth_userapi_t *userapi,
                                 const char *key);

goth_status_t goth_userapi_spawn(goth_userapi_t *userapi,
                                 const char *program,
                                 goth_task_id_t *id);
goth_process_t *goth_userapi_get_process(goth_userapi_t *userapi,
                                         goth_task_id_t id);
const goth_process_t *goth_userapi_find_process(const goth_userapi_t *userapi,
                                                goth_task_id_t id);
goth_status_t goth_userapi_set_process_state(goth_userapi_t *userapi,
                                             goth_task_id_t id,
                                             goth_process_state_t state);
goth_status_t goth_userapi_kill(goth_userapi_t *userapi, goth_task_id_t id);
goth_status_t goth_userapi_reap(goth_userapi_t *userapi, goth_task_id_t id);
size_t goth_userapi_list_processes(const goth_userapi_t *userapi,
                                   goth_process_t *out,
                                   size_t capacity);

goth_status_t goth_userapi_endpoint_create(goth_userapi_t *userapi,
                                           uint32_t *id);
goth_status_t goth_userapi_endpoint_destroy(goth_userapi_t *userapi,
                                            uint32_t id);
goth_status_t goth_userapi_endpoint_send(goth_userapi_t *userapi,
                                         uint32_t id,
                                         uint32_t type,
                                         uintptr_t value0,
                                         uintptr_t value1);
goth_status_t goth_userapi_endpoint_receive(goth_userapi_t *userapi,
                                            uint32_t id,
                                            goth_message_t *message);

#endif
