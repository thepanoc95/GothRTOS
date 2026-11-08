local framework = require("ui.framework")

local server = {}
local Server = {}
Server.__index = Server

function server.new(options)
    options = options or {}
    return setmetatable({
        options = options,
        display = options.display,
        owns_display = false,
        canvas = nil,
        apps = {},
        focused = nil,
        running = false,
        dirty = true
    }, Server)
end

function Server:register(name, app)
    if type(name) ~= "string" or name == "" then
        return false, "app name must be a non-empty string"
    end
    if type(app) ~= "table" or type(app.render) ~= "function" then
        return false, "app must provide a render(canvas, theme, capabilities) function"
    end
    if self.apps[name] ~= nil then
        return false, "app is already registered: " .. name
    end
    self.apps[name] = app
    if self.focused == nil then
        self.focused = name
    end
    self.dirty = true
    return true
end

function Server:unregister(name)
    if self.apps[name] == nil then
        return false, "app is not registered: " .. tostring(name)
    end
    self.apps[name] = nil
    if self.focused == name then
        self.focused = next(self.apps)
    end
    self.dirty = true
    return true
end

function Server:focus(name)
    if self.apps[name] == nil then
        return false, "app is not registered: " .. tostring(name)
    end
    self.focused = name
    self.dirty = true
    return true
end

function Server:invalidate()
    self.dirty = true
    return true
end

function Server:render()
    local focused = self.focused
    local app = focused ~= nil and self.apps[focused] or nil
    if app == nil then
        return false, "there is no focused UI app"
    end
    local canvas = self.canvas
    self.dirty = false
    canvas:begin_frame()
    app.render(canvas, canvas.theme, canvas.capabilities)
    canvas:present()
    return true
end

function Server:start()
    if self.running then
        return true
    end
    if self.display == nil then
        local gfx = self.options.gfx or require("gothgfx")
        self.display = gfx.open(self.options.display_options or {})
        self.owns_display = true
    end

    local canvas, message = framework.new(self.display, {
        theme = self.options.theme or "dark",
        target = self.options.target,
        capabilities = self.options.capabilities
    })
    if canvas == nil then
        if self.owns_display and type(self.display.close) == "function" then
            self.display:close()
            self.display = nil
            self.owns_display = false
        end
        return false, message
    end
    self.canvas = canvas
    self.running = true
    self.dirty = true
    if self.focused ~= nil then
        self:render()
    end
    return true
end

function Server:tick(timeout)
    if not self.running then
        return false, "UI server is not running"
    end
    local canvas = self.canvas
    local events = self.display:pollEvents(timeout or 0)
    for _, event in ipairs(events) do
        local handled = canvas:dispatch(event)
        local app = self.focused ~= nil and self.apps[self.focused] or nil
        if app ~= nil and type(app.on_event) == "function" then
            app.on_event(event, handled)
        end
        if not self.running then
            break
        end
        self.dirty = true
    end
    local app = self.focused ~= nil and self.apps[self.focused] or nil
    if self.running and self.dirty and app ~= nil then
        self:render()
    end
    return #events
end

function Server:run(timeout)
    if not self.running then
        return false, "UI server is not running"
    end
    while self.running do
        self:tick(timeout or 16)
    end
    return true
end

function Server:stop()
    if not self.running then
        return true
    end
    self.running = false
    if self.owns_display and self.display ~= nil and
        type(self.display.close) == "function" then
        self.display:close()
        self.display = nil
        self.owns_display = false
    end
    self.canvas = nil
    return true
end

function Server:info()
    local capabilities = self.canvas ~= nil and self.canvas.capabilities or nil
    return {
        running = self.running,
        focused = self.focused,
        theme = self.canvas ~= nil and self.canvas.theme.name or
            (self.options.theme or "dark"),
        target = self.canvas ~= nil and self.canvas.capabilities.target or
            self.options.target or "unknown",
        blur = capabilities ~= nil and capabilities.blur or false,
        blur_reason = capabilities ~= nil and
            (capabilities.blur_reason or "blur enabled") or
            "hardware capabilities have not been evaluated"
    }
end

return server
