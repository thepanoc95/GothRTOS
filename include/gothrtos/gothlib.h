#ifndef GOTHRTOS_GOTHLIB_H
#define GOTHRTOS_GOTHLIB_H

#include "lua.h"

/* Publishes the GothRTOS userland API to a Lua state.
 *
 * On success the state gains a global "goth" table (camelCase methods such as
 * goth.write, goth.read, goth.spawn and goth.currentUser) plus the flat legacy
 * globals goth_puts, goth_write, goth_read, goth_exit, goth_sleep,
 * goth_yield and goth_ticks that the boot scripts rely on.
 *
 * Returns 0 on success and a non-zero value if the table could not be built. */
int goth_lua_open_userapi(lua_State *state);

#endif
