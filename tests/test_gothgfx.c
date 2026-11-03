/*
 * Host runner for the libgothgfx Lua tests.
 *
 * It links the real gothgfx module against the host Lua library and runs
 * tests/test_gothgfx.lua.  The tests only use in-memory surfaces, so they work
 * on development machines without a framebuffer or an evdev device.
 */

#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "gothgfx.h"

int main(int argc, char **argv)
{
    const char *script = argc > 1 ? argv[1] : "tests/test_gothgfx.lua";
    lua_State *state = luaL_newstate();
    int status;

    if (state == NULL) {
        fprintf(stderr, "test_gothgfx: cannot create Lua state\n");
        return 1;
    }
    luaL_openlibs(state);
    luaL_requiref(state, "gothgfx", luaopen_gothgfx, 1);
    lua_pop(state, 1);

    status = luaL_loadfile(state, script);
    if (status == LUA_OK) {
        status = lua_pcall(state, 0, 0, 0);
    }
    if (status != LUA_OK) {
        fprintf(stderr, "test_gothgfx: %s\n",
                lua_tostring(state, -1) != NULL ? lua_tostring(state, -1)
                                                : "(no message)");
        lua_close(state);
        return 1;
    }

    lua_close(state);
    printf("gothgfx tests passed\n");
    return 0;
}
