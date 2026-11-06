-- Exercises the GothRTOS UserKernel: kernel modes, profiles, services and the
-- SystemKit. Runs under the standalone interpreter and, when the kernel Lua
-- binding is present, inside build/kern.ldr too.

package.path = "kernel/?.lua;kernel/../../kernel/?.lua;" ..
    "os/init/init.d/?.lua;" .. package.path

local UserKernel = require("UserKernel")

-- Modes ----------------------------------------------------------------

local modes = UserKernel:listModes()
assert(#modes == 4)
local saw = {}
for _, name in ipairs(modes) do
    saw[name] = true
end
assert(saw.monolithic and saw.hybrid and saw.reversemonolithic and saw.realtime)

-- Every mode must be a valid partition of the subsystem set.
for _, name in ipairs(modes) do
    local ok, descriptor = UserKernel:setMode(name)
    assert(ok, "mode " .. name .. " rejected: " .. tostring(descriptor))
    assert(descriptor.name == name)
    assert(UserKernel:getMode().name == name)
end

-- Aliases resolve to the canonical modes.
assert(UserKernel:setMode("reversed-monolithic"))
assert(UserKernel:getMode().name == "reversemonolithic")
assert(UserKernel:setMode("rt"))
assert(UserKernel:getMode().name == "realtime")

-- An unknown mode is refused and leaves the current mode in place.
local before = UserKernel:getMode().name
assert(UserKernel:setMode("banana") == false)
assert(UserKernel:getMode().name == before)
assert(type(UserKernel:getError()) == "string")

-- The nanokernel core can never be delegated to userland.
local ok = UserKernel:validateMode({
    kernel = { "scheduler", "ipc", "memory" },
    user = { "syscall", "klog", "vfs", "drivers", "console", "identity",
             "process", "services" }
})
assert(ok == false)

-- A mode that leaves a subsystem unassigned is invalid.
ok = UserKernel:validateMode({
    kernel = { "scheduler", "ipc", "memory", "syscall" },
    user = { "klog", "vfs", "drivers", "console", "identity", "process" }
})
assert(ok == false)

-- Profiles -------------------------------------------------------------

local profiles = UserKernel:listProfiles()
assert(#profiles == 4)
for _, name in ipairs(profiles) do
    local okp, profile = UserKernel:applyProfile(name)
    assert(okp, "profile " .. name .. " rejected: " .. tostring(profile))
    assert(profile.name == name)
    assert(UserKernel:getProfile().name == name)
    -- A profile always selects a valid mode.
    assert(UserKernel:getMode() ~= nil)
end

-- The embedded profile is reverse-monolithic and cooperative.
assert(UserKernel:applyProfile("embedded"))
assert(UserKernel:getMode().name == "reversemonolithic")
assert(UserKernel:getProfile().scheduler == "cooperative")

-- A profile needing an unknown service is refused.
assert(UserKernel:applyProfile({ name = "broken", mode = "hybrid",
                                  scheduler = "preemptive",
                                  services = { "does-not-exist" } }) == false)

-- Services -------------------------------------------------------------

local services = UserKernel:listServices()
assert(#services >= 2)
assert(UserKernel:getService("versioning") ~= nil)
assert(UserKernel:getService("hardware-info") ~= nil)
assert(UserKernel:getService("nope") == nil)

-- Boot -----------------------------------------------------------------

-- The compact profile selects the hybrid mode.
assert(UserKernel:boot({ profile = "compact" }) == true)
assert(UserKernel:isBooted() == true)
assert(UserKernel:getMode().name == "hybrid")

-- Report ---------------------------------------------------------------

local report = UserKernel:report()
assert(report.mode == "hybrid")
assert(report.profile == "compact")
assert(type(report.kernelSubsystems) == "table")
assert(type(report.userSubsystems) == "table")
assert(type(report.services) == "table")

-- A mode can override the profile's choice.
local fresh = require("UserKernel")
assert(fresh:boot({ profile = "compact", mode = "monolithic" }) == true)
assert(fresh:getMode().name == "monolithic")
assert(fresh:report().mode == "monolithic")
-- In the kernel the nanokernel state must be visible.
local in_kernel = type(goth) == "table" and type(goth.kernel) == "table"
if in_kernel then
    assert(type(report.nanokernel) == "string")
end

-- SystemKit ------------------------------------------------------------

local kit = UserKernel:systemKit()
assert(kit:getVersion() == "0.0-indev")
assert(kit:getBuild() == "indev")
local summary = kit:summary()
assert(summary.name == "GothRTOS")
assert(summary.mode == UserKernel:getMode().name)
assert(type(summary.devices) == "number")
assert(type(kit:format()) == "string")

-- Versioning service.
local versioning = UserKernel:getService("versioning")
assert(versioning:getVersion() == "0.0-indev")
assert(type(versioning:banner()) == "string")

-- Hardware service: a well-formed (possibly empty) device list.
local hardware = UserKernel:getService("hardware-info")
assert(type(hardware:grab()) == "table")
local hw_summary = hardware:summary()
assert(type(hw_summary.total) == "number")
assert(hw_summary.total == #hardware:grab())

print("test_userkernel: OK")
