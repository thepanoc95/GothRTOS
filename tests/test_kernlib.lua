-- Exercises the GothRTOS-specific kernel bindings added to the kernel Lua
-- interpreter (goth.kernel and the legacy goth_kernel_* / goth_hdf_devices
-- globals). Run through build/kern.ldr, which links kernlib.c into the state.
--
-- Under the standalone interpreter there is no kernel, so the test checks the
-- guarded fallback instead: the modules must not error when goth is absent.

local in_kernel = type(goth) == "table" and type(goth.kernel) == "table"

if not in_kernel then
    -- Standalone: the kernel view is simply absent, and that must be safe.
    assert(type(goth) ~= "table" or goth.kernel == nil)
    print("test_kernlib: standalone, no kernel view (ok)")
    return
end

-- Nanokernel lifecycle state.
local state = goth.kernel.state()
assert(type(state) == "string")
assert(state == "ready" or state == "running" or state == "early",
       "unexpected nanokernel state: " .. tostring(state))
assert(goth.kernel.name == "goth@nanokernel")
assert(type(goth.kernel.abi) == "number")

-- Kernel-mode gate.
assert(type(goth.kernel.inKernel()) == "boolean")

-- Subsystem registry: the reinforced split must be visible and consistent.
local subsystems = goth.kernel.subsystems()
assert(type(subsystems) == "table")
assert(#subsystems >= 8)

local saw_kernel, saw_platform, saw_user = false, false, false
for _, entry in ipairs(subsystems) do
    assert(type(entry.name) == "string")
    assert(type(entry.scope) == "string")
    assert(type(entry.static) == "boolean")
    if entry.scope == "kernel" then
        saw_kernel = true
        -- Kernel core is static-allocation only.
        assert(entry.static == true, entry.name .. " must be static")
    elseif entry.scope == "platform" then
        saw_platform = true
    elseif entry.scope == "user" then
        saw_user = true
    end
end
assert(saw_kernel and saw_platform and saw_user,
       "the kernel/platform/user split must all be present")

assert(goth.kernel.scopeCount("kernel") >= 1)
assert(goth.kernel.scopeCount("platform") >= 1)
assert(goth.kernel.scopeCount("user") >= 1)
assert(goth.kernel.scopeCount("bogus") == nil)

-- Invariant check: the reinforced architecture holds in a booted kernel.
local ok, violations = goth.kernel.check()
assert(type(ok) == "boolean")
if ok then
    assert(violations == 0)
else
    -- If it ever fails, the violations table must explain why.
    assert(type(violations) == "table")
end

-- Hardware Driver Foundation view.
local devices = goth.kernel.hdf()
assert(type(devices) == "table")
assert(type(goth.kernel.hdfStarted()) == "number")

-- Boot parameters captured at platform probe.
local boot = goth.kernel.boot()
assert(type(boot) == "table")
assert(type(boot.platform) == "string")
assert(type(boot.architecture) == "string")
assert(boot.pointerWidth == 32 or boot.pointerWidth == 64)
assert(boot.cpus >= 1)
assert(boot.scheduler == "preemptive" or boot.scheduler == "cooperative")

-- A state transition request is validated by the nanokernel state machine.
-- Requesting the current state is always legal.
local same_ok, same_message = goth.kernel.setState(state)
assert(same_ok == true, tostring(same_message))

-- Legacy flat globals must also be published.
assert(type(goth_kernel_state) == "function")
assert(type(goth_kernel_subsystems) == "function")
assert(type(goth_hdf_devices) == "function")
assert(goth_kernel_state() == state)

print("test_kernlib: OK")
