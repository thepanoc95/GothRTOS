#include "gothrtos/luaexec.h"

#include <assert.h>
#include <stddef.h>

int main(void)
{
    const char *arguments[] = {"first", "second"};

    assert(goth_lua_exec_string(
               "assert(_VERSION == 'Lua 5.5'); "
               "assert(arg[1] == 'first' and arg[2] == 'second')",
               "=(smoke test)", 2, arguments) == GOTH_LUA_OK);
    assert(goth_lua_exec_string("this is not valid Lua", "=(syntax test)",
                                0, NULL) == GOTH_LUA_COMPILE_ERROR);
    assert(goth_lua_exec_string("error('expected runtime error')",
                                "=(runtime test)", 0, NULL) ==
           GOTH_LUA_RUNTIME_ERROR);
    assert(goth_lua_exec_string(NULL, NULL, 0, NULL) ==
           GOTH_LUA_INVALID_ARGUMENT);
    assert(goth_lua_exec_string("return", NULL, 1, NULL) ==
           GOTH_LUA_INVALID_ARGUMENT);
    assert(goth_lua_exec_file("tests/lua_smoke.lua", 2, arguments) ==
           GOTH_LUA_OK);
    assert(goth_lua_exec_file(NULL, 0, NULL) == GOTH_LUA_INVALID_ARGUMENT);
    return 0;
}
