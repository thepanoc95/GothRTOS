-- Headless smoke test for SystemUI.lua on a gothgfx memory surface.
--
-- The Android runtime normally supplies /dev/graphics/fb0 and evdev input.
-- This test uses the development fallback so the whole UI (layout, touch
-- hit-testing, rotation, navigation, shutdown request) is exercised on a
-- build machine.

package.path = "os/Android/?.lua;os/Android/SystemUI/?.lua;" ..
               "os/Android/devices/?.lua;" .. package.path

local STOP_FILE = "/tmp/opencode/gothrtos-systemui-stop"
os.remove(STOP_FILE)

gothdroid_options = {
    memory = true,
    width = 360,
    height = 640,
    bpp = 32,
    input = false,
    device = "harpia",
}

local SystemUI = assert(require("SystemUI"))

local ui, message = SystemUI.new({ stop_file = STOP_FILE })
assert(ui, message)

assert(ui.screen == "home", "SystemUI starts on the home screen")
assert(ui.display:info().logical_width == 360, "configured width")
assert(ui.display:info().logical_height == 640, "configured height")

ui:draw()
assert(#ui.buttons >= 4, "home screen has action tiles")

local function find(id)
    for _, button in ipairs(ui.buttons) do
        if button.id == id then
            return button
        end
    end
    return nil
end

local console = assert(find("console"), "console tile")
ui:handleEvent({ type = "touch", action = "down", x = console.x + 4,
                 y = console.y + 4 })
ui:handleEvent({ type = "touch", action = "up", x = console.x + 4,
                 y = console.y + 4 })
assert(ui.screen == "console", "touch opens the console screen")
ui:draw()
assert(find("home"), "console screen has a Back button")

ui:handleEvent({ type = "key", code = gothgfx.KEY_BACK })
assert(ui.screen == "home", "Back returns to the home screen")

local before = ui.display:info().rotation
ui:rotate()
local after = ui.display:info().rotation
assert(after ~= before, "rotate changes the display rotation")
assert(ui.display:info().logical_width == 640, "rotated logical width")
assert(ui.display:info().logical_height == 360, "rotated logical height")
ui:draw()

ui.screen = "diagnostics"
ui:draw()
ui.screen = "about"
ui:draw()
ui.screen = "home"
ui:draw()

ui:requestStop()
assert(not ui.running, "stop request ends the UI")
local handle = assert(io.open(STOP_FILE, "rb"), "stop request writes the stop file")
handle:close()
os.remove(STOP_FILE)

ui.display:close()

-- Unknown device names must not fall back to another device's profile and
-- must not break the UI.
gothdroid_options.device = "not-a-real-device"
local generic = assert(SystemUI.new({}))
assert(generic.display:info().logical_width == 360, "generic width")
generic:draw()
generic.display:close()
gothdroid_options.device = "harpia"

print("test_systemui.lua: all checks passed")
