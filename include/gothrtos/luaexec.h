#ifndef GOTHRTOS_LUAEXEC_H
#define GOTHRTOS_LUAEXEC_H

typedef enum {
    GOTH_LUA_OK = 0,
    GOTH_LUA_INVALID_ARGUMENT,
    GOTH_LUA_OUT_OF_MEMORY,
    GOTH_LUA_COMPILE_ERROR,
    GOTH_LUA_RUNTIME_ERROR
} goth_lua_status_t;

goth_lua_status_t goth_lua_exec_file(const char *path,
                                     int argument_count,
                                     const char *const arguments[]);
goth_lua_status_t goth_lua_exec_string(const char *source,
                                       const char *chunk_name,
                                       int argument_count,
                                       const char *const arguments[]);

#endif
