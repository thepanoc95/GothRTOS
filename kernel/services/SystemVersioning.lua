-- System versioning service for the GothRTOS UserKernel.
--
-- Reports the GothRTOS version and build, and (when the goth kernel binding is
-- present) the nanokernel ABI version, so a diagnostic can tell exactly which
-- kernel and which userland are talking to each other.

local SystemVersioning = {}

SystemVersioning.name = "versioning"

SystemVersioning.version = "0.0-indev"
SystemVersioning.build = "indev"

function SystemVersioning:start(userkernel)
    self.userkernel = userkernel
    self.started = true
    return true
end

function SystemVersioning:stop()
    self.started = false
    return true
end

function SystemVersioning:getVersion()
    return self.version
end

function SystemVersioning:getBuild()
    return self.build
end

-- Returns the nanokernel ABI version when the kernel binding is present.
function SystemVersioning:getKernelAbi()
    if type(goth) == "table" and type(goth.kernel) == "table" then
        return goth.kernel.abi
    end
    return nil
end

function SystemVersioning:banner()
    local abi = self:getKernelAbi()
    local suffix = abi ~= nil and (" (kernel ABI " .. tostring(abi) .. ")")
        or ""
    return "GothRTOS " .. self.version .. " " .. self.build .. suffix
end

return SystemVersioning
