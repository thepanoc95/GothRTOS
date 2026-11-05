#include "gothrtos/gothlib.h"
#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/userapi.h"

#include "lauxlib.h"
#include "lua.h"

#include <limits.h>
#include <string.h>

/* The user-mode kernel runs a single Lua state, so a file-scoped instance of
   the userland tables is enough. Bare-metal builds do not link this file. */
static goth_userapi_t goth_userapi;

static const char *userapi_status_message(goth_status_t status)
{
    switch (status) {
    case GOTH_STATUS_OK:
        return "ok";
    case GOTH_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case GOTH_STATUS_NOT_INITIALIZED:
        return "kernel is not initialized";
    case GOTH_STATUS_BAD_STATE:
        return "bad state";
    case GOTH_STATUS_EMPTY:
        return "not found";
    case GOTH_STATUS_FULL:
        return "table is full";
    case GOTH_STATUS_INVALID_SYSCALL:
        return "invalid syscall";
    case GOTH_STATUS_PLATFORM_ERROR:
        return "platform error";
    case GOTH_STATUS_UNSUPPORTED:
        return "unsupported operation";
    case GOTH_STATUS_WOULD_BLOCK:
        return "operation would block";
    default:
        return "unknown error";
    }
}

static const char *scheduler_name(goth_sched_policy_t policy)
{
    return policy == GOTH_SCHED_PREEMPTIVE ? "preemptive" : "cooperative";
}

static int push_status(lua_State *state, goth_status_t status)
{
    if (status == GOTH_STATUS_OK) {
        lua_pushboolean(state, 1);
        return 1;
    }
    /* Userland calls follow the Lua convention of (false, message); spawn and
       the lookup helpers use nil to signal "no such object". */
    lua_pushboolean(state, 0);
    lua_pushstring(state, userapi_status_message(status));
    return 2;
}

static void push_user_table(lua_State *state, const goth_user_t *user)
{
    lua_createtable(state, 0, 5);
    lua_pushstring(state, user->name);
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, (lua_Integer)user->uid);
    lua_setfield(state, -2, "uid");
    lua_pushinteger(state, (lua_Integer)user->gid);
    lua_setfield(state, -2, "gid");
    lua_pushstring(state, user->home);
    lua_setfield(state, -2, "home");
    lua_pushstring(state, user->shell);
    lua_setfield(state, -2, "shell");
}

static void push_process_table(lua_State *state, const goth_process_t *process)
{
    const char *name = "unused";

    if (process->state == GOTH_PROCESS_READY) {
        name = "ready";
    } else if (process->state == GOTH_PROCESS_RUNNING) {
        name = "running";
    } else if (process->state == GOTH_PROCESS_TERMINATED) {
        name = "terminated";
    }

    lua_createtable(state, 0, 8);
    lua_pushinteger(state, (lua_Integer)process->id);
    lua_setfield(state, -2, "id");
    lua_pushinteger(state, (lua_Integer)process->task_id);
    lua_setfield(state, -2, "task_id");
    lua_pushinteger(state, (lua_Integer)process->owner);
    lua_setfield(state, -2, "owner");
    lua_pushstring(state, name);
    lua_setfield(state, -2, "state");
    lua_pushinteger(state, (lua_Integer)process->exit_code);
    lua_setfield(state, -2, "exit_code");
    lua_pushinteger(state, (lua_Integer)process->start_ticks);
    lua_setfield(state, -2, "start_ticks");
    lua_pushstring(state, process->program);
    lua_setfield(state, -2, "program");
}

static int goth_lua_write(lua_State *state)
{
    const char *text = luaL_checkstring(state, 1);

    return push_status(state,
                       goth_userapi_console_write(&goth_userapi, text));
}

static int goth_lua_println(lua_State *state)
{
    const char *text = lua_tostring(state, 1);
    goth_status_t status;

    status = goth_userapi_console_write(&goth_userapi,
                                        text != NULL ? text : "");
    if (status == GOTH_STATUS_OK) {
        status = goth_userapi_console_write(&goth_userapi, "\n");
    }
    return push_status(state, status);
}

static int goth_lua_read(lua_State *state)
{
    char buffer[512];
    size_t length = 0U;
    bool reached_end = false;

    /* Read one console line at a time so shells and scripts get a predictable
       unit. Trailing CR/LF are stripped; nil is returned at end of input and
       an empty string for a blank line. */
    for (;;) {
        char character = '\0';
        size_t read_length = 0U;
        goth_status_t status = goth_userapi_console_read(&goth_userapi,
                                                         &character, 1U,
                                                         &read_length);

        if (status != GOTH_STATUS_OK) {
            lua_pushnil(state);
            lua_pushstring(state, userapi_status_message(status));
            return 2;
        }
        if (read_length == 0U) {
            reached_end = true;
            break;
        }
        if (character == '\n') {
            break;
        }
        if (character == '\r') {
            continue;
        }
        if (length + 1U < sizeof(buffer)) {
            buffer[length++] = character;
        }
    }

    if (length == 0U && reached_end) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, buffer, length);
    return 1;
}

static int goth_lua_read_chunk(lua_State *state)
{
    lua_Integer requested = luaL_optinteger(state, 1, 256);
    char buffer[1024];
    size_t capacity = sizeof(buffer);
    size_t read_length = 0U;
    goth_status_t status;

    if (requested > 0 && (size_t)requested < capacity) {
        capacity = (size_t)requested;
    }

    status = goth_userapi_console_read(&goth_userapi, buffer, capacity,
                                       &read_length);
    if (status != GOTH_STATUS_OK) {
        lua_pushnil(state);
        lua_pushstring(state, userapi_status_message(status));
        return 2;
    }
    if (read_length == 0U) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushlstring(state, buffer, read_length);
    return 1;
}

static int goth_lua_sleep(lua_State *state)
{
    lua_Integer ticks = luaL_checkinteger(state, 1);

    if (ticks <= 0 || ticks > (lua_Integer)UINT32_MAX) {
        return push_status(state, GOTH_STATUS_INVALID_ARGUMENT);
    }
    return push_status(state,
                       goth_userapi_sleep(&goth_userapi, (uint32_t)ticks));
}

static int goth_lua_yield(lua_State *state)
{
    return push_status(state, goth_userapi_yield(&goth_userapi));
}

static int goth_lua_exit(lua_State *state)
{
    int code = (int)luaL_optinteger(state, 1, 0);

    return push_status(state, goth_userapi_exit(&goth_userapi, code));
}

static int goth_lua_ticks(lua_State *state)
{
    uint64_t ticks = 0U;

    if (goth_userapi_get_ticks(&goth_userapi, &ticks) != GOTH_STATUS_OK) {
        lua_pushinteger(state, 0);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)ticks);
    return 1;
}

static int goth_lua_time_ns(lua_State *state)
{
    uint64_t time_ns = 0U;

    if (goth_userapi_time_ns(&goth_userapi, &time_ns) != GOTH_STATUS_OK) {
        lua_pushinteger(state, 0);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)time_ns);
    return 1;
}

static int goth_lua_add_user(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);
    lua_Integer uid = luaL_optinteger(state, 2, 0);
    lua_Integer gid = luaL_optinteger(state, 3, (lua_Integer)uid);
    const char *home = lua_tostring(state, 4);

    return push_status(state,
                       goth_userapi_add_user(&goth_userapi, name,
                                             (uint32_t)uid, (uint32_t)gid,
                                             home));
}

static int goth_lua_remove_user(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);

    return push_status(state, goth_userapi_remove_user(&goth_userapi, name));
}

static int goth_lua_set_user(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);

    return push_status(state, goth_userapi_set_user(&goth_userapi, name));
}

static int goth_lua_current_user(lua_State *state)
{
    const goth_user_t *user = goth_userapi_current_user(&goth_userapi);

    if (user == NULL) {
        lua_pushnil(state);
        return 1;
    }
    push_user_table(state, user);
    return 1;
}

static int goth_lua_get_user(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);
    const goth_user_t *user = goth_userapi_get_user(&goth_userapi, name);

    if (user == NULL) {
        lua_pushnil(state);
        return 1;
    }
    push_user_table(state, user);
    return 1;
}

static int goth_lua_is_root(lua_State *state)
{
    lua_pushboolean(state, goth_userapi_is_root(&goth_userapi));
    return 1;
}

static int goth_lua_get_uid(lua_State *state)
{
    const goth_user_t *user = goth_userapi_current_user(&goth_userapi);

    lua_pushinteger(state, user != NULL ? (lua_Integer)user->uid : 0);
    return 1;
}

static int goth_lua_get_gid(lua_State *state)
{
    const goth_user_t *user = goth_userapi_current_user(&goth_userapi);

    lua_pushinteger(state, user != NULL ? (lua_Integer)user->gid : 0);
    return 1;
}

static int goth_lua_get_env(lua_State *state)
{
    const char *key = luaL_checkstring(state, 1);
    const char *value = goth_userapi_get_env(&goth_userapi, key);

    if (value == NULL) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushstring(state, value);
    return 1;
}

static int goth_lua_set_env(lua_State *state)
{
    const char *key = luaL_checkstring(state, 1);
    const char *value = lua_isnoneornil(state, 2)
                            ? NULL
                            : luaL_checkstring(state, 2);

    return push_status(state,
                       goth_userapi_set_env(&goth_userapi, key, value));
}

static int goth_lua_spawn(lua_State *state)
{
    const char *program = luaL_checkstring(state, 1);
    goth_task_id_t id = 0U;
    goth_status_t status = goth_userapi_spawn(&goth_userapi, program, &id);

    if (status != GOTH_STATUS_OK) {
        lua_pushnil(state);
        lua_pushstring(state, userapi_status_message(status));
        return 2;
    }
    lua_pushinteger(state, (lua_Integer)id);
    return 1;
}

static int goth_lua_get_task(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);
    const goth_process_t *process =
        goth_userapi_find_process(&goth_userapi, (goth_task_id_t)id);

    if (process == NULL) {
        lua_pushnil(state);
        return 1;
    }
    push_process_table(state, process);
    return 1;
}

static int goth_lua_list_tasks(lua_State *state)
{
    goth_process_t processes[GOTH_USERAPI_MAX_PROCESSES];
    size_t count = goth_userapi_list_processes(&goth_userapi, processes,
                                               GOTH_USERAPI_MAX_PROCESSES);
    size_t index;

    lua_createtable(state, (int)count, 0);
    for (index = 0U; index < count; index++) {
        push_process_table(state, &processes[index]);
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    return 1;
}

static int goth_lua_kill(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);

    return push_status(state,
                       goth_userapi_kill(&goth_userapi, (goth_task_id_t)id));
}

static int goth_lua_reap(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);

    return push_status(state,
                       goth_userapi_reap(&goth_userapi, (goth_task_id_t)id));
}

static void push_message_table(lua_State *state,
                               const goth_message_t *message)
{
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)message->sender);
    lua_setfield(state, -2, "sender");
    lua_pushinteger(state, (lua_Integer)message->type);
    lua_setfield(state, -2, "type");
    lua_pushinteger(state, (lua_Integer)message->value[0]);
    lua_setfield(state, -2, "value0");
    lua_pushinteger(state, (lua_Integer)message->value[1]);
    lua_setfield(state, -2, "value1");
}

static int goth_lua_endpoint_create(lua_State *state)
{
    uint32_t id = 0U;
    goth_status_t status = goth_userapi_endpoint_create(&goth_userapi, &id);

    if (status != GOTH_STATUS_OK) {
        lua_pushnil(state);
        lua_pushstring(state, userapi_status_message(status));
        return 2;
    }
    lua_pushinteger(state, (lua_Integer)id);
    return 1;
}

static int goth_lua_endpoint_destroy(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);

    return push_status(state, goth_userapi_endpoint_destroy(&goth_userapi,
                                                            (uint32_t)id));
}

static int goth_lua_endpoint_send(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);
    lua_Integer type = luaL_checkinteger(state, 2);
    lua_Integer value0 = luaL_optinteger(state, 3, 0);
    lua_Integer value1 = luaL_optinteger(state, 4, 0);

    return push_status(state,
                       goth_userapi_endpoint_send(&goth_userapi, (uint32_t)id,
                                                  (uint32_t)type,
                                                  (uintptr_t)value0,
                                                  (uintptr_t)value1));
}

static int goth_lua_endpoint_receive(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);
    goth_message_t message;

    if (goth_userapi_endpoint_receive(&goth_userapi, (uint32_t)id,
                                      &message) != GOTH_STATUS_OK) {
        lua_pushnil(state);
        return 1;
    }

    push_message_table(state, &message);
    return 1;
}

static int goth_lua_endpoint_poll(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);
    goth_message_t message;

    if (goth_userapi_endpoint_poll(&goth_userapi, (uint32_t)id,
                                   &message) != GOTH_STATUS_OK) {
        lua_pushnil(state);
        return 1;
    }

    push_message_table(state, &message);
    return 1;
}

static int goth_lua_scheduler(lua_State *state)
{
    lua_pushstring(state,
                   scheduler_name(goth_userapi_scheduler(&goth_userapi)));
    return 1;
}

static int goth_lua_set_scheduler(lua_State *state)
{
    const char *mode = luaL_checkstring(state, 1);
    goth_sched_policy_t policy;

    if (strcmp(mode, "preemptive") == 0 || strcmp(mode, "preempt") == 0) {
        policy = GOTH_SCHED_PREEMPTIVE;
    } else if (strcmp(mode, "cooperative") == 0 ||
               strcmp(mode, "coop") == 0) {
        policy = GOTH_SCHED_COOPERATIVE;
    } else {
        return push_status(state, GOTH_STATUS_INVALID_ARGUMENT);
    }

    return push_status(state,
                       goth_userapi_set_scheduler(&goth_userapi, policy));
}

static int goth_lua_set_preempt(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TBOOLEAN);
    return push_status(state,
                       goth_userapi_set_preemptive(&goth_userapi,
                                                   lua_toboolean(state, 1) != 0));
}

static int goth_lua_is_preempt(lua_State *state)
{
    lua_pushboolean(state,
                    goth_userapi_is_preemptive(&goth_userapi) ? 1 : 0);
    return 1;
}

static int goth_lua_get_quantum(lua_State *state)
{
    lua_pushinteger(state,
                    (lua_Integer)goth_userapi_quantum(&goth_userapi));
    return 1;
}

static int goth_lua_set_quantum(lua_State *state)
{
    lua_Integer ticks = luaL_checkinteger(state, 1);

    if (ticks <= 0 || ticks > (lua_Integer)UINT32_MAX) {
        return push_status(state, GOTH_STATUS_INVALID_ARGUMENT);
    }
    return push_status(state,
                       goth_userapi_set_quantum(&goth_userapi,
                                                (uint32_t)ticks));
}

static int goth_lua_sysinfo(lua_State *state)
{
    goth_system_info_t info;

    if (goth_userapi_system_info(&goth_userapi, &info) != GOTH_STATUS_OK) {
        lua_pushnil(state);
        return 1;
    }

    lua_createtable(state, 0, 8);
    lua_pushinteger(state, (lua_Integer)info.abi_version);
    lua_setfield(state, -2, "abi");
    lua_pushstring(state, scheduler_name(info.scheduler));
    lua_setfield(state, -2, "scheduler");
    lua_pushinteger(state, (lua_Integer)info.quantum);
    lua_setfield(state, -2, "quantum");
    lua_pushinteger(state, (lua_Integer)info.ticks);
    lua_setfield(state, -2, "ticks");
    lua_pushinteger(state, (lua_Integer)info.tasks);
    lua_setfield(state, -2, "tasks");
    lua_pushinteger(state, (lua_Integer)info.processes);
    lua_setfield(state, -2, "processes");
    lua_pushinteger(state, (lua_Integer)info.users);
    lua_setfield(state, -2, "users");
    lua_pushinteger(state, (lua_Integer)info.endpoints);
    lua_setfield(state, -2, "endpoints");
    return 1;
}

static int goth_lua_self(lua_State *state)
{
    goth_task_id_t id = goth_userapi_self(&goth_userapi);

    if (id == 0U) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)id);
    return 1;
}

static int goth_lua_wait_process(lua_State *state)
{
    lua_Integer id = luaL_checkinteger(state, 1);
    int exit_code = 0;
    goth_status_t status = goth_userapi_wait(&goth_userapi,
                                             (goth_task_id_t)id,
                                             &exit_code);

    if (status == GOTH_STATUS_WOULD_BLOCK) {
        lua_pushnil(state);
        lua_pushstring(state, userapi_status_message(status));
        return 2;
    }
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }
    lua_pushboolean(state, 1);
    lua_pushinteger(state, (lua_Integer)exit_code);
    return 2;
}

static bool parse_log_level(const char *name, goth_log_level_t *level)
{
    static const struct {
        const char *name;
        goth_log_level_t level;
    } levels[] = {
        {"emerg", GOTH_LOG_EMERG},
        {"emergency", GOTH_LOG_EMERG},
        {"alert", GOTH_LOG_ALERT},
        {"crit", GOTH_LOG_CRIT},
        {"critical", GOTH_LOG_CRIT},
        {"err", GOTH_LOG_ERR},
        {"error", GOTH_LOG_ERR},
        {"warn", GOTH_LOG_WARNING},
        {"warning", GOTH_LOG_WARNING},
        {"notice", GOTH_LOG_NOTICE},
        {"info", GOTH_LOG_INFO},
        {"debug", GOTH_LOG_DEBUG}
    };
    size_t index;

    for (index = 0U; index < sizeof(levels) / sizeof(levels[0]); index++) {
        if (strcmp(name, levels[index].name) == 0) {
            *level = levels[index].level;
            return true;
        }
    }
    return false;
}

static int goth_lua_dmesg(lua_State *state)
{
    goth_log_level_t minimum = GOTH_LOG_DEBUG;
    size_t count = goth_klog_count();
    size_t index;
    int output = 0;

    if (!lua_isnoneornil(state, 1)) {
        const char *name = luaL_checkstring(state, 1);

        if (!parse_log_level(name, &minimum)) {
            lua_pushnil(state);
            lua_pushstring(state, "unknown log level");
            return 2;
        }
    }

    lua_createtable(state, (int)count, 0);
    for (index = 0U; index < count; index++) {
        const goth_log_record_t *record = goth_klog_at(index);

        if (record == NULL || record->level > minimum) {
            continue;
        }
        lua_createtable(state, 0, 3);
        lua_pushinteger(state, (lua_Integer)record->timestamp_ns);
        lua_setfield(state, -2, "time");
        lua_pushstring(state, goth_klog_level_name(record->level));
        lua_setfield(state, -2, "level");
        lua_pushstring(state, record->message);
        lua_setfield(state, -2, "message");
        lua_rawseti(state, -2, ++output);
    }
    return 1;
}

static int goth_lua_meminfo(lua_State *state)
{
    size_t index;
    size_t count = goth_mem_region_count();

    lua_createtable(state, 0, 5);
    lua_pushinteger(state, (lua_Integer)count);
    lua_setfield(state, -2, "regions");
    lua_pushinteger(state, (lua_Integer)goth_mem_region_capacity());
    lua_setfield(state, -2, "capacity");
    lua_pushinteger(state, (lua_Integer)goth_mem_violations());
    lua_setfield(state, -2, "violations");
    lua_pushstring(state, "canary+guard-page");
    lua_setfield(state, -2, "guards");

    lua_createtable(state, (int)count, 0);
    for (index = 0U; index < count; index++) {
        const goth_mem_region_t *region = goth_mem_region_at(index);

        if (region == NULL) {
            continue;
        }
        lua_createtable(state, 0, 5);
        lua_pushstring(state, region->name != NULL ? region->name : "region");
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)region->base);
        lua_setfield(state, -2, "base");
        lua_pushinteger(state, (lua_Integer)region->size);
        lua_setfield(state, -2, "size");
        lua_pushinteger(state, (lua_Integer)region->flags);
        lua_setfield(state, -2, "flags");
        lua_pushstring(state,
                       region->kind == GOTH_MEM_USER ? "user" : "kernel");
        lua_setfield(state, -2, "kind");
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    return 1;
}

static int goth_lua_fs_write_common(lua_State *state, bool append)
{
    const char *path = luaL_checkstring(state, 1);
    size_t length = 0U;
    const char *data = luaL_checklstring(state, 2, &length);
    uint32_t fd = 0U;
    uint32_t flags = GOTH_VFS_OPEN_WRITE | GOTH_VFS_OPEN_CREATE;
    size_t written = 0U;
    goth_status_t status;

    flags |= append ? GOTH_VFS_OPEN_APPEND : GOTH_VFS_OPEN_TRUNCATE;
    status = goth_userapi_fs_open(&goth_userapi, path, flags, &fd);
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }

    status = goth_userapi_fs_write(&goth_userapi, fd, data, length, &written);
    {
        goth_status_t close_status = goth_userapi_fs_close(&goth_userapi, fd);

        if (status == GOTH_STATUS_OK) {
            status = close_status;
        }
    }
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }
    lua_pushinteger(state, (lua_Integer)written);
    return 1;
}

static int goth_lua_fs_write(lua_State *state)
{
    return goth_lua_fs_write_common(state, false);
}

static int goth_lua_fs_append(lua_State *state)
{
    return goth_lua_fs_write_common(state, true);
}

static int goth_lua_fs_read(lua_State *state)
{
    static char contents[GOTH_VFS_MAX_FILE_SIZE];
    const char *path = luaL_checkstring(state, 1);
    uint32_t fd = 0U;
    size_t total = 0U;
    goth_status_t status;

    status = goth_userapi_fs_open(&goth_userapi, path, GOTH_VFS_OPEN_READ,
                                  &fd);
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }

    while (total < sizeof(contents)) {
        size_t read_length = 0U;

        status = goth_userapi_fs_read(&goth_userapi, fd, contents + total,
                                      sizeof(contents) - total,
                                      &read_length);
        if (status != GOTH_STATUS_OK || read_length == 0U) {
            break;
        }
        total += read_length;
    }
    {
        goth_status_t close_status = goth_userapi_fs_close(&goth_userapi, fd);

        if (status == GOTH_STATUS_OK) {
            status = close_status;
        }
    }
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }
    lua_pushlstring(state, contents, total);
    return 1;
}

static int goth_lua_fs_list(lua_State *state)
{
    const char *path = luaL_checkstring(state, 1);
    uint32_t index = 0U;

    lua_createtable(state, 0, 0);
    for (;;) {
        goth_vfs_dirent_t entry;
        goth_status_t status = goth_userapi_fs_readdir(&goth_userapi, path,
                                                       index, &entry);

        if (status == GOTH_STATUS_EMPTY) {
            break;
        }
        if (status != GOTH_STATUS_OK) {
            return push_status(state, status);
        }
        lua_createtable(state, 0, 3);
        lua_pushstring(state, entry.name);
        lua_setfield(state, -2, "name");
        lua_pushstring(state,
                       entry.type == GOTH_VFS_NODE_DIRECTORY ? "dir"
                                                             : "file");
        lua_setfield(state, -2, "type");
        lua_pushinteger(state, (lua_Integer)entry.size);
        lua_setfield(state, -2, "size");
        lua_rawseti(state, -2, (lua_Integer)index + 1);
        index++;
    }
    return 1;
}

static int goth_lua_fs_mkdir(lua_State *state)
{
    const char *path = luaL_checkstring(state, 1);

    return push_status(state, goth_userapi_fs_mkdir(&goth_userapi, path));
}

static int goth_lua_fs_remove(lua_State *state)
{
    const char *path = luaL_checkstring(state, 1);

    return push_status(state, goth_userapi_fs_unlink(&goth_userapi, path));
}

static int goth_lua_fs_stat(lua_State *state)
{
    const char *path = luaL_checkstring(state, 1);
    goth_vfs_stat_t stat;
    goth_status_t status = goth_userapi_fs_stat(&goth_userapi, path, &stat);

    if (status == GOTH_STATUS_EMPTY) {
        lua_pushnil(state);
        return 1;
    }
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }

    lua_createtable(state, 0, 3);
    lua_pushstring(state,
                   stat.type == GOTH_VFS_NODE_DIRECTORY ? "dir" : "file");
    lua_setfield(state, -2, "type");
    lua_pushinteger(state, (lua_Integer)stat.size);
    lua_setfield(state, -2, "size");
    lua_pushinteger(state, (lua_Integer)stat.node);
    lua_setfield(state, -2, "node");
    return 1;
}

static int goth_lua_fs_exists(lua_State *state)
{
    const char *path = luaL_checkstring(state, 1);
    goth_vfs_stat_t stat;
    goth_status_t status = goth_userapi_fs_stat(&goth_userapi, path, &stat);

    if (status == GOTH_STATUS_EMPTY) {
        lua_pushboolean(state, 0);
        return 1;
    }
    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }
    lua_pushboolean(state, 1);
    return 1;
}

static int goth_lua_fs_usage(lua_State *state)
{
    uint32_t used = 0U;
    uint32_t total = 0U;
    goth_status_t status = goth_userapi_fs_usage(&goth_userapi, &used, &total);

    if (status != GOTH_STATUS_OK) {
        return push_status(state, status);
    }
    lua_createtable(state, 0, 2);
    lua_pushinteger(state, (lua_Integer)used);
    lua_setfield(state, -2, "used");
    lua_pushinteger(state, (lua_Integer)total);
    lua_setfield(state, -2, "total");
    return 1;
}

static const luaL_Reg goth_fs_functions[] = {
    {"write", goth_lua_fs_write},
    {"append", goth_lua_fs_append},
    {"read", goth_lua_fs_read},
    {"list", goth_lua_fs_list},
    {"mkdir", goth_lua_fs_mkdir},
    {"remove", goth_lua_fs_remove},
    {"stat", goth_lua_fs_stat},
    {"exists", goth_lua_fs_exists},
    {"usage", goth_lua_fs_usage},
    {NULL, NULL}
};

static const luaL_Reg goth_userapi_functions[] = {
    {"write", goth_lua_write},
    {"puts", goth_lua_write},
    {"println", goth_lua_println},
    {"read", goth_lua_read},
    {"readChunk", goth_lua_read_chunk},
    {"sleep", goth_lua_sleep},
    {"yield", goth_lua_yield},
    {"exit", goth_lua_exit},
    {"ticks", goth_lua_ticks},
    {"timeNs", goth_lua_time_ns},
    {"self", goth_lua_self},
    {"scheduler", goth_lua_scheduler},
    {"setScheduler", goth_lua_set_scheduler},
    {"setPreempt", goth_lua_set_preempt},
    {"isPreempt", goth_lua_is_preempt},
    {"getQuantum", goth_lua_get_quantum},
    {"setQuantum", goth_lua_set_quantum},
    {"sysinfo", goth_lua_sysinfo},
    {"addUser", goth_lua_add_user},
    {"removeUser", goth_lua_remove_user},
    {"setUser", goth_lua_set_user},
    {"currentUser", goth_lua_current_user},
    {"getUser", goth_lua_get_user},
    {"isRoot", goth_lua_is_root},
    {"getUid", goth_lua_get_uid},
    {"getGid", goth_lua_get_gid},
    {"getEnv", goth_lua_get_env},
    {"setEnv", goth_lua_set_env},
    {"spawn", goth_lua_spawn},
    {"getTask", goth_lua_get_task},
    {"listTasks", goth_lua_list_tasks},
    {"kill", goth_lua_kill},
    {"reap", goth_lua_reap},
    {"wait", goth_lua_wait_process},
    {"endpointCreate", goth_lua_endpoint_create},
    {"endpointDestroy", goth_lua_endpoint_destroy},
    {"endpointSend", goth_lua_endpoint_send},
    {"endpointReceive", goth_lua_endpoint_receive},
    {"endpointPoll", goth_lua_endpoint_poll},
    {"dmesg", goth_lua_dmesg},
    {"meminfo", goth_lua_meminfo},
    {NULL, NULL}
};

static void register_legacy_globals(lua_State *state)
{
    lua_pushcfunction(state, goth_lua_write);
    lua_setglobal(state, "goth_write");
    lua_pushcfunction(state, goth_lua_write);
    lua_setglobal(state, "goth_puts");
    lua_pushcfunction(state, goth_lua_read);
    lua_setglobal(state, "goth_read");
    lua_pushcfunction(state, goth_lua_exit);
    lua_setglobal(state, "goth_exit");
    lua_pushcfunction(state, goth_lua_sleep);
    lua_setglobal(state, "goth_sleep");
    lua_pushcfunction(state, goth_lua_yield);
    lua_setglobal(state, "goth_yield");
    lua_pushcfunction(state, goth_lua_ticks);
    lua_setglobal(state, "goth_ticks");
}

int goth_lua_open_userapi(lua_State *state)
{
    if (state == NULL) {
        return 1;
    }

    goth_userapi_init(&goth_userapi);

    luaL_newlib(state, goth_userapi_functions);
    luaL_newlib(state, goth_fs_functions);
    lua_setfield(state, -2, "fs");
    lua_pushstring(state, "GothRTOS");
    lua_setfield(state, -2, "name");
    lua_pushstring(state, "0.0-indev");
    lua_setfield(state, -2, "version");
    lua_pushinteger(state, (lua_Integer)GOTH_USERAPI_ABI_VERSION);
    lua_setfield(state, -2, "abi");
    lua_setglobal(state, "goth");

    register_legacy_globals(state);
    return 0;
}
