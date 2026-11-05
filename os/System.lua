-- GothRTOS system state module.

local System = {
    version = "0.0-indev",
    build = "indev",
    booted = false,
    error = nil,
    root = "/",

    -- Scheduler policy read by the boot pipeline. PREEMPT=False selects
    -- cooperative multitasking; true (the default) selects preemptive
    -- scheduling with the quantum below.
    PREEMPT = true,
    quantum = 1
}

function System:getVersion()
    return self.version
end

function System:getBuild()
    return self.build
end

function System:getRoot()
    return self.root
end

function System:setRoot(path)
    if type(path) ~= "string" or path == "" then
        return false, "invalid root path"
    end
    self.root = path
    return true
end

function System:getError()
    return self.error
end

function System:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

function System:isBooted()
    return self.booted == true
end

function System:isPreemptive()
    return self.PREEMPT ~= false
end

function System:getQuantum()
    return self.quantum or 1
end

function System:boot()
    if self.booted then
        return true
    end

    if self.root == nil or self.root == "" then
        self:setError("no root filesystem configured")
        return false
    end

    self.booted = true
    self.error = nil
    return true
end

function System:halt(message)
    self.booted = false
    if message ~= nil then
        self:setError(message)
    end
    return true
end


-- System modules ---------------------------------------------------------
-- SysInject replaces the region between these markers with module source
-- before System.lua is compiled. A module can append its own name/record to
-- `modules` and can extend the System table directly.

local modules = {}

-- @sysinject:require scheduler

-- @sysinject:begin
-- @sysinject:end

System.modules = modules

return System
