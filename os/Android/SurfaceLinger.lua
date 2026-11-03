-- SurfaceLinger.lua -- keep a GothRTOS surface alive and watch for shutdown.
--
-- The Android supervisor owns the /system/sbin/stop and /system/sbin/start
-- calls, so the UI has no direct way to reboot Android.  When the user asks to
-- leave GothRTOS the UI creates a stop file (or calls gothdroid.requestStop()).
-- This module is the single place that knows how to test and create that
-- request, which keeps SystemUI.lua and future applications consistent.

local surface = {}

surface.__index = surface

local function file_exists(path)
    if type(path) ~= "string" or path == "" then
        return false
    end
    local handle = io.open(path, "rb")
    if handle == nil then
        return false
    end
    handle:close()
    return true
end

local function default_stop_file()
    if type(gothdroid) == "table" and type(gothdroid.stopFile) == "string" then
        return gothdroid.stopFile
    end
    return "/data/local/tmp/gothrtos/stop"
end

function surface.new(options)
    options = options or {}
    return setmetatable({
        stop_file = options.stop_file or default_stop_file(),
        poll_ms = options.poll_ms or 100,
        on_idle = options.on_idle,
        on_stop = options.on_stop,
        last_activity = 0,
        running = true,
    }, surface)
end

function surface:stopRequested()
    if type(gothdroid) == "table" and
       type(gothdroid.isStopping) == "function" and
       gothdroid.isStopping() then
        return true
    end
    return file_exists(self.stop_file)
end

function surface:requestStop()
    if type(gothdroid) == "table" and
       type(gothdroid.requestStop) == "function" then
        gothdroid.requestStop(self.stop_file)
        return true
    end
    if type(self.stop_file) ~= "string" or self.stop_file == "" then
        return false
    end
    local handle = io.open(self.stop_file, "wb")
    if handle == nil then
        return false
    end
    handle:write("stop\n")
    handle:close()
    return true
end

function surface:touch(now)
    self.last_activity = now or 0
end

-- Runs `draw(self)` at the requested rate until a stop is requested.  The
-- caller can supply its own loop instead; this helper exists so simple screens
-- (boot banners, shutdown notices) do not have to reimplement the polling.
function surface:linger(draw)
    while self.running and not self:stopRequested() do
        if draw ~= nil then
            draw(self)
        end
        if type(gothdroid) == "table" and type(gothdroid.sleep) == "function" then
            gothdroid.sleep(self.poll_ms)
        else
            os.execute("sleep " .. tostring(self.poll_ms / 1000))
        end
        if self.on_idle ~= nil then
            self.on_idle(self)
        end
    end
    if self.on_stop ~= nil then
        self.on_stop(self)
    end
    self.running = false
    return not self:stopRequested()
end

return surface
