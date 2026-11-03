-- SystemUI.lua -- the GothRTOS user interface shown on /dev/graphics/fb0.
--
-- It is a plain Lua 5.5 script.  All display and touch work goes through the
-- libgothgfx C module, so the same screen can run on an Android phone, on a
-- desktop Linux machine with a framebuffer, or against an in-memory surface
-- for development.
--
-- The Android supervisor is responsible for /system/sbin/stop and
-- /system/sbin/start.  The "Reboot Android" action simply asks the runtime to
-- stop; the supervisor notices the exit and starts the Android framework
-- again.

local SystemUI = {}
SystemUI.__index = SystemUI
SystemUI.version = "1.0"

local ok_gfx, gfx = pcall(require, "gothgfx")
if not ok_gfx then
    error("SystemUI needs the gothgfx module: " .. tostring(gfx), 0)
end

local ok_linger, SurfaceLinger = pcall(require, "SurfaceLinger")
if not ok_linger then
    SurfaceLinger = nil
end

local DEFAULT_THEME = {
    background = 0x080b12,
    surface = 0x121826,
    surface_alt = 0x1b2335,
    accent = 0x7c5cff,
    accent_alt = 0x26d0ce,
    text = 0xf4f7ff,
    muted = 0x8c98b8,
    danger = 0xff5c7a,
    success = 0x59d98e,
}

local function clamp(value, minimum, maximum)
    if value < minimum then
        return minimum
    end
    if value > maximum then
        return maximum
    end
    return value
end

local function contains(button, x, y)
    return x >= button.x and y >= button.y and
           x < button.x + button.w and y < button.y + button.h
end

local function merge(destination, source)
    for key, value in pairs(source or {}) do
        if type(value) == "table" and type(destination[key]) == "table" then
            merge(destination[key], value)
        else
            destination[key] = value
        end
    end
    return destination
end

local function log_line(message)
    if type(gothdroid) == "table" and type(gothdroid.log) == "function" then
        gothdroid.log(message)
    else
        io.stderr:write("SystemUI: " .. tostring(message) .. "\n")
        io.stderr:flush()
    end
end

local function device_profile()
    -- Only the device named by --device (ro.product.device) is loaded.  There
    -- is deliberately no implicit fallback: without a profile gothgfx uses the
    -- framebuffer's own geometry, which is correct for unknown devices.
    local device = nil
    if type(gothdroid_options) == "table" then
        device = gothdroid_options.device
    end
    if type(device) ~= "string" or device == "" then
        device = os.getenv("GOTHDROID_DEVICE")
    end
    if type(device) ~= "string" or device == "" then
        return {}
    end
    local loaded, profile = pcall(require, "devices." .. device)
    if loaded and type(profile) == "table" then
        return profile
    end
    log_line("no profile for device " .. device)
    return {}
end

function SystemUI.new(options)
    options = options or {}
    local profile = device_profile()
    local display_options = merge(merge({}, profile.display or {}), {})

    if type(gothdroid_options) == "table" then
        merge(display_options, gothdroid_options)
    end
    if options.display then
        merge(display_options, options.display)
    end

    -- Device profiles keep touch information in a nested table.  gothgfx
    -- expects those keys at the top level, so flatten the handful it knows.
    local touch = display_options.touch
    if type(touch) == "table" then
        for _, key in ipairs({ "input", "swap_xy", "invert_x", "invert_y",
                               "x_min", "x_max", "y_min", "y_max" }) do
            if touch[key] ~= nil then
                display_options[key] = touch[key]
            end
        end
    end

    if type(display_options.input) == "table" and
       display_options.input[1] == nil then
        display_options.input = nil
    end

    local open_ok, display, open_error = pcall(gfx.open, display_options)
    if not open_ok then
        return nil, display
    end
    if display == nil then
        -- Development fallback: without a framebuffer there is still enough
        -- of the UI to inspect the layout and exercise the touch logic.
        if type(gothdroid) ~= "table" or os.getenv("GOTHDROID_MEMORY") then
            local width = display_options.width or 720
            local height = display_options.height or 1280
            display = assert(gfx.create(width, height,
                                        display_options.bpp or 32))
            log_line(string.format(
                "no framebuffer (%s); using %dx%d memory surface",
                tostring(open_error), width, height))
        else
            return nil, open_error
        end
    end

    local self = setmetatable({
        display = display,
        profile = profile,
        theme = merge(merge({}, DEFAULT_THEME), profile.theme or {}),
        screen = "home",
        buttons = {},
        pressed = nil,
        running = true,
        message = "",
        message_until = 0,
        log = {},
        rotation_index = 0,
        frames = 0,
        fps = 0,
        fps_time = gfx.now(),
        clock_time = 0,
        clock_text = "",
        date_text = "",
        start_time = gfx.now(),
        dirty = true,
    }, SystemUI)

    self.linger = SurfaceLinger and SurfaceLinger.new({
        stop_file = options.stop_file,
    }) or nil

    self:addLog("gothgfx " .. tostring(gfx.version))
    local info = display:info()
    local rotations = { 0, 90, 180, 270 }
    for index, rotation in ipairs(rotations) do
        if rotation == info.rotation then
            self.rotation_index = index
            break
        end
    end
    self:addLog(string.format("display %dx%d, %d bpp, stride %d",
                              info.logical_width, info.logical_height,
                              info.bpp, info.stride))
    log_line(string.format("display %dx%d, %d bpp, stride %d, rotation %d",
                           info.logical_width, info.logical_height, info.bpp,
                           info.stride, info.rotation))
    local inputs = display:inputs()
    self:addLog(string.format("%d touch device(s)", #inputs))
    for _, path in ipairs(inputs) do
        self:addLog("input " .. tostring(path))
    end
    log_line(string.format("%d touch device(s)", #inputs))
    self:addLog("SystemUI ready")
    return self
end

function SystemUI:addLog(message)
    message = tostring(message)
    self.log[#self.log + 1] = message
    while #self.log > 40 do
        table.remove(self.log, 1)
    end
    self.dirty = true
end

function SystemUI:toast(message)
    self.message = tostring(message)
    self.message_until = gfx.now() + 2500
    self.dirty = true
end

function SystemUI:requestStop()
    if self.linger ~= nil then
        self.linger:requestStop()
    elseif type(gothdroid) == "table" and
           type(gothdroid.requestStop) == "function" then
        gothdroid.requestStop()
    end
    self.running = false
end

function SystemUI:stopRequested()
    if not self.running then
        return true
    end
    if self.linger ~= nil and self.linger:stopRequested() then
        return true
    end
    if type(gothdroid) == "table" and
       type(gothdroid.isStopping) == "function" and
       gothdroid.isStopping() then
        return true
    end
    return false
end

function SystemUI:rotate()
    local rotations = { 0, 90, 180, 270 }
    self.rotation_index = (self.rotation_index % #rotations) + 1
    local rotation = rotations[self.rotation_index]
    self.display:setRotation(rotation)
    self:addLog("rotation " .. rotation .. " degrees")
    self:toast("Rotated to " .. rotation .. " degrees")
end

function SystemUI:activate(button)
    if button == nil then
        return
    end
    if button.id == "home" then
        self.screen = "home"
    elseif button.id == "console" then
        self.screen = "console"
    elseif button.id == "diagnostics" then
        self.screen = "diagnostics"
    elseif button.id == "about" then
        self.screen = "about"
    elseif button.id == "rotate" then
        self:rotate()
    elseif button.id == "leave" then
        self:addLog("leaving GothRTOS; Android will restart")
        self:requestStop()
        return
    end
    self.dirty = true
end

function SystemUI:handleEvent(event)
    if event.type == "key" then
        local name = gfx.keyName(event.code)
        if name == "back" then
            if self.screen ~= "home" then
                self.screen = "home"
                self.dirty = true
            else
                self:requestStop()
            end
        elseif name == "volumeup" then
            self:rotate()
        elseif name == "volumedown" then
            self.screen = "diagnostics"
            self.dirty = true
        end
        return
    end
    if event.type ~= "touch" then
        return
    end

    if event.action == "down" then
        self.pressed = nil
        for index = #self.buttons, 1, -1 do
            if contains(self.buttons[index], event.x, event.y) then
                self.pressed = self.buttons[index]
                break
            end
        end
        self.dirty = true
    elseif event.action == "move" then
        if self.pressed ~= nil and
           not contains(self.pressed, event.x, event.y) then
            self.pressed = nil
            self.dirty = true
        end
    elseif event.action == "up" then
        local button = self.pressed
        self.pressed = nil
        if button ~= nil and contains(button, event.x, event.y) then
            self:activate(button)
        end
        self.dirty = true
    elseif event.action == "cancel" then
        self.pressed = nil
        self.dirty = true
    end
end

function SystemUI:updateClock(now)
    if now - self.clock_time < 500 then
        return
    end
    self.clock_time = now
    self.clock_text = os.date("%H:%M")
    self.date_text = os.date("%a %d %b")
    self.dirty = true
end

function SystemUI:updateFps(now)
    self.frames = self.frames + 1
    if now - self.fps_time >= 1000 then
        self.fps = self.frames * 1000 / (now - self.fps_time)
        self.frames = 0
        self.fps_time = now
        if self.screen == "diagnostics" then
            self.dirty = true
        end
    end
end

function SystemUI:addButton(id, label, detail, x, y, width, height, palette)
    self.buttons[#self.buttons + 1] = {
        id = id,
        label = label,
        detail = detail,
        x = x,
        y = y,
        w = width,
        h = height,
        palette = palette,
    }
end

function SystemUI:layout()
    local width, height = self.display:size()
    local margin = clamp(math.floor(math.min(width, height) * 0.05), 14, 40)
    local top = margin
    local header = clamp(math.floor(height * 0.07), 54, 92)
    local hero = clamp(math.floor(height * 0.26), 150, 330)
    local gap = clamp(math.floor(math.min(width, height) * 0.025), 10, 24)
    local bar = clamp(math.floor(height * 0.07), 56, 88)
    local grid_top = top + header + gap
    local grid_bottom = height - margin - bar - gap
    local columns = width >= 640 and 2 or 2
    local tile_gap = gap
    local tile_width = math.floor((width - margin * 2 - tile_gap *
                                   (columns - 1)) / columns)
    local tile_height = math.floor((grid_bottom - grid_top - tile_gap) / 2)
    if tile_height < 80 then
        tile_height = 80
    end
    return {
        width = width,
        height = height,
        margin = margin,
        top = top,
        header = header,
        hero = hero,
        gap = gap,
        bar = bar,
        grid_top = grid_top,
        grid_bottom = grid_bottom,
        tile_width = tile_width,
        tile_height = tile_height,
        tile_gap = tile_gap,
        bar_y = height - margin - bar,
    }
end

function SystemUI:drawBackButton(layout, label)
    local theme = self.theme
    local width = layout.margin * 2 + 92
    local height = clamp(layout.header, 40, 64)
    local y = layout.top + math.floor((layout.header - height) / 2)
    local pressed = self.pressed ~= nil and self.pressed.id == "home"

    self.display:fill(layout.margin, y, width, height,
                      pressed and theme.accent or theme.surface_alt)
    self.display:rect(layout.margin, y, width, height, theme.accent, 2)
    self.display:text(layout.margin + 18, y + math.floor((height - 13) / 2),
                      label or "< Back", theme.text, 1)
    self:addButton("home", "< Back", nil, layout.margin, y, width, height,
                   theme.accent)
end

function SystemUI:drawHeader(layout, title, title_x)
    local theme = self.theme
    local width = layout.width
    local x = title_x or layout.margin

    self.display:fill(0, 0, width, layout.top + layout.header,
                      theme.background)
    self.display:fill(layout.margin, layout.top + layout.header - 2,
                      width - layout.margin * 2, 2, theme.accent)
    self.display:text(x, layout.top + 6, title or "GothRTOS",
                      theme.text, 2)
    self.display:text(width - layout.margin -
                      #self.clock_text * gfx.fontWidth * 2,
                      layout.top + 6, self.clock_text, theme.text, 2)
    self.display:text(width - layout.margin -
                      #self.date_text * gfx.fontWidth,
                      layout.top + 8 + gfx.fontHeight * 2 + 4,
                      self.date_text, theme.muted, 1)
end

function SystemUI:drawHome(layout)
    local theme = self.theme
    local width = layout.width
    local hero_y = layout.top + layout.header + layout.gap
    local hero_x = layout.margin
    local hero_width = width - layout.margin * 2

    self.display:fill(hero_x, hero_y, hero_width, layout.hero, theme.surface)
    self.display:rect(hero_x, hero_y, hero_width, layout.hero,
                      theme.surface_alt, 2)
    self.display:fill(hero_x, hero_y, 8, layout.hero, theme.accent)
    self.display:text(hero_x + 30, hero_y + 26, "GothRTOS", theme.text,
                      width >= 640 and 4 or 3)
    self.display:text(hero_x + 30, hero_y + 26 + gfx.fontHeight * 4 + 8,
                      "Android framework stopped", theme.accent_alt, 2)
    self.display:text(hero_x + 30, hero_y + 26 + gfx.fontHeight * 6 + 18,
                      "SystemUI " .. SystemUI.version, theme.muted, 1)
    self.display:text(hero_x + 30, hero_y + layout.hero - 34,
                      "fb0 + evdev / libgothgfx", theme.muted, 1)

    local tiles = {
        { id = "console", label = "Console",
          detail = "Runtime log and messages", palette = theme.accent_alt },
        { id = "diagnostics", label = "Diagnostics",
          detail = "Display, input and frame rate", palette = theme.accent },
        { id = "rotate", label = "Rotate",
          detail = "Cycle 0 / 90 / 180 / 270", palette = theme.success },
        { id = "leave", label = "Reboot Android",
          detail = "/system/sbin/start", palette = theme.danger },
    }
    for index, tile in ipairs(tiles) do
        local column = (index - 1) % 2
        local row = math.floor((index - 1) / 2)
        local x = layout.margin + column * (layout.tile_width + layout.tile_gap)
        local y = layout.grid_top + row * (layout.tile_height + layout.tile_gap)
        local pressed = self.pressed ~= nil and self.pressed.id == tile.id

        self.display:fill(x, y, layout.tile_width, layout.tile_height,
                          pressed and theme.surface_alt or theme.surface)
        self.display:rect(x, y, layout.tile_width, layout.tile_height,
                          pressed and tile.palette or theme.surface_alt, 2)
        self.display:fill(x, y, 6, layout.tile_height, tile.palette)
        local text_x = x + 22
        local text_y = y + math.max(16, layout.tile_height / 2 - 24)
        self.display:text(text_x, text_y, tile.label, theme.text, 2)
        self.display:text(text_x, text_y + gfx.fontHeight * 2 + 6,
                          tile.detail, theme.muted, 1)
        self:addButton(tile.id, tile.label, tile.detail, x, y,
                       layout.tile_width, layout.tile_height, tile.palette)
    end

    local bar_y = layout.bar_y
    self.display:fill(layout.margin, bar_y, width - layout.margin * 2,
                      layout.bar, theme.surface)
    self.display:rect(layout.margin, bar_y, width - layout.margin * 2,
                      layout.bar, theme.surface_alt, 2)
    self.display:text(layout.margin + 18,
                      bar_y + math.floor((layout.bar - gfx.fontHeight) / 2),
                      "Touch a tile - Back exits GothRTOS", theme.muted, 1)
end

function SystemUI:drawConsole(layout)
    local theme = self.theme
    local box_x = layout.margin
    local box_y = layout.grid_top
    local box_width = layout.width - layout.margin * 2
    local box_height = layout.bar_y - layout.grid_top - layout.gap
    local line_height = gfx.fontHeight + 4
    local visible = math.max(1, math.floor((box_height - 28) / line_height))
    local first = math.max(1, #self.log - visible + 1)

    self.display:fill(box_x, box_y, box_width, box_height, theme.surface)
    self.display:rect(box_x, box_y, box_width, box_height, theme.accent_alt, 2)
    self.display:fill(box_x, box_y, box_width, 3, theme.accent_alt)
    local y = box_y + 16
    for index = first, #self.log do
        local line = self.log[index]
        local color = index == #self.log and theme.text or theme.muted
        self.display:text(box_x + 16, y, "> " .. line, color, 1)
        y = y + line_height
        if y + gfx.fontHeight > box_y + box_height - 10 then
            break
        end
    end
    self.display:text(layout.margin, layout.bar_y + 10,
                      "Back returns to the launcher", theme.muted, 1)
end

function SystemUI:drawDiagnostics(layout)
    local theme = self.theme
    local info = self.display:info()
    local inputs = self.display:inputs()
    local box_x = layout.margin
    local box_y = layout.grid_top
    local box_width = layout.width - layout.margin * 2
    local box_height = layout.bar_y - layout.grid_top - layout.gap
    local lines = {
        string.format("framebuffer  %dx%d @ %d bpp", info.width, info.height,
                      info.bpp),
        string.format("logical      %dx%d, rotation %d", info.logical_width,
                      info.logical_height, info.rotation),
        string.format("stride       %d bytes", info.stride),
        string.format("double buf   %s", tostring(info.double_buffer)),
        string.format("touch inputs %d", #inputs),
        string.format("frame rate   %.1f fps", self.fps),
        string.format("runtime      %s", tostring(_VERSION)),
        string.format("uptime       %.1f s", (gfx.now() - self.start_time) /
                      1000),
        "stop file    " .. tostring(self.linger and self.linger.stop_file or
                                    (gothdroid and gothdroid.stopFile) or "?"),
    }
    for _, path in ipairs(inputs) do
        lines[#lines + 1] = "  " .. tostring(path)
    end

    self.display:fill(box_x, box_y, box_width, box_height, theme.surface)
    self.display:rect(box_x, box_y, box_width, box_height, theme.accent, 2)
    local y = box_y + 18
    for _, line in ipairs(lines) do
        if y + gfx.fontHeight > box_y + box_height - 12 then
            break
        end
        self.display:text(box_x + 18, y, line, theme.text, 1)
        y = y + gfx.fontHeight + 6
    end
    self.display:text(layout.margin, layout.bar_y + 10,
                      "Back returns to the launcher", theme.muted, 1)
end

function SystemUI:drawAbout(layout)
    local theme = self.theme
    local box_x = layout.margin
    local box_y = layout.grid_top
    local box_width = layout.width - layout.margin * 2
    local box_height = layout.bar_y - layout.grid_top - layout.gap

    self.display:fill(box_x, box_y, box_width, box_height, theme.surface)
    self.display:rect(box_x, box_y, box_width, box_height, theme.accent, 2)
    self.display:text(box_x + 22, box_y + 24, "GothRTOS", theme.text, 3)
    self.display:text(box_x + 22, box_y + 24 + gfx.fontHeight * 3 + 10,
                      "A small statically allocated nanokernel", theme.text, 1)
    self.display:text(box_x + 22, box_y + 24 + gfx.fontHeight * 4 + 18,
                      "with a Lua userland.", theme.text, 1)
    self.display:text(box_x + 22, box_y + box_height - 30,
                      "libgothgfx draws to /dev/graphics/fb0", theme.muted, 1)
    self.display:text(layout.margin, layout.bar_y + 10,
                      "Back returns to the launcher", theme.muted, 1)
end

function SystemUI:draw()
    local theme = self.theme
    local layout = self:layout()
    local width = layout.width
    local height = layout.height

    self.buttons = {}
    self.display:clear(theme.background)
    self.display:fill(0, 0, width, height, theme.background)

    if self.screen == "home" then
        self:drawHeader(layout, "GothRTOS")
        self:drawHome(layout)
    else
        local title = self.screen:gsub("^%l", string.upper)
        local back_width = layout.margin * 2 + 92
        self:drawHeader(layout, title,
                        layout.margin + back_width + layout.gap)
        self:drawBackButton(layout, "< Back")
        if self.screen == "console" then
            self:drawConsole(layout)
        elseif self.screen == "diagnostics" then
            self:drawDiagnostics(layout)
        else
            self:drawAbout(layout)
        end
    end

    if self.message ~= "" and gfx.now() < self.message_until then
        local message_width = #self.message * gfx.fontWidth + 28
        local x = math.floor((width - message_width) / 2)
        local y = layout.bar_y - layout.gap - 38
        self.display:fill(x, y, message_width, 30, theme.accent)
        self.display:text(x + 14, y + 8, self.message, theme.background, 1)
    end

    self.display:present()
    self.dirty = false
end

function SystemUI:run()
    log_line("SystemUI entering main loop")
    self:updateClock(gfx.now())
    self:draw()
    while not self:stopRequested() do
        local events = self.display:pollEvents(16)
        for _, event in ipairs(events) do
            self:handleEvent(event)
        end
        local now = gfx.now()
        self:updateClock(now)
        self:updateFps(now)
        if self.dirty then
            self:draw()
        end
        -- Hand the CPU back to the GothRTOS scheduler so the clock advances
        -- and any other task can run.  Under the plain Lua interpreter used
        -- by the host tests there is no goth table, so this is skipped.
        if type(goth) == "table" and type(goth.yield) == "function" then
            goth.yield()
        end
    end
    self:drawShutdown()
    log_line("SystemUI stopped")
    self.display:close()
    return true
end

function SystemUI:drawShutdown()
    local theme = self.theme
    local width, height = self.display:size()

    self.display:clear(theme.background)
    self.display:fill(0, math.floor(height * 0.42), width, 120,
                      theme.surface)
    self.display:text(28, math.floor(height * 0.42) + 20,
                      "Restoring Android...", theme.text, 2)
    self.display:text(28, math.floor(height * 0.42) + 20 +
                      gfx.fontHeight * 2 + 10,
                      "/system/sbin/start", theme.muted, 1)
    self.display:present()
    gfx.sleep(350)
end

function SystemUI.main(options)
    local ui, message = SystemUI.new(options)
    if ui == nil then
        log_line("cannot start SystemUI: " .. tostring(message))
        return false, message
    end
    return ui:run()
end

-- Loaded by gothdroid as a script (no module vararg): run immediately.
-- Loaded with require("SystemUI"): only return the module.
if select("#", ...) == 0 then
    local ok, result, message = xpcall(function()
        return SystemUI.main()
    end, debug.traceback)
    if not ok then
        log_line("fatal: " .. tostring(result))
        os.exit(1)
    end
    if result == false then
        log_line("startup failed: " .. tostring(message))
        os.exit(1)
    end
end

return SystemUI
