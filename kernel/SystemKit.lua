-- GothRTOS SystemKit.
--
-- SystemKit is the UserKernel's view of the system as a whole: the version and
-- build identity, the host hardware, and the composition of the running kernel
-- (mode, profile, subsystems and services). It gathers what the various pieces
-- know and presents it as one object, so a shell or a diagnostic script has a
-- single place to ask "what am I running on?".
--
-- It reads the goth kernel binding when present and falls back to the
-- UserKernel's own state plus Lua's standard library otherwise, so it works
-- both inside the kernel and under the standalone interpreter.

local SystemKit = {}

SystemKit.__index = SystemKit

local VERSION = "0.0-indev"
local BUILD = "indev"

-- Reads the nanokernel boot parameters when the kernel binding is available.
local function kernel_view()
    if type(goth) ~= "table" or type(goth.kernel) ~= "table" then
        return nil
    end
    local view = { binding = goth.kernel }
    local ok, boot = pcall(goth.kernel.boot)
    if ok and type(boot) == "table" then
        view.boot = boot
    end
    local ok, state = pcall(goth.kernel.state)
    if ok and type(state) == "string" then
        view.state = state
    end
    local ok, subsystems = pcall(goth.kernel.subsystems)
    if ok and type(subsystems) == "table" then
        view.subsystems = subsystems
    end
    return view
end

-- Reads the HDF device table when the kernel binding is available.
local function hardware_view()
    if type(goth) ~= "table" or type(goth.kernel) ~= "table" then
        return { devices = {}, started = 0, source = "none" }
    end
    local ok, devices = pcall(goth.kernel.hdf)
    if not ok or type(devices) ~= "table" then
        return { devices = {}, started = 0, source = "none" }
    end
    local started = 0
    local ok_started, count = pcall(goth.kernel.hdfStarted)
    if ok_started and type(count) == "number" then
        started = count
    end
    return { devices = devices, started = started, source = "hdf" }
end

function SystemKit.new(userkernel)
    local kit = setmetatable({}, SystemKit)
    kit.userkernel = userkernel
    kit.version = VERSION
    kit.build = BUILD
    kit.kernel = kernel_view()
    kit.hardware = hardware_view()
    return kit
end

function SystemKit:getVersion()
    return self.version
end

function SystemKit:getBuild()
    return self.build
end

function SystemKit:getKernelState()
    if self.kernel ~= nil and self.kernel.state ~= nil then
        return self.kernel.state
    end
    return "unknown"
end

function SystemKit:getBoot()
    return self.kernel and self.kernel.boot or nil
end

function SystemKit:getSubsystems()
    return self.kernel and self.kernel.subsystems or {}
end

function SystemKit:getHardware()
    return self.hardware
end

-- A device by name, from the HDF view, or nil.
function SystemKit:getDevice(name)
    for _, device in ipairs(self.hardware.devices) do
        if device.name == name then
            return device
        end
    end
    return nil
end

-- A one-table summary suitable for printing or for a script to read.
function SystemKit:summary()
    local report = nil
    if self.userkernel ~= nil and type(self.userkernel.report) == "function" then
        report = self.userkernel:report()
    end

    return {
        name = "GothRTOS",
        version = self.version,
        build = self.build,
        kernelState = self:getKernelState(),
        mode = report and report.mode or nil,
        profile = report and report.profile or nil,
        scheduler = report and report.scheduler or nil,
        devices = #self.hardware.devices,
        startedDevices = self.hardware.started,
        hardwareSource = self.hardware.source
    }
end

function SystemKit:format()
    local info = self:summary()
    local lines = {
        "GothRTOS " .. tostring(info.version) ..
            " (" .. tostring(info.build) .. ")",
        "kernel state: " .. tostring(info.kernelState),
        "kernel mode: " .. tostring(info.mode),
        "profile: " .. tostring(info.profile),
        "scheduler: " .. tostring(info.scheduler),
        "devices: " .. tostring(info.devices) ..
            " (" .. tostring(info.startedDevices) .. " started)"
    }
    return table.concat(lines, "\n")
end

return SystemKit
