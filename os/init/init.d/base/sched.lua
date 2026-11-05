-- Scheduler policy for the GothRTOS Lua userland (base/sched).
--
-- The nanokernel owns the real scheduler; this module is the userland-facing
-- policy surface. It forwards to the kernel binding when present and keeps a
-- small local state under the standalone interpreter so scripts remain
-- testable outside the kernel.

local host = require("base/host")

local sched = {}

local function normalize_mode(mode)
    if mode == "preemptive" or mode == "preempt" then
        return "preemptive"
    end
    if mode == "cooperative" or mode == "coop" then
        return "cooperative"
    end
    return nil
end

function sched.getMode()
    local mode = host.scheduler()
    return normalize_mode(mode) or "cooperative"
end

function sched.setMode(mode)
    local normalized = normalize_mode(mode)
    if normalized == nil then
        return false, "unknown scheduler mode: " .. tostring(mode)
    end
    return host.setScheduler(normalized)
end

function sched.setPreempt(enabled)
    return host.setPreempt(enabled == true)
end

function sched.isPreemptive()
    return sched.getMode() == "preemptive"
end

function sched.isCooperative()
    return sched.getMode() == "cooperative"
end

function sched.getQuantum()
    return host.getQuantum()
end

function sched.setQuantum(ticks)
    if type(ticks) ~= "number" or ticks < 1 or ticks % 1 ~= 0 then
        return false, "quantum must be a positive integer"
    end
    return host.setQuantum(ticks)
end

function sched.self()
    return host.self()
end

function sched.info()
    return host.sysinfo()
end

return sched
