#ifndef GOTHGFX_LVGL_H
#define GOTHGFX_LVGL_H

#include "gothgfx.h"

/* Lua module that exposes LVGL widgets through a GothGfxDisplay surface. */
int luaopen_gothlv(lua_State *state);

#endif
