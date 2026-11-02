-- GothRTOS userland API.
--
-- This is the module the boot script and the shell talk to. It is a thin
-- composition over the base/* OS API modules: identity and environment live in
-- base/user, the process table in base/proc, endpoints in base/ipc, the clock
-- in base/sys and the console in base/io. Those modules forward to the kernel
-- binding when it is present and fall back to pure Lua otherwise, so this
-- module behaves the same in the kernel and under the standalone interpreter.
--
-- Calls that can fail return true or (false, message); the failing call is also
-- recorded and readable through :getError(). Lookups return nil when nothing
-- matches.

local io_api = require("base/io")
local sys = require("base/sys")
local user = require("base/user")
local proc = require("base/proc")
local ipc = require("base/ipc")

local userapi = {
    error = nil,
    io = io_api,
    sys = sys,
    user = user,
    proc = proc,
    ipc = ipc
}

local function record(self, ok, message)
    if ok == false then
        self.error = message or "operation failed"
    else
        self.error = nil
    end
    return ok, message
end

function userapi:getError()
    return self.error
end

function userapi:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

-- Console ------------------------------------------------------------------
function userapi:write(value)
    return record(self, io_api.write(value))
end

function userapi:puts(value)
    return record(self, io_api.puts(value))
end

function userapi:println(value)
    return record(self, io_api.println(value))
end

function userapi:read()
    return io_api.read()
end

-- Clock and task control ---------------------------------------------------
function userapi:ticks()
    return sys.ticks()
end

function userapi:timeNs()
    return sys.timeNs()
end

function userapi:timeMs()
    return sys.timeMs()
end

function userapi:sleep(ticks)
    return record(self, sys.sleep(ticks))
end

function userapi:yield()
    return record(self, sys.yield())
end

function userapi:exit(code)
    return record(self, sys.exit(code))
end

-- Identity and environment -------------------------------------------------
function userapi:currentUser()
    return user.currentUser()
end

function userapi:getUser(name)
    return user.getUser(name)
end

function userapi:setUser(name)
    return record(self, user.setUser(name))
end

function userapi:addUser(name, uid, gid, home)
    return record(self, user.addUser(name, uid, gid, home))
end

function userapi:removeUser(name)
    return record(self, user.removeUser(name))
end

function userapi:isRoot()
    return user.isRoot()
end

function userapi:getUid()
    return user.getUid()
end

function userapi:getGid()
    return user.getGid()
end

function userapi:getEnv(key)
    return user.getEnv(key)
end

function userapi:setEnv(key, value)
    return record(self, user.setEnv(key, value))
end

-- Process table ------------------------------------------------------------
function userapi:spawn(program, arguments)
    local id, message = proc.spawn(program, arguments)
    if id == nil then
        self.error = message or "spawn failed"
    else
        self.error = nil
    end
    return id, message
end

function userapi:getTask(id)
    return proc.getTask(id)
end

function userapi:listTasks()
    return proc.listTasks()
end

function userapi:kill(id)
    return record(self, proc.kill(id))
end

function userapi:reap(id)
    return record(self, proc.reap(id))
end

-- Userland IPC -------------------------------------------------------------
function userapi:endpointCreate()
    local id, message = ipc.create()
    if id == nil then
        self.error = message or "could not create endpoint"
    else
        self.error = nil
    end
    return id, message
end

function userapi:endpointDestroy(id)
    return record(self, ipc.destroy(id))
end

function userapi:endpointSend(id, message_type, value0, value1)
    return record(self, ipc.send(id, message_type, value0, value1))
end

function userapi:endpointReceive(id)
    return ipc.receive(id)
end

return userapi
