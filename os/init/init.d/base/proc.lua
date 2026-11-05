-- Process table for the GothRTOS Lua userland (base/proc).
--
-- A process is a named program plus its owner and state. There is no kernel
-- process object; the table is userland bookkeeping that the shell and other
-- tools read through base/proc.

local host = require("base/host")
local user = require("base/user")

local proc = {}

local state = {
    tasks = {},
    next_id = 1
}

function proc.reset()
    state.tasks = {}
    state.next_id = 1
    return true
end

function proc.spawn(program, arguments)
    if type(program) ~= "string" or program == "" then
        return nil, "invalid program name"
    end
    if arguments ~= nil and type(arguments) ~= "table" then
        return nil, "program arguments must be a table"
    end
    if host.bound then
        return goth.spawn(program)
    end

    local id = state.next_id
    state.next_id = id + 1
    state.tasks[id] = {
        id = id,
        program = program,
        arguments = arguments or {},
        owner = user.getUid() or 0,
        state = "ready",
        exit_code = 0
    }
    return id
end

function proc.getTask(id)
    if host.bound then
        return goth.getTask(id)
    end
    return state.tasks[id]
end

function proc.listTasks()
    if host.bound then
        return goth.listTasks()
    end

    local copy = {}
    local ids = {}
    for id in pairs(state.tasks) do
        ids[#ids + 1] = id
    end
    table.sort(ids)
    for _, id in ipairs(ids) do
        copy[#copy + 1] = state.tasks[id]
    end
    return copy
end

function proc.kill(id)
    if host.bound then
        return goth.kill(id)
    end
    local task = state.tasks[id]
    if task == nil then
        return false, "unknown task: " .. tostring(id)
    end

    task.state = "terminated"
    task.exit_code = task.exit_code or 0
    return true
end

function proc.reap(id)
    if host.bound then
        return goth.reap(id)
    end
    local task = state.tasks[id]
    if task == nil then
        return false, "unknown task: " .. tostring(id)
    end
    if task.state ~= "terminated" then
        return false, "task is still running: " .. tostring(id)
    end

    state.tasks[id] = nil
    return true
end

function proc.wait(id)
    if host.bound then
        local ok, result = goth.wait(id)
        if ok == true then
            return true, result
        end
        return false, result
    end

    local task = state.tasks[id]
    if task == nil then
        return false, "unknown task: " .. tostring(id)
    end
    if task.state ~= "terminated" then
        return false, "task is still running: " .. tostring(id)
    end
    return true, task.exit_code or 0
end

function proc.self()
    if host.bound then
        return goth.self()
    end
    return nil
end

function proc.count()
    local total = 0
    for _ in pairs(state.tasks) do
        total = total + 1
    end
    return total
end

return proc
