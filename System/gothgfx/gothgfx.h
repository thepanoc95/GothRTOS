#ifndef GOTHGFX_H
#define GOTHGFX_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Loads the gothgfx Lua module into `state`.  The module is published as the
 * `gothgfx` global and returned on the stack, so both
 *
 *     require("gothgfx")
 *
 * and a statically linked host calling
 *
 *     luaL_requiref(L, "gothgfx", luaopen_gothgfx, 1);
 *
 * work.
 */
int luaopen_gothgfx(lua_State *state);

/* LVGL bridge: require("gothlv").open(display) */
int luaopen_gothlv(lua_State *state);

#ifdef __cplusplus
}
#endif

#endif /* GOTHGFX_H */
