-- Host bridge for the GothRTOS Lua userland.
--
-- The base/* modules are the GothRTOS OS API expressed in Lua. When the kernel
-- publishes its userland binding (the "goth" table built by os/init/gothlib.c)
-- these calls are forwarded to it, so scripts and the kernel share one set of
-- tables. Under the standalone interpreter, or any host without the binding,
-- each call falls back to a pure-Lua equivalent so the modules stay usable and
-- testable on their own.

local host = {}

local binding = nil
if type(goth) == "table" and type(goth.write) == "function" then
    binding = goth
end

host.bound = binding ~= nil

-- Standalone fallback scheduler state. In the kernel the authoritative
-- values live in the nanokernel and are read back through the binding.
local fallback_scheduler = "preemptive"
local fallback_quantum = 1

local function forward(name, ...)
    local fn = binding[name]
    if fn == nil then
        return nil
    end
    return fn(...)
end

-- Console --------------------------------------------------------------
function host.write(text)
    if binding then
        return forward("write", text)
    end
    io.write(text)
    return true
end

function host.read()
    if binding then
        return forward("read")
    end
    return io.read("*l")
end

-- Clock ----------------------------------------------------------------
function host.ticks()
    if binding then
        return forward("ticks") or 0
    end
    return math.floor(os.clock() * 1000)
end

function host.timeNs()
    if binding then
        return forward("timeNs") or 0
    end
    return math.floor(os.clock() * 1000000000)
end

-- Task control ---------------------------------------------------------
function host.sleep(ticks)
    if binding then
        return forward("sleep", ticks)
    end
    -- There is no scheduler outside the kernel, so report success without
    -- blocking the standalone interpreter.
    return true
end

function host.yield()
    if binding then
        return forward("yield")
    end
    return true
end

function host.exit(code)
    if binding then
        return forward("exit", code)
    end
    os.exit(code or 0)
end

-- Scheduler -------------------------------------------------------------
function host.scheduler()
    if binding then
        return forward("scheduler") or "cooperative"
    end
    return fallback_scheduler
end

function host.setScheduler(mode)
    if type(mode) ~= "string" then
        return false, "scheduler mode must be a string"
    end
    if binding then
        return forward("setScheduler", mode)
    end
    if mode ~= "preemptive" and mode ~= "cooperative" then
        return false, "unknown scheduler mode: " .. mode
    end
    fallback_scheduler = mode
    return true
end

function host.setPreempt(enabled)
    if type(enabled) ~= "boolean" then
        return false, "preemption flag must be a boolean"
    end
    if binding then
        return forward("setPreempt", enabled)
    end
    fallback_scheduler = enabled and "preemptive" or "cooperative"
    return true
end

function host.isPreempt()
    return host.scheduler() == "preemptive"
end

function host.getQuantum()
    if binding then
        return forward("getQuantum") or 1
    end
    return fallback_quantum
end

function host.setQuantum(ticks)
    if type(ticks) ~= "number" or ticks < 1 or ticks % 1 ~= 0 then
        return false, "quantum must be a positive integer"
    end
    if binding then
        return forward("setQuantum", ticks)
    end
    fallback_quantum = ticks
    return true
end

function host.self()
    if binding then
        return forward("self")
    end
    return nil
end

function host.sysinfo()
    if binding then
        return forward("sysinfo")
    end

    return {
        abi = 0,
        scheduler = fallback_scheduler,
        quantum = fallback_quantum,
        ticks = host.ticks(),
        tasks = 0,
        processes = 0,
        users = 0,
        endpoints = 0
    }
end

return host
