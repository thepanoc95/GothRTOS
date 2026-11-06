#include "gothrtos/luaexec.h"
#include "gothrtos/gothlib.h"
#include "gothrtos/kernlib.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static void open_environment(lua_State *state)
{
    luaL_openlibs(state);
    (void)goth_lua_open_userapi(state);
    /* GothRTOS-specific kernel view: the nanokernel state, subsystem split,
     * HDF device table and boot parameters, reachable as goth.kernel. */
    (void)goth_lua_open_kernel(state);
}

static void set_arguments(lua_State *state,
                          int argument_count,
                          const char *const arguments[])
{
    int index;

    lua_createtable(state, argument_count + 1, 0);
    for (index = 0; index < argument_count; index++) {
        lua_pushstring(state, arguments[index]);
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    lua_setglobal(state, "arg");
}

static goth_lua_status_t report_lua_error(lua_State *state, int status)
{
    const char *message = lua_tostring(state, -1);

    if (message == NULL) {
        message = "Lua failed without an error message";
    }
    (void)fprintf(stderr, "lua: %s\n", message);

    if (status == LUA_ERRMEM) {
        return GOTH_LUA_OUT_OF_MEMORY;
    }
    if (status == LUA_ERRSYNTAX || status == LUA_ERRFILE) {
        return GOTH_LUA_COMPILE_ERROR;
    }
    return GOTH_LUA_RUNTIME_ERROR;
}

static goth_lua_status_t execute_chunk(int load_status,
                                       lua_State *state)
{
    goth_lua_status_t result;

    if (load_status != LUA_OK) {
        result = report_lua_error(state, load_status);
        lua_close(state);
        return result;
    }

    load_status = lua_pcall(state, 0, 0, 0);
    if (load_status != LUA_OK) {
        result = report_lua_error(state, load_status);
        lua_close(state);
        return result;
    }

    lua_close(state);
    return GOTH_LUA_OK;
}

static goth_lua_status_t validate_arguments(int argument_count,
                                            const char *const arguments[])
{
    int index;

    if (argument_count < 0 || argument_count == INT_MAX ||
        (argument_count > 0 && arguments == NULL)) {
        return GOTH_LUA_INVALID_ARGUMENT;
    }
    for (index = 0; index < argument_count; index++) {
        if (arguments[index] == NULL) {
            return GOTH_LUA_INVALID_ARGUMENT;
        }
    }
    return GOTH_LUA_OK;
}

goth_lua_status_t goth_lua_exec_file(const char *path,
                                     int argument_count,
                                     const char *const arguments[])
{
    lua_State *state;
    int status;
    goth_lua_status_t validation;

    if (path == NULL) {
        return GOTH_LUA_INVALID_ARGUMENT;
    }
    validation = validate_arguments(argument_count, arguments);
    if (validation != GOTH_LUA_OK) {
        return validation;
    }

    state = luaL_newstate();
    if (state == NULL) {
        return GOTH_LUA_OUT_OF_MEMORY;
    }
    open_environment(state);
    set_arguments(state, argument_count, arguments);
    status = luaL_loadfilex(state, path, NULL);
    return execute_chunk(status, state);
}

goth_lua_status_t goth_lua_exec_string(const char *source,
                                       const char *chunk_name,
                                       int argument_count,
                                       const char *const arguments[])
{
    lua_State *state;
    int status;
    goth_lua_status_t validation;

    if (source == NULL) {
        return GOTH_LUA_INVALID_ARGUMENT;
    }
    validation = validate_arguments(argument_count, arguments);
    if (validation != GOTH_LUA_OK) {
        return validation;
    }

    state = luaL_newstate();
    if (state == NULL) {
        return GOTH_LUA_OUT_OF_MEMORY;
    }
    open_environment(state);
    set_arguments(state, argument_count, arguments);
    status = luaL_loadbufferx(state, source, strlen(source),
                              chunk_name != NULL ? chunk_name : "=(string)",
                              NULL);
    return execute_chunk(status, state);
}