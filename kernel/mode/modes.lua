-- Kernel modes for the GothRTOS UserKernel.
--
-- A kernel mode is not the scheduler policy (that is base/sched). It is the
-- shape of the UserKernel: which subsystems stay in the nanokernel and which
-- are pushed out to userland, and therefore where the syscall boundary sits.
--
-- Each mode is a plain descriptor so it can be inspected, compared and
-- reported. `kernel` lists the subsystems that stay in the nanokernel; `user`
-- lists the ones delegated to userland. UserKernel:validate checks that the
-- two lists partition mode.ALL_SUBSYSTEMS exactly.
--
-- The four modes are:
--
--   monolithic        drivers, filesystem and services in the kernel;
--                     userland is thin. Fastest, least isolated.
--   hybrid            the nanokernel keeps scheduling, IPC, memory and the
--                     driver core; filesystem, console policy and services
--                     are userland. The default.
--   reversemonolithic everything except the nanokernel core (scheduling,
--                     IPC, memory, the syscall gate) is userland, drivers
--                     included. Smallest kernel, most isolation.
--   realtime          the hybrid layout with deterministic scheduling: no
--                     asynchronous preemption and a fixed quantum.

local modes = {}

modes.ALL_SUBSYSTEMS = {
    "scheduler", "ipc", "memory", "syscall", "klog", "vfs",
    "drivers", "console", "identity", "process", "services"
}

-- Subsystems that can never leave the nanokernel: without them there is no
-- kernel left to run userland.
modes.CORE = { "scheduler", "ipc", "memory", "syscall" }

local function descriptor(name, summary, kernel_list, user_list, options)
    local entry = {
        name = name,
        summary = summary,
        kernel = kernel_list,
        user = user_list,
        -- Whether userland reaches the kernel only through the syscall gate.
        syscall_gate = true,
        -- Whether the nanokernel preempts tasks at quantum boundaries.
        preempt = true,
        quantum = 1,
        deterministic = false
    }
    if options ~= nil then
        for key, value in pairs(options) do
            entry[key] = value
        end
    end
    return entry
end

modes.monolithic = function()
    return descriptor(
        "monolithic",
        "drivers, filesystem and services in the kernel; userland is thin",
        { "scheduler", "ipc", "memory", "syscall", "klog", "vfs",
          "drivers", "console", "identity", "process", "services" },
        {},
        { syscall_gate = false })
end

modes.hybrid = function()
    return descriptor(
        "hybrid",
        "nanokernel keeps scheduling, IPC, memory and the driver core; "
        .. "filesystem, console policy and services are userland",
        { "scheduler", "ipc", "memory", "syscall", "klog", "drivers" },
        { "vfs", "console", "identity", "process", "services" })
end

modes.reversemonolithic = function()
    return descriptor(
        "reversemonolithic",
        "everything except the nanokernel core is userland, drivers included",
        { "scheduler", "ipc", "memory", "syscall" },
        { "klog", "vfs", "drivers", "console", "identity", "process",
          "services" })
end

modes.realtime = function()
    return descriptor(
        "realtime",
        "hybrid layout with deterministic scheduling: no asynchronous "
        .. "preemption and a fixed quantum",
        { "scheduler", "ipc", "memory", "syscall", "klog", "drivers" },
        { "vfs", "console", "identity", "process", "services" },
        { preempt = false, quantum = 1, deterministic = true })
end

-- Aliases accepted by UserKernel:setMode.
modes.aliases = {
    ["reversed-monolithic"] = "reversemonolithic",
    ["reverse-monolithic"] = "reversemonolithic",
    ["reverse"] = "reversemonolithic",
    ["real-time"] = "realtime",
    ["rt"] = "realtime"
}

modes.names = { "monolithic", "hybrid", "reversemonolithic", "realtime" }

function modes.resolve(name)
    if type(name) ~= "string" then
        return nil
    end
    local key = name:lower()
    key = modes.aliases[key] or key
    if modes[key] ~= nil and type(modes[key]) == "function" then
        return modes[key]()
    end
    return nil
end

return modes
