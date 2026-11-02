-- GothRTOS standard userland library.
--
-- Re-exports the base/* OS API modules under one table, so boot scripts can
-- `local std = require("base/std")` and reach console, clock, identity,
-- process and IPC calls from a single import. The individual modules remain
-- available for callers that prefer them.

local io_api = require("base/io")
local sys = require("base/sys")
local user = require("base/user")
local proc = require("base/proc")
local ipc = require("base/ipc")

local std = {
    io = io_api,
    sys = sys,
    user = user,
    proc = proc,
    ipc = ipc
}

-- Console.
std.write = io_api.write
std.puts = io_api.puts
std.println = io_api.println
std.print = io_api.print
std.printf = io_api.printf
std.read = io_api.read
std.clear = io_api.clear

-- Clock and task control.
std.ticks = sys.ticks
std.timeNs = sys.timeNs
std.timeMs = sys.timeMs
std.sleep = sys.sleep
std.yield = sys.yield
std.exit = sys.exit

-- Identity and environment.
std.currentUser = user.currentUser
std.getUser = user.getUser
std.setUser = user.setUser
std.isRoot = user.isRoot
std.getUid = user.getUid
std.getGid = user.getGid
std.getEnv = user.getEnv
std.setEnv = user.setEnv

-- Process table.
std.spawn = proc.spawn
std.getTask = proc.getTask
std.listTasks = proc.listTasks
std.kill = proc.kill
std.reap = proc.reap

-- Userland IPC.
std.endpointCreate = ipc.create
std.endpointDestroy = ipc.destroy
std.endpointSend = ipc.send
std.endpointReceive = ipc.receive

-- Filesystem helpers backed by the host's io library.
function std.exists(path)
    if type(path) ~= "string" or path == "" then
        return false
    end

    local handle = io.open(path, "r")
    if handle == nil then
        return false
    end
    handle:close()
    return true
end

function std.is_dir(path)
    if type(path) ~= "string" or path == "" then
        return false
    end

    local handle = io.open(path, "r")
    if handle == nil then
        return false
    end

    -- Reading a directory fails on the host, while reading a regular file
    -- returns data (or a clean end-of-file). That difference distinguishes
    -- the two without a dedicated stat call.
    local data, read_error = handle:read(1)
    handle:close()
    return data == nil and read_error ~= nil
end

return std
