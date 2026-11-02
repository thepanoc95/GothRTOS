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

return host
