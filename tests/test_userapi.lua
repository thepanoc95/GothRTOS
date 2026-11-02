-- Exercises the GothRTOS userland Lua binding (the "goth" table) inside a
-- real kernel-backed Lua state.

assert(type(goth) == "table", "goth table missing")
assert(goth.name == "GothRTOS")

assert(type(goth_puts) == "function")
assert(type(goth_write) == "function")
assert(type(goth_read) == "function")
assert(type(goth_exit) == "function")
assert(type(goth_ticks) == "function")

assert(goth.write("userapi.lua: console write ok\n") == true)

assert(goth.ticks() >= 0)
assert(goth.timeNs() >= 0)

-- Identity
assert(goth.isRoot() == true)
local root = goth.currentUser()
assert(root.name == "root")
assert(root.uid == 0)
assert(goth.getUid() == 0)
assert(goth.getGid() == 0)
assert(goth.getUser("missing") == nil)

assert(goth.addUser("goth", 1000, 1000, "/home/goth") == true)
assert(goth.addUser("goth", 1000, 1000) == false)
assert(goth.setUser("goth") == true)
assert(goth.isRoot() == false)
assert(goth.currentUser().name == "goth")
assert(goth.setUser("root") == true)
assert(goth.removeUser("goth") == true)
assert(goth.removeUser("goth") == false)

-- Environment
assert(goth.setEnv("FOO", "bar") == true)
assert(goth.getEnv("FOO") == "bar")
assert(goth.setEnv("FOO", nil) == true)
assert(goth.getEnv("FOO") == nil)

-- Process table
local first = goth.spawn("sh")
assert(type(first) == "number")
local second = goth.spawn("ls")
assert(second == first + 1)
assert(goth.getTask(first).program == "sh")
assert(goth.getTask(first).state == "ready")
assert(#goth.listTasks() == 2)
assert(goth.kill(first) == true)
assert(goth.getTask(first).state == "terminated")
assert(goth.reap(first) == true)
assert(goth.getTask(first) == nil)
assert(goth.reap(first) == false)
assert(goth.spawn("") == nil)

-- Endpoints (userland IPC)
local endpoint = goth.endpointCreate()
assert(type(endpoint) == "number")
assert(goth.endpointSend(endpoint, 7, 11, 13) == true)
local message = goth.endpointReceive(endpoint)
assert(message.type == 7)
assert(message.value0 == 11)
assert(message.value1 == 13)
assert(goth.endpointDestroy(endpoint) == true)

print("test_userapi: OK")
