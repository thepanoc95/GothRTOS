#ifndef GOTHRTOS_KERNLIB_H
#define GOTHRTOS_KERNLIB_H

#include "lua.h"

/* GothRTOS-specific additions to the Lua interpreter that runs inside the
 * kernel (the state created by luaexec.c for -init scripts).
 *
 * The userapi binding (gothlib.c) exposes console, identity, processes and
 * endpoints: the parts of the OS that look like an operating system to a
 * script. This binding exposes the parts that make GothRTOS GothRTOS and that
 * a boot or diagnostic script has no other way to see:
 *
 *   goth.kernel.state()      -- the nanokernel lifecycle state
 *   goth.kernel.subsystems() -- the audited kernel/platform/user split
 *   goth.kernel.check()      -- run the reinforced-architecture invariants
 *   goth.kernel.hdf()        -- the Hardware Driver Foundation device table
 *   goth.kernel.boot()       -- the captured boot parameters
 *
 * It also publishes the flat legacy globals goth_kernel_state,
 * goth_kernel_subsystems and goth_hdf_devices that early boot scripts use
 * before the full goth table is available.
 *
 * Returns 0 on success and non-zero if the table could not be built. */
int goth_lua_open_kernel(lua_State *state);

#endif
