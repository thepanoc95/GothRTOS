-- Kernel profiles for the GothRTOS UserKernel.
--
-- A profile is a named system configuration: it selects a kernel mode, a
-- scheduler policy and the services the UserKernel loads. Modes describe the
-- shape of the kernel; profiles describe the build and deployment choices
-- layered on top of it.
--
-- A profile is a plain descriptor. UserKernel:applyProfile checks that the
-- profile's mode exists and that every service it names is registered before
-- it commits the change.

local profiles = {}

local function descriptor(name, summary, options)
    local entry = {
        name = name,
        summary = summary,
        mode = "hybrid",
        scheduler = "preemptive",
        quantum = 1,
        services = {},
        -- Extra kernel command-line style options the boot pipeline reads.
        options = {}
    }
    if options ~= nil then
        for key, value in pairs(options) do
            entry[key] = value
        end
    end
    return entry
end

profiles.embedded = function()
    return descriptor(
        "embedded",
        "small image for microcontrollers: reverse-monolithic layout, "
        .. "cooperative scheduling, no optional services",
        {
            mode = "reversemonolithic",
            scheduler = "cooperative",
            quantum = 1,
            services = { "versioning", "hardware-info" },
            options = { mem = "static", root = "ramfs" }
        })
end

profiles.compact = function()
    return descriptor(
        "compact",
        "lean image with the hybrid layout and the minimal service set",
        {
            mode = "hybrid",
            scheduler = "preemptive",
            quantum = 1,
            services = { "versioning" },
            options = { mem = "static" }
        })
end

profiles.desktop = function()
    return descriptor(
        "desktop",
        "full userland: hybrid layout, preemptive scheduling, all services",
        {
            mode = "hybrid",
            scheduler = "preemptive",
            quantum = 1,
            services = { "versioning", "hardware-info" },
            options = { mem = "dynamic", root = "hostfs" }
        })
end

profiles.mobile = function()
    return descriptor(
        "mobile",
        "battery-oriented: hybrid layout, cooperative scheduling to reduce "
        .. "wakeups, all services",
        {
            mode = "hybrid",
            scheduler = "cooperative",
            quantum = 2,
            services = { "versioning", "hardware-info" },
            options = { mem = "static" }
        })
end

profiles.names = { "embedded", "compact", "desktop", "mobile" }

function profiles.resolve(name)
    if type(name) ~= "string" then
        return nil
    end
    local key = name:lower()
    if profiles[key] ~= nil and type(profiles[key]) == "function" then
        return profiles[key]()
    end
    return nil
end

return profiles
