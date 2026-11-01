-- Kernel filesystem bridge for the GothRTOS boot environment.
--
-- /kern is the kernel namespace seen by boot scripts. In the user-mode build
-- the namespace is backed by files in the source tree, so init scripts can use
-- target-style paths such as /kern/init.d/sys/System.lua.

local function module_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/[^/]+$") or "."
end

local here = module_directory()

local kernelmode = {
    root = "/kern",
    mounted = false,
    error = nil,
    namespaces = {}
}

function kernelmode:mountAll()
    self.mounted = true
    self.error = nil
    self.namespaces = {
        "/kern",
        "/kern/init.d",
        "/kern/init.d/sys",
        "/kern/builts"
    }
    return true
end

function kernelmode:isMounted()
    return self.mounted == true
end

function kernelmode:getMountPoint()
    return self.root
end

function kernelmode:list()
    local copy = {}
    for index, value in ipairs(self.namespaces) do
        copy[index] = value
    end
    return copy
end

function kernelmode:hostCandidates(value)
    local candidates = {}

    local function add(candidate)
        if candidate ~= nil and candidate ~= "" then
            candidates[#candidates + 1] = candidate
        end
    end

    if value == "/kern/init.d/sys/System.lua" then
        add(here .. "/../../System.lua")
    end

    local init_relative = value:match("^/kern/init%.d/(.+)$")
    if init_relative ~= nil then
        add(here .. "/" .. init_relative)
    end

    local relative = value:match("^/kern/(.+)$")
    if relative ~= nil then
        add("os/" .. relative)
        add(relative)
    end

    add(value)
    return candidates
end

function kernelmode:resolve(value)
    if type(value) ~= "string" or value == "" then
        return nil, self:setError("invalid kernel path")
    end
    if not self.mounted then
        return nil, self:setError("kernel filesystem is not mounted")
    end

    for _, candidate in ipairs(self:hostCandidates(value)) do
        local file = io.open(candidate, "r")
        if file ~= nil then
            file:close()
            self.error = nil
            return candidate
        end
    end

    return nil, self:setError("no such kernel file: " .. value)
end

function kernelmode:open(value, mode)
    if mode ~= nil and mode ~= "r" and mode ~= "rb" then
        return nil, self:setError("kernel filesystem is read-only")
    end
    if not self.mounted then
        return nil, self:setError("kernel filesystem is not mounted")
    end

    for _, candidate in ipairs(self:hostCandidates(value)) do
        local file = io.open(candidate, mode or "r")
        if file ~= nil then
            self.error = nil
            return file, candidate
        end
    end

    return nil, self:setError("no such kernel file: " .. tostring(value))
end

function kernelmode:readFile(value)
    local file, error_message = self:open(value, "r")
    if file == nil then
        return nil, error_message
    end

    local contents = file:read("*a")
    file:close()
    return contents
end

function kernelmode:exists(value)
    local file = self:open(value, "r")
    if file == nil then
        return false
    end
    file:close()
    return true
end

function kernelmode:getError()
    return self.error
end

function kernelmode:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

return kernelmode
