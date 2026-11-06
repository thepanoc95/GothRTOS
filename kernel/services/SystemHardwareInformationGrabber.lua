-- System hardware information service for the GothRTOS UserKernel.
--
-- Collects the hardware description from the Hardware Driver Foundation (the
-- kernel device table exposed as goth.kernel.hdf) and presents it as a list
-- of devices plus a small summary. This is the userland view of the HDF: the
-- kernel owns the device model, and this service is how the rest of the
-- userland reads it.
--
-- When there is no HDF (the standalone interpreter, or a kernel built without
-- the device model) it reports an empty, well-formed table rather than
-- failing, so callers never have to special-case it.

local SystemHardwareInformationGrabber = {}

SystemHardwareInformationGrabber.name = "hardware-info"

function SystemHardwareInformationGrabber:start(userkernel)
    self.userkernel = userkernel
    self.started = true
    return true
end

function SystemHardwareInformationGrabber:stop()
    self.started = false
    return true
end

-- Returns a table of device records, each with name, bus, class, state,
-- power and driver. Empty when no HDF is reachable.
function SystemHardwareInformationGrabber:grab()
    if type(goth) ~= "table" or type(goth.kernel) ~= "table" or
       type(goth.kernel.hdf) ~= "function" then
        return {}
    end
    local ok, devices = pcall(goth.kernel.hdf)
    if not ok or type(devices) ~= "table" then
        return {}
    end
    return devices
end

function SystemHardwareInformationGrabber:find(name)
    for _, device in ipairs(self:grab()) do
        if device.name == name then
            return device
        end
    end
    return nil
end

-- Counts devices by class and by state for a quick overview.
function SystemHardwareInformationGrabber:summary()
    local by_class = {}
    local by_state = {}
    local total = 0
    local started = 0

    for _, device in ipairs(self:grab()) do
        total = total + 1
        by_class[device.class or "unknown"] =
            (by_class[device.class or "unknown"] or 0) + 1
        by_state[device.state or "unknown"] =
            (by_state[device.state or "unknown"] or 0) + 1
        if device.state == "started" then
            started = started + 1
        end
    end

    return {
        total = total,
        started = started,
        byClass = by_class,
        byState = by_state
    }
end

return SystemHardwareInformationGrabber
