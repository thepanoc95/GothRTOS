-- GothRTOS UserKernel.
--
-- The UserKernel is the userspace part of the kernel: the policy layer that
-- sits on top of the nanokernel and decides how the rest of the system is
-- arranged. The nanokernel schedules tasks, moves messages and guards memory;
-- the UserKernel chooses a kernel mode (how far the kernel reaches into
-- userland), a profile (a named configuration), and the services to run.
--
-- Nothing here is a C kernel object. It is Lua policy, which is the point:
-- the kernel stays a nanokernel and the shape of the OS is configurable at
-- boot and at runtime, within what the nanokernel permits. Where the goth
-- kernel binding is present, requests are validated against the nanokernel
-- (the state machine and the subsystem registry); under the standalone
-- interpreter the UserKernel runs on its own so it stays testable.

local here = (function()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/[^/]+$") or "."
end)()

package.path = here .. "/?.lua;" .. package.path

local modes = require("mode/modes")
local profiles = require("profile/profiles")
local SystemKit = require("SystemKit")

local UserKernel = {}

-- Kernel binding, when running inside the kernel (os/init/kernlib.c).
local kernel_binding = nil
if type(goth) == "table" and type(goth.kernel) == "table" then
    kernel_binding = goth.kernel
end

-- Scheduler policy bridge (base/sched), optional: the UserKernel applies the
-- profile's scheduler choice through it when the userland API is loaded.
local sched = nil
pcall(function()
    sched = require("base/sched")
end)

UserKernel.state = {
    mode = nil,
    profile = nil,
    services = {},
    booted = false,
    error = nil
}

-- Registers the built-in services. Services are small userland components the
-- UserKernel can load; each is a table with a name and optional start/stop.
local function builtin_services()
    return {
        versioning = require("services/SystemVersioning"),
        ["hardware-info"] = require("services/SystemHardwareInformationGrabber")
    }
end

UserKernel.services = builtin_services()

function UserKernel:getError()
    return self.state.error
end

function UserKernel:setError(message)
    self.state.error = message ~= nil and tostring(message) or nil
    return self.state.error
end

-- Mode -----------------------------------------------------------------

function UserKernel:getMode()
    return self.state.mode
end

function UserKernel:listModes()
    local copy = {}
    for index, name in ipairs(modes.names) do
        copy[index] = name
    end
    return copy
end

-- Checks that a mode descriptor partitions the subsystem set and keeps the
-- nanokernel core intact.
function UserKernel:validateMode(descriptor)
    if type(descriptor) ~= "table" then
        return false, "mode descriptor must be a table"
    end

    local seen = {}
    local function claim(list, where)
        for _, name in ipairs(list) do
            if seen[name] ~= nil then
                return false, "subsystem '" .. name .. "' appears in both " ..
                       seen[name] .. " and " .. where
            end
            seen[name] = where
        end
        return true
    end

    local ok, message = claim(descriptor.kernel or {}, "kernel")
    if not ok then
        return false, message
    end
    ok, message = claim(descriptor.user or {}, "user")
    if not ok then
        return false, message
    end

    -- Every subsystem must be accounted for.
    for _, name in ipairs(modes.ALL_SUBSYSTEMS) do
        if seen[name] == nil then
            return false, "subsystem '" .. name .. "' is unassigned"
        end
    end

    -- The nanokernel core can never be delegated.
    for _, name in ipairs(modes.CORE) do
        if seen[name] ~= "kernel" then
            return false, "nanokernel core subsystem '" .. name ..
                   "' cannot be userland"
        end
    end
    return true
end

function UserKernel:setMode(name)
    local descriptor = modes.resolve(name)
    if descriptor == nil then
        return false, self:setError("unknown kernel mode: " .. tostring(name))
    end

    local ok, message = self:validateMode(descriptor)
    if not ok then
        return false, self:setError("invalid mode '" .. descriptor.name ..
                                    "': " .. message)
    end

    self.state.mode = descriptor
    self:setError(nil)
    return true, descriptor
end

-- Profile --------------------------------------------------------------

function UserKernel:getProfile()
    return self.state.profile
end

function UserKernel:listProfiles()
    local copy = {}
    for index, name in ipairs(profiles.names) do
        copy[index] = name
    end
    return copy
end

-- Applies a profile: selects its mode, applies its scheduler policy and
-- records the services it asks for. Fails without committing anything if the
-- mode is unknown or a service is not registered.
function UserKernel:applyProfile(name)
    local profile = type(name) == "table" and name or profiles.resolve(name)
    if profile == nil then
        return false, self:setError("unknown kernel profile: " ..
                                    tostring(name))
    end

    local previous_mode = self.state.mode
    local previous_profile = self.state.profile

    local ok, message = self:setMode(profile.mode)
    if not ok then
        self.state.mode = previous_mode
        return false, self:setError(message)
    end

    for _, service in ipairs(profile.services or {}) do
        if self.services[service] == nil then
            self.state.mode = previous_mode
            self.state.profile = previous_profile
            return false, self:setError("profile '" .. profile.name ..
                                        "' needs unknown service '" ..
                                        service .. "'")
        end
    end

    if sched ~= nil then
        -- The nanokernel owns the real scheduler; the profile's choice is
        -- advisory. Inside the kernel the init script is not a scheduled task
        -- yet, so the request may be refused -- that is not a profile failure.
        local sched_ok = sched.setMode(profile.scheduler)
        if sched_ok then
            sched.setQuantum(profile.quantum or 1)
        end
    end

    self.state.profile = profile
    self:setError(nil)
    return true, profile
end

-- Services -------------------------------------------------------------

function UserKernel:getService(name)
    return self.services[name]
end

function UserKernel:listServices()
    local names = {}
    for name in pairs(self.services) do
        names[#names + 1] = name
    end
    table.sort(names)
    return names
end

function UserKernel:startServices()
    local started = {}
    for _, name in ipairs(self.state.profile and self.state.profile.services
                          or {}) do
        local service = self.services[name]
        if service ~= nil then
            if type(service.start) == "function" then
                local ok, message = service:start(self)
                if not ok then
                    return false, self:setError("service '" .. name ..
                                                "' failed: " ..
                                                tostring(message))
                end
            end
            started[#started + 1] = name
        end
    end
    return true, started
end

-- Boot -----------------------------------------------------------------

-- Selects a mode and/or profile, starts their services and (when the goth
-- kernel binding is present) checks the reinforced-architecture invariants.
-- `options.profile` names a profile; `options.mode` names a mode and overrides
-- whatever the profile selected. With neither, the "compact" profile is used.
-- Returns true when the UserKernel is up.
function UserKernel:boot(options)
    options = options or {}

    local profile_name = options.profile
    if profile_name ~= nil and profiles.resolve(profile_name) == nil then
        return false, self:setError("unknown kernel profile: " ..
                                    tostring(profile_name))
    end

    local ok, message = self:applyProfile(profile_name or "compact")
    if not ok then
        return false, message
    end

    if options.mode ~= nil then
        ok, message = self:setMode(options.mode)
        if not ok then
            return false, message
        end
    end

    ok, message = self:startServices()
    if not ok then
        return false, message
    end

    if kernel_binding ~= nil and type(kernel_binding.check) == "function" then
        local clean, violations = kernel_binding.check()
        if clean ~= true then
            return false, self:setError(
                "nanokernel invariants failed: " ..
                tostring(violations and violations[1] or "unknown"))
        end
    end

    self.state.booted = true
    self:setError(nil)
    return true
end

function UserKernel:isBooted()
    return self.state.booted == true
end

-- Reports the current arrangement. `nanokernel` is filled from the kernel
-- binding when present, so a script can see what the kernel thinks it is.
function UserKernel:report()
    local mode = self.state.mode
    local profile = self.state.profile

    return {
        mode = mode and mode.name or nil,
        modeSummary = mode and mode.summary or nil,
        kernelSubsystems = mode and mode.kernel or {},
        userSubsystems = mode and mode.user or {},
        profile = profile and profile.name or nil,
        scheduler = profile and profile.scheduler or nil,
        quantum = profile and profile.quantum or nil,
        services = self:listServices(),
        nanokernel = kernel_binding and kernel_binding.state() or nil,
        booted = self.state.booted
    }
end

-- The SystemKit is the UserKernel's view of the system as a whole: version,
-- host hardware and the composition of the running kernel.
function UserKernel:systemKit()
    return SystemKit.new(self)
end

return UserKernel
