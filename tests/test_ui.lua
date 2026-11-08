package.path = "System/?.lua;System/?/init.lua;" .. package.path

local ui = require("ui")

local function check(condition, message)
    if not condition then
        error(message or "check failed", 2)
    end
end

local function check_equal(actual, expected, message)
    if actual ~= expected then
        error(string.format("%s: expected %s, got %s",
                            message or "value", tostring(expected),
                            tostring(actual)), 2)
    end
end

local palette = assert(ui.theme.get())
check_equal(palette.name, "dark", "default theme")
check_equal(palette.background, 0x08090c, "dark background")
check(ui.theme.get("invalid") == nil, "unknown theme rejected")

local calls = {}
local queued_events = {}
local display = {
    clear = function(_, color) calls[#calls + 1] = {"clear", color} end,
    fill = function(_, x, y, width, height, color)
        calls[#calls + 1] = {"fill", x, y, width, height, color}
    end,
    rect = function(_, x, y, width, height, color, thickness)
        calls[#calls + 1] = {"rect", x, y, width, height, color, thickness}
    end,
    text = function(_, x, y, text, color, scale)
        calls[#calls + 1] = {"text", x, y, text, color, scale}
    end,
    measure = function(_, text)
        return #text * 8
    end,
    blur = function(_, x, y, width, height, radius)
        calls[#calls + 1] = {"blur", x, y, width, height, radius}
    end,
    pollEvents = function()
        local events = queued_events
        queued_events = {}
        return events
    end,
    present = function() calls[#calls + 1] = {"present"} end
}

local conservative, message = ui.new(display, {target = "unknown"})
check(conservative ~= nil, message)
check(not conservative.capabilities.blur, "unknown hardware disables blur")
conservative:begin_frame()
conservative:panel({x = 0, y = 0, width = 32, height = 16,
                    translucent = true})
check_equal(calls[#calls][1], "rect", "unknown hardware uses solid panel")

local pico, pico_error = ui.new(display, {
    target = "Raspberry Pi Pico 2",
    capabilities = {gpu = true, blur = true, render_budget = true}
})
check(pico ~= nil, pico_error)
check(not pico.capabilities.blur, "Pico targets always disable blur")
check(pico.capabilities.blur_reason:find("Pico", 1, true) ~= nil,
      "Pico policy explains fallback")

local capable = assert(ui.new(display, {
    target = "host-desktop",
    capabilities = {gpu = true, blur = true, render_budget = true}
}))
check(capable.capabilities.blur, "explicit capable backend enables blur")
capable:begin_frame()
capable:panel({x = 1, y = 2, width = 20, height = 10, translucent = true})
check_equal(calls[#calls - 1][1], "blur", "capable translucent panel blurs")

local cpu_capable = assert(ui.new(display, {
    target = "generic-cpu",
    capabilities = {cpu_blur = true, blur = true, render_budget = true}
}))
check(cpu_capable.capabilities.blur, "explicit CPU blur budget enables blur")
local insufficient = assert(ui.new(display, {
    target = "generic-cpu",
    capabilities = {cpu_blur = true, blur = true, render_budget = false}
}))
check(not insufficient.capabilities.blur,
      "insufficient render budget disables blur")

local pressed = 0
local rendered = 0
local events_seen = 0
local service = ui.server.new({display = display, target = "unknown"})
check(service:register("home", {
    render = function(canvas, theme, capabilities)
        rendered = rendered + 1
        check_equal(theme.name, "dark", "server default theme")
        check(not capabilities.blur, "server is conservative by default")
        canvas:panel({x = 0, y = 0, width = 80, height = 60})
        canvas:label({x = 4, y = 4, text = "Home"})
        canvas:button({
            id = "launch",
            x = 4, y = 24, width = 48, height = 20, label = "Open",
            on_press = function() pressed = pressed + 1 end
        })
    end,
    on_event = function(event, handled)
        events_seen = events_seen + 1
        check_equal(event.type, "touch", "forwarded touch event")
        check(handled, "button receives touch event")
    end
}))
check(service:start(), "start service")
check_equal(service:info().theme, "dark", "dark-first server")
check_equal(rendered, 1, "initial render")

queued_events = {
    {type = "touch", action = "down", id = 1, x = 8, y = 30},
    {type = "touch", action = "up", id = 1, x = 8, y = 30}
}
check_equal(service:tick(0), 2, "event count")
check_equal(pressed, 1, "button callback")
check_equal(events_seen, 2, "event forwarding")
check_equal(rendered, 2, "event redraw")
check(service:invalidate(), "invalidate")
check_equal(service:tick(0), 0, "empty service tick")
check_equal(rendered, 3, "invalidated redraw")
check(service:stop(), "stop service")
check(not service:info().running, "stopped service state")
check(not service:tick(0), "stopped service rejects ticks")

local owned_closed = false
local owned_display = {
    clear = display.clear,
    fill = display.fill,
    rect = display.rect,
    text = display.text,
    pollEvents = function() return {} end,
    close = function() owned_closed = true end
}
local owned_service = ui.server.new({
    gfx = {open = function() return owned_display end}
})
check(owned_service:register("owned", {
    render = function(canvas)
        canvas:label({x = 0, y = 0, text = "Owned"})
    end
}))
check(owned_service:start(), "service opens its configured graphics backend")
check(owned_service:stop(), "owned service stops")
check(owned_closed, "service closes the display it opened")

print("test_ui.lua: all checks passed")
