#include "gothrtos/kernlib.h"

#include "gothrtos/hdf.h"
#include "gothrtos/kernel.h"
#include "gothrtos/nanokernel.h"

#include "lauxlib.h"
#include "lua.h"

/* The "goth.kernel" table: the GothRTOS-specific view of the kernel for
 * Lua boot and diagnostic scripts. The table is nested under the "goth"
 * table published by gothlib.c, and every lookup also has a flat legacy
 * global so init scripts written before the nested table existed keep
 * working. */

static const char *kernel_status_message(goth_status_t status)
{
    switch (status) {
    case GOTH_STATUS_OK:
        return "ok";
    case GOTH_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case GOTH_STATUS_BAD_STATE:
        return "bad state";
    default:
        return "error";
    }
}

/* goth.kernel.state() -> state name */
static int goth_lua_kernel_state(lua_State *state)
{
    lua_pushstring(state, goth_nk_state_name(goth_nk_state()));
    return 1;
}

/* goth.kernel.inKernel() -> boolean (true while a syscall runs kernel code) */
static int goth_lua_kernel_in_kernel(lua_State *state)
{
    lua_pushboolean(state, goth_nk_in_kernel());
    return 1;
}

/* goth.kernel.check() -> ok, violations.
 *
 * Runs the nanokernel invariants. Returns true and 0 when the reinforced
 * architecture holds, or false and a table of violation messages. */
typedef struct {
    lua_State *state;
    int count;
} check_recorder_t;

static void record_violation(const char *invariant, void *context)
{
    check_recorder_t *recorder = (check_recorder_t *)context;

    lua_pushstring(recorder->state, invariant);
    lua_rawseti(recorder->state, -2, ++recorder->count);
}

static int goth_lua_kernel_check(lua_State *state)
{
    check_recorder_t recorder;
    size_t count;

    lua_createtable(state, 0, 0);
    recorder.state = state;
    recorder.count = 0;
    count = goth_nk_check_invariants(record_violation, &recorder);

    if (count == 0U) {
        lua_pop(state, 1);
        lua_pushboolean(state, 1);
        lua_pushinteger(state, 0);
        return 2;
    }
    lua_pushboolean(state, 0);
    lua_insert(state, -2);
    return 2;
}

/* goth.kernel.subsystems() -> array of subsystem records. */
static int goth_lua_kernel_subsystems(lua_State *state)
{
    size_t index;
    size_t count = goth_nk_subsystem_count();

    lua_createtable(state, (int)count, 0);
    for (index = 0U; index < count; index++) {
        const goth_nk_subsystem_t *entry = goth_nk_subsystem_at(index);

        lua_createtable(state, 0, 4);
        lua_pushstring(state, entry->name);
        lua_setfield(state, -2, "name");
        lua_pushstring(state, goth_nk_scope_name(entry->scope));
        lua_setfield(state, -2, "scope");
        lua_pushstring(state, entry->description);
        lua_setfield(state, -2, "description");
        lua_pushboolean(state, entry->static_allocation);
        lua_setfield(state, -2, "static");
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    return 1;
}

/* goth.kernel.scopeCount(scope) -> number of components in that scope. */
static int goth_lua_kernel_scope_count(lua_State *state)
{
    const char *scope = luaL_checkstring(state, 1);
    goth_nk_scope_t value;

    if (scope[0] == 'k') {
        value = GOTH_NK_SCOPE_KERNEL;
    } else if (scope[0] == 'p') {
        value = GOTH_NK_SCOPE_PLATFORM;
    } else if (scope[0] == 'u') {
        value = GOTH_NK_SCOPE_USER;
    } else {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, (lua_Integer)goth_nk_scope_count(value));
    return 1;
}

static void push_hdf_device(lua_State *state,
                            const goth_hdf_device_t *device)
{
    lua_createtable(state, 0, 8);
    lua_pushstring(state, device->name);
    lua_setfield(state, -2, "name");
    lua_pushstring(state, device->compatible);
    lua_setfield(state, -2, "compatible");
    lua_pushstring(state, goth_hdf_bus_name(device->bus));
    lua_setfield(state, -2, "bus");
    lua_pushstring(state, goth_hdf_class_name(device->class_id));
    lua_setfield(state, -2, "class");
    lua_pushstring(state, goth_hdf_state_name(device->state));
    lua_setfield(state, -2, "state");
    lua_pushstring(state, goth_hdf_power_name(device->power));
    lua_setfield(state, -2, "power");
    if (device->driver != NULL) {
        lua_pushstring(state, device->driver->name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "driver");
    if (device->mmio_count > 0U) {
        lua_pushinteger(state, (lua_Integer)device->mmio[0].base);
        lua_setfield(state, -2, "mmio");
    }
}

/* goth.kernel.hdf() -> array of device records. */
static int goth_lua_kernel_hdf(lua_State *state)
{
    size_t index;
    size_t count = goth_hdf_device_count();

    lua_createtable(state, (int)count, 0);
    for (index = 0U; index < count; index++) {
        goth_hdf_device_t *device = goth_hdf_device_at(index);

        if (device == NULL) {
            continue;
        }
        push_hdf_device(state, device);
        lua_rawseti(state, -2, (lua_Integer)index + 1);
    }
    return 1;
}

/* goth.kernel.hdfDevice(name) -> device record or nil. */
static int goth_lua_kernel_hdf_device(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);
    goth_hdf_device_t *device = goth_hdf_device_find(name);

    if (device == NULL) {
        lua_pushnil(state);
        return 1;
    }
    push_hdf_device(state, device);
    return 1;
}

/* goth.kernel.hdfStarted() -> number of started devices. */
static int goth_lua_kernel_hdf_started(lua_State *state)
{
    lua_pushinteger(state, (lua_Integer)goth_hdf_started_count());
    return 1;
}

/* goth.kernel.boot() -> table of captured boot parameters, or nil. */
static int goth_lua_kernel_boot(lua_State *state)
{
    goth_nk_boot_params_t params;

    if (!goth_nk_boot_params(&params)) {
        lua_pushnil(state);
        return 1;
    }
    lua_createtable(state, 0, 6);
    lua_pushstring(state, params.platform);
    lua_setfield(state, -2, "platform");
    lua_pushstring(state, params.architecture);
    lua_setfield(state, -2, "architecture");
    lua_pushinteger(state, (lua_Integer)params.pointer_width);
    lua_setfield(state, -2, "pointerWidth");
    lua_pushinteger(state, (lua_Integer)params.logical_cpus);
    lua_setfield(state, -2, "cpus");
    lua_pushstring(state, params.scheduler == GOTH_SCHED_PREEMPTIVE
                              ? "preemptive"
                              : "cooperative");
    lua_setfield(state, -2, "scheduler");
    lua_pushinteger(state, (lua_Integer)params.quantum);
    lua_setfield(state, -2, "quantum");
    return 1;
}

/* goth.kernel.setMode(state) -> true, or false, message.
 *
 * The nanokernel state machine is authoritative; this only lets a script ask
 * for a legal transition (for example a clean HALTED -> READY restart) and
 * reports the state machine's answer. */
static int goth_lua_kernel_set_state(lua_State *state)
{
    const char *name = luaL_checkstring(state, 1);
    goth_nk_state_t target;
    goth_status_t status;

    if (name[0] == 'e') {
        target = GOTH_NK_STATE_EARLY;
    } else if (name[0] == 'r' && name[1] == 'e') {
        target = GOTH_NK_STATE_READY;
    } else if (name[0] == 'r') {
        target = GOTH_NK_STATE_RUNNING;
    } else if (name[0] == 'h') {
        target = GOTH_NK_STATE_HALTED;
    } else {
        lua_pushboolean(state, 0);
        lua_pushstring(state, "unknown nanokernel state");
        return 2;
    }

    status = goth_nk_transition(target);
    if (status == GOTH_STATUS_OK) {
        lua_pushboolean(state, 1);
        return 1;
    }
    lua_pushboolean(state, 0);
    lua_pushstring(state, kernel_status_message(status));
    return 2;
}

static const luaL_Reg goth_kernel_functions[] = {
    {"state", goth_lua_kernel_state},
    {"setState", goth_lua_kernel_set_state},
    {"inKernel", goth_lua_kernel_in_kernel},
    {"check", goth_lua_kernel_check},
    {"subsystems", goth_lua_kernel_subsystems},
    {"scopeCount", goth_lua_kernel_scope_count},
    {"hdf", goth_lua_kernel_hdf},
    {"hdfDevice", goth_lua_kernel_hdf_device},
    {"hdfStarted", goth_lua_kernel_hdf_started},
    {"boot", goth_lua_kernel_boot},
    {NULL, NULL}
};

int goth_lua_open_kernel(lua_State *state)
{
    if (state == NULL) {
        return 1;
    }

    luaL_newlib(state, goth_kernel_functions);
    lua_pushstring(state, GOTH_NK_NAME);
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, (lua_Integer)GOTH_NK_ABI_VERSION);
    lua_setfield(state, -2, "abi");

    /* Attach to the existing goth table if gothlib already ran, otherwise
     * create it so early boot scripts can still reach the kernel view. */
    lua_getglobal(state, "goth");
    if (lua_istable(state, -1)) {
        lua_pushvalue(state, -2);
        lua_setfield(state, -2, "kernel");
        lua_pop(state, 1);
    } else {
        lua_pop(state, 1);
        lua_newtable(state);
        lua_pushvalue(state, -2);
        lua_setfield(state, -2, "kernel");
        lua_setglobal(state, "goth");
    }
    lua_pop(state, 1);

    lua_pushcfunction(state, goth_lua_kernel_state);
    lua_setglobal(state, "goth_kernel_state");
    lua_pushcfunction(state, goth_lua_kernel_subsystems);
    lua_setglobal(state, "goth_kernel_subsystems");
    lua_pushcfunction(state, goth_lua_kernel_hdf);
    lua_setglobal(state, "goth_hdf_devices");
    return 0;
}
