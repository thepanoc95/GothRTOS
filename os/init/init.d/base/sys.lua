-- Clock and task control for the GothRTOS Lua userland (base/sys).

local host = require("base/host")

local sys = {}

function sys.ticks()
    return host.ticks()
end

function sys.timeNs()
    return host.timeNs()
end

function sys.timeMs()
    return math.floor(host.timeNs() / 1000000)
end

function sys.sleep(ticks)
    if type(ticks) ~= "number" or ticks <= 0 then
        return false, "invalid tick count"
    end
    return host.sleep(math.floor(ticks))
end

function sys.yield()
    return host.yield()
end

function sys.exit(code)
    if code ~= nil and type(code) ~= "number" then
        return false, "exit code must be a number"
    end
    return host.exit(code or 0)
end

return sys
