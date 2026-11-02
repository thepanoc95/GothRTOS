-- Console I/O for the GothRTOS Lua userland (base/io).
--
-- Every call returns true on success, or (false, message) when the console
-- rejects the request.

local host = require("base/host")

local io_api = {}

function io_api.write(value)
    return host.write(tostring(value))
end

function io_api.puts(value)
    return host.write(tostring(value))
end

function io_api.println(value)
    return host.write(tostring(value or "") .. "\n")
end

function io_api.print(...)
    local parts = {}
    for index = 1, select("#", ...) do
        parts[index] = tostring((select(index, ...)))
    end
    return host.write(table.concat(parts))
end

function io_api.printf(format, ...)
    return host.write(string.format(format, ...))
end

function io_api.read()
    return host.read()
end

function io_api.clear()
    return host.write("\27[2J\27[H")
end

return io_api
