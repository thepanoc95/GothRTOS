-- Exercises the GothRTOS Lua userland API (the base/* modules and the userapi
-- composition). Runs both under the standalone interpreter and inside the
-- kernel, where the base/* modules forward to the "goth" C binding.

package.path = "os/init/init.d/?.lua;" .. package.path

local io_api = require("base/io")
local sys = require("base/sys")
local user = require("base/user")
local proc = require("base/proc")
local ipc = require("base/ipc")
local app = require("base/app")
local std = require("base/std")

-- Console
assert(io_api.write("") == true)
assert(io_api.println("test_userland_api: console ok") == true)
assert(type(std.printf) == "function")

-- Clock and task control
local in_kernel = type(goth) == "table"
assert(type(sys.ticks()) == "number")
assert(sys.ticks() >= 0)
assert(sys.timeNs() >= 0)
assert(sys.timeMs() >= 0)
assert(sys.sleep(-1) == false)
assert(sys.sleep(0) == false)

-- The init script is not itself a scheduled task, so inside the kernel the
-- scheduler calls report (false, message) instead of succeeding. Outside the
-- kernel they are no-ops that succeed. Either way the call must not raise.
local sleep_ok, sleep_message = sys.sleep(1)
assert(type(sleep_ok) == "boolean")
if not in_kernel then
    assert(sleep_ok == true)
end
if sleep_ok == false then
    assert(type(sleep_message) == "string")
end

local yield_ok, yield_message = sys.yield()
assert(type(yield_ok) == "boolean")
if not in_kernel then
    assert(yield_ok == true)
end
if yield_ok == false then
    assert(type(yield_message) == "string")
end

-- Identity and environment
assert(user.isRoot() == true)
assert(user.currentUser().name == "root")
assert(user.getUid() == 0)
assert(user.getGid() == 0)
assert(user.getUser("missing") == nil)
assert(user.setUser("nobody") == false)
assert(user.addUser("goth", 1000, 1000, "/home/goth") == true)
assert(user.addUser("goth", 1000, 1000) == false)
assert(user.addUser("", 1, 1) == false)
assert(user.setUser("goth") == true)
assert(user.isRoot() == false)
assert(user.currentUser().name == "goth")
assert(user.getUid() == 1000)
assert(user.getEnv("USER") == "goth")
assert(user.setEnv("FOO", "bar") == true)
assert(user.getEnv("FOO") == "bar")
assert(user.setEnv("FOO", nil) == true)
assert(user.getEnv("FOO") == nil)
assert(user.setEnv("", "x") == false)
assert(user.setUser("root") == true)
assert(user.removeUser("goth") == true)
assert(user.removeUser("goth") == false)
assert(user.removeUser("root") == false)

-- Process table
local first = assert(proc.spawn("sh", { "/root" }))
assert(type(first) == "number")
assert(proc.spawn("") == nil)
assert(proc.spawn("ls", "not-a-table") == nil)
local second = assert(proc.spawn("ls"))
assert(second == first + 1)
assert(proc.getTask(first).program == "sh")
assert(proc.getTask(first).state == "ready")
assert(#proc.listTasks() == 2)
assert(proc.reap(first) == false)
assert(proc.kill(first) == true)
assert(proc.getTask(first).state == "terminated")
assert(proc.reap(first) == true)
assert(proc.getTask(first) == nil)
assert(proc.reap(first) == false)

-- Userland IPC
local endpoint = assert(ipc.create())
assert(type(endpoint) == "number")
assert(ipc.receive(endpoint) == nil)
assert(ipc.send(endpoint, 7, 11, 13) == true)
local message = ipc.receive(endpoint)
assert(message.type == 7)
assert(message.value0 == 11)
assert(message.value1 == 13)
assert(ipc.receive(endpoint) == nil)
assert(ipc.destroy(endpoint) == true)
assert(ipc.destroy(endpoint) == false)

-- Application registry
assert(app.register("hello", function(name) return "hi " .. name end) == true)
assert(app.register("bad", "not-a-function") == false)
assert(app.lookup("hello") ~= nil)
assert(app.run("hello", "goth") == true)
local ran_ok, ran_value = app.run("hello", "goth")
assert(ran_ok == true and ran_value == "hi goth")
assert(app.run("missing") == false)
local app_id = assert(app.spawn("hello"))
assert(proc.getTask(app_id).program == "hello")
assert(app.spawn("missing") == nil)
assert(app.list()[1] == "hello")
assert(app.unregister("hello") == true)
assert(app.unregister("hello") == false)

-- The userapi composition used by the boot script and the shell
local userapi = require("userapi")
assert(userapi:getError() == nil)
assert(userapi:isRoot() == true)
assert(userapi:setUser("nobody") == false)
assert(userapi:getError() ~= nil)
assert(userapi:addUser("goth", 1000, 1000) == true)
assert(userapi:setUser("goth") == true)
assert(userapi:getError() == nil)
assert(userapi:getUid() == 1000)
assert(userapi:setUser("root") == true)
assert(userapi:removeUser("goth") == true)

local task = assert(userapi:spawn("sh"))
assert(userapi:getTask(task).program == "sh")
assert(userapi:kill(task) == true)
assert(userapi:reap(task) == true)
assert(userapi:spawn("") == nil)

local ep = assert(userapi:endpointCreate())
assert(userapi:endpointSend(ep, 1, 2, 3) == true)
assert(userapi:endpointReceive(ep).type == 1)
assert(userapi:endpointDestroy(ep) == true)

assert(userapi:println("test_userland_api: OK") == true)
