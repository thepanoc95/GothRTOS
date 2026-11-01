-- Logical mount table used by the GothRTOS boot pipeline.
--
-- The user-mode kernel has no block-device layer yet, so mounts recorded here
-- describe the intended filesystem layout. They do not touch the host
-- filesystem; kernelmode.lua provides host-backed reads for the kernel tree.

local mounts = {
    entries = {},
    pending = {},
    mounted = false,
    error = nil
}

local function valid_target(target)
    return type(target) == "string" and target:sub(1, 1) == "/"
end

local function find_entry(entries, target)
    for index, entry in ipairs(entries) do
        if entry.target == target then
            return entry, index
        end
    end
    return nil, nil
end

function mounts:reset()
    self.entries = {}
    self.pending = {}
    self.mounted = false
    self.error = nil
    return true
end

function mounts:declare(source, target, fstype, options)
    if type(source) ~= "string" or source == "" then
        return false, self:setError("invalid mount source")
    end
    if not valid_target(target) then
        return false, self:setError("mount target must be absolute: " ..
                                   tostring(target))
    end
    if type(fstype) ~= "string" or fstype == "" then
        fstype = "unknown"
    end
    if options ~= nil and type(options) ~= "table" then
        return false, self:setError("mount options must be a table")
    end

    self.pending[#self.pending + 1] = {
        source = source,
        target = target,
        fstype = fstype,
        options = options or {}
    }
    return true
end

function mounts:mount(source, target, fstype, options)
    if type(source) ~= "string" or source == "" then
        return false, self:setError("invalid mount source")
    end
    if not valid_target(target) then
        return false, self:setError("mount target must be absolute: " ..
                                   tostring(target))
    end
    if find_entry(self.entries, target) ~= nil then
        return false, self:setError("target already mounted: " .. target)
    end
    if options ~= nil and type(options) ~= "table" then
        return false, self:setError("mount options must be a table")
    end

    local entry = {
        source = source,
        target = target,
        fstype = (type(fstype) == "string" and fstype ~= "") and
            fstype or "unknown",
        options = options or {},
        mounted = true
    }

    self.entries[#self.entries + 1] = entry
    self.mounted = true
    self.error = nil
    return true
end

function mounts:mountAll()
    local staged = self.pending
    self.pending = {}

    local seen = {}
    for _, entry in ipairs(self.entries) do
        seen[entry.target] = true
    end
    for _, entry in ipairs(staged) do
        if seen[entry.target] then
            self.pending = staged
            return false, self:setError("target already mounted: " ..
                                       entry.target)
        end
        seen[entry.target] = true
    end

    for _, entry in ipairs(staged) do
        entry.mounted = true
        self.entries[#self.entries + 1] = entry
    end

    self.mounted = true
    self.error = nil
    return true
end

function mounts:unmount(target)
    local _, index = find_entry(self.entries, target)
    if index == nil then
        return false, self:setError("target is not mounted: " .. tostring(target))
    end

    table.remove(self.entries, index)
    self.mounted = #self.entries > 0
    self.error = nil
    return true
end

function mounts:isMounted(target)
    return find_entry(self.entries, target) ~= nil
end

function mounts:find(value)
    if type(value) ~= "string" or value == "" then
        return nil
    end

    local best = nil
    for _, entry in ipairs(self.entries) do
        local matches = entry.target == "/" or value == entry.target or
            value:sub(1, #entry.target + 1) == entry.target .. "/"
        if matches and (best == nil or #entry.target > #best.target) then
            best = entry
        end
    end
    return best
end

function mounts:list()
    local copy = {}
    for index, entry in ipairs(self.entries) do
        copy[index] = entry
    end
    return copy
end

function mounts:getError()
    return self.error
end

function mounts:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

return mounts
