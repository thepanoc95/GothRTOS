-- Userland message endpoints for the GothRTOS Lua userland (base/ipc).
--
-- Endpoints are the GothRTOS inter-task primitive. The kernel owns the
-- endpoint queues; this module hands out ids and forwards to the binding, or
-- keeps a pure-Lua queue when running outside the kernel.

local host = require("base/host")

local ipc = {}

local state = {
    endpoints = {},
    next_id = 1
}

function ipc.reset()
    state.endpoints = {}
    state.next_id = 1
    return true
end

function ipc.create()
    if host.bound then
        return goth.endpointCreate()
    end
    local id = state.next_id
    state.next_id = id + 1
    state.endpoints[id] = {}
    return id
end

function ipc.destroy(id)
    if host.bound then
        return goth.endpointDestroy(id)
    end
    if state.endpoints[id] == nil then
        return false, "unknown endpoint: " .. tostring(id)
    end
    state.endpoints[id] = nil
    return true
end

function ipc.send(id, message_type, value0, value1)
    if host.bound then
        return goth.endpointSend(id, message_type, value0, value1)
    end
    local queue = state.endpoints[id]
    if queue == nil then
        return false, "unknown endpoint: " .. tostring(id)
    end

    queue[#queue + 1] = {
        sender = 0,
        type = message_type,
        value0 = value0,
        value1 = value1
    }
    return true
end

function ipc.receive(id)
    if host.bound then
        return goth.endpointReceive(id)
    end
    local queue = state.endpoints[id]
    if queue == nil or #queue == 0 then
        return nil
    end
    return table.remove(queue, 1)
end

function ipc.poll(id)
    if host.bound then
        return goth.endpointPoll(id)
    end
    local queue = state.endpoints[id]
    if queue == nil or #queue == 0 then
        return nil
    end
    return queue[1]
end

function ipc.count(id)
    local queue = state.endpoints[id]
    return queue ~= nil and #queue or 0
end

return ipc
