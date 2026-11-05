-- SysInject: runtime module injection for System.lua.
--
-- System.lua exposes a marked region:
--
--     -- @sysinject:begin
--     -- @sysinject:end
--
-- SysInject replaces everything between the markers with the source of the
-- requested modules. A module can request further modules by adding a line
--
--     -- @sysinject:require <name>
--
-- to System.lua, or the boot loader can pass extra modules explicitly.
-- Injection happens before the chunk is compiled, so injected code can add
-- methods and fields to the local `System` table exactly as if it had been
-- written in the file.

local SysInject = {
    begin_marker = "@sysinject:begin",
    end_marker = "@sysinject:end",
    require_marker = "@sysinject:require",
    name_pattern = "^[%w_%-%.]+$"
}

local function marker_pattern(marker)
    return "%-%-%s*" .. marker:gsub("%-", "%%-")
end

local function source_line_marker(source, marker, init)
    return source:find(marker_pattern(marker), init or 1)
end

-- Returns the byte offsets that delimit the injectable region. The content
-- starts after the begin marker's line and ends immediately before the end
-- marker's line.
function SysInject.region(source)
    if type(source) ~= "string" then
        return nil, "System.lua source must be a string"
    end

    local _, begin_end = source_line_marker(source, SysInject.begin_marker)
    if begin_end == nil then
        return nil, "missing " .. SysInject.begin_marker .. " marker"
    end

    local end_start = source_line_marker(source, SysInject.end_marker,
                                         begin_end + 1)
    if end_start == nil then
        return nil, "missing " .. SysInject.end_marker .. " marker"
    end

    local newline = source:find("\n", begin_end, true)
    if newline == nil or newline > end_start then
        return nil, "markers must be on separate lines"
    end

    return newline + 1, end_start
end

-- Collects -- @sysinject:require <name> declarations in source order and
-- removes duplicates.
function SysInject.required(source)
    local names = {}
    local seen = {}

    if type(source) ~= "string" then
        return names
    end

    local pattern = marker_pattern(SysInject.require_marker) .. "%s+([%w_%-%.]+)"
    for name in source:gmatch(pattern) do
        if not seen[name] then
            seen[name] = true
            names[#names + 1] = name
        end
    end
    return names
end

local function module_name_ok(name)
    return type(name) == "string" and name ~= "" and
        name:match(SysInject.name_pattern) ~= nil
end

-- Replaces the marked region with the supplied modules. `modules` is an
-- array of { name = string, source = string } records. The operation is
-- idempotent: injecting again replaces the previous generated section.
function SysInject.inject(source, modules)
    if type(source) ~= "string" then
        return nil, "System.lua source must be a string"
    end

    local content_start, content_end = SysInject.region(source)
    if content_start == nil then
        return nil, content_end
    end

    local generated = {
        "-- SysInject: generated System.lua module section.",
        "-- This region is replaced at boot; edit the module files instead."
    }

    for _, module in ipairs(modules or {}) do
        if not module_name_ok(module.name) then
            return nil, "invalid system module name: " .. tostring(module.name)
        end
        if type(module.source) ~= "string" then
            return nil, "system module has no source: " .. module.name
        end

        generated[#generated + 1] = ""
        generated[#generated + 1] = "-- >>> " .. module.name
        generated[#generated + 1] = module.source
        generated[#generated + 1] = "-- <<< " .. module.name
    end

    if #generated == 2 then
        generated[#generated + 1] = "-- No modules requested."
    end

    local replacement = table.concat(generated, "\n") .. "\n"
    return source:sub(1, content_start - 1) .. replacement ..
        source:sub(content_end)
end

-- Loads a System.lua source string, injecting required modules first.
--
-- options:
--   modules     optional array of additional module names
--   load_module function(name) -> source, error
--   chunk_name  name used in Lua error messages
--
-- Returns the System table, or nil plus an error message.
function SysInject.load(source, options)
    options = options or {}

    local loader = options.load_module
    if type(loader) ~= "function" then
        return nil, "no system module loader configured"
    end

    local requested = SysInject.required(source)
    local seen = {}
    for _, name in ipairs(requested) do
        seen[name] = true
    end
    for _, name in ipairs(options.modules or {}) do
        if not module_name_ok(name) then
            return nil, "invalid system module name: " .. tostring(name)
        end
        if not seen[name] then
            seen[name] = true
            requested[#requested + 1] = name
        end
    end

    local modules = {}
    for _, name in ipairs(requested) do
        local module_source, load_error = loader(name)
        if module_source == nil then
            return nil, "cannot load system module " .. name .. ": " ..
                tostring(load_error)
        end
        modules[#modules + 1] = { name = name, source = module_source }
    end

    local injected, inject_error = SysInject.inject(source, modules)
    if injected == nil then
        return nil, inject_error
    end

    local chunk, compile_error = load(injected,
                                      options.chunk_name or "=(System.lua)")
    if chunk == nil then
        return nil, compile_error
    end

    local ok, result = pcall(chunk)
    if not ok then
        return nil, result
    end
    if type(result) ~= "table" then
        return nil, "System.lua did not return a module table"
    end
    return result
end

return SysInject
