-- Application registry and launch helpers for the GothRTOS Lua userland.
--
-- An application is a named Lua entry point. It can be run in-process (in the
-- caller's task) with app.run, or recorded in the process table with app.spawn
-- so the shell and other tools can see it.

local proc = require("base/proc")

local app = {
    registry = {}
}

function app.register(name, entry)
    if type(name) ~= "string" or name == "" then
        return false, "invalid application name"
    end
    if type(entry) ~= "function" then
        return false, "application entry must be a function"
    end

    app.registry[name] = entry
    return true
end

function app.unregister(name)
    if app.registry[name] == nil then
        return false, "unknown application: " .. tostring(name)
    end
    app.registry[name] = nil
    return true
end

function app.lookup(name)
    return app.registry[name]
end

function app.list()
    local names = {}
    for name in pairs(app.registry) do
        names[#names + 1] = name
    end
    table.sort(names)
    return names
end

-- Runs a registered application synchronously and returns (ok, result).
function app.run(name, ...)
    local entry = app.registry[name]
    if entry == nil then
        return false, "unknown application: " .. tostring(name)
    end
    return pcall(entry, ...)
end

-- Records an application launch in the process table.
function app.spawn(name, arguments)
    if app.registry[name] == nil then
        return nil, "unknown application: " .. tostring(name)
    end
    return proc.spawn(name, arguments)
end

return app
