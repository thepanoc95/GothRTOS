local theme_module = require("ui.theme")
local capability_module = require("ui.capabilities")

local framework = {}
local Canvas = {}
Canvas.__index = Canvas

local function integer(value, name)
    if type(value) ~= "number" or value % 1 ~= 0 then
        error(name .. " must be an integer", 3)
    end
    return value
end

local function valid_rect(options)
    return integer(options.x, "x"), integer(options.y, "y"),
        integer(options.width, "width"), integer(options.height, "height")
end

local function point_in_rect(x, y, rect)
    return x >= rect.x and y >= rect.y and
        x < rect.x + rect.width and y < rect.y + rect.height
end

function framework.new(display, options)
    options = options or {}
    if type(options) ~= "table" then
        return nil, "UI options must be a table"
    end
    if type(display) ~= "table" and type(display) ~= "userdata" then
        return nil, "a GothGfx-compatible display is required"
    end
    for _, method in ipairs({"clear", "fill", "rect", "text"}) do
        if type(display[method]) ~= "function" then
            return nil, "display is missing the " .. method .. " method"
        end
    end

    local palette, theme_error = theme_module.get(options.theme)
    if palette == nil then
        return nil, theme_error
    end

    local capabilities = capability_module.resolve(
        options.target, options.capabilities, display)
    return setmetatable({
        display = display,
        theme = palette,
        capabilities = capabilities,
        hits = {},
        pressed = nil,
        pointer_id = nil
    }, Canvas)
end

function Canvas:begin_frame()
    self.hits = {}
    self.pressed = nil
    self.pointer_id = nil
    self.display:clear(self.theme.background)
end

function Canvas:panel(options)
    local x, y, width, height = valid_rect(options)
    if width <= 0 or height <= 0 then
        return false, "panel dimensions must be positive"
    end

    if options.translucent == true and self.capabilities.blur then
        self.display:blur(x, y, width, height, options.radius or 12)
    else
        self.display:fill(x, y, width, height,
                          options.color or self.theme.surface)
    end
    self.display:rect(x, y, width, height,
                      options.border or self.theme.border, 1)
    return true
end

function Canvas:label(options)
    local x, y = integer(options.x, "x"), integer(options.y, "y")
    self.display:text(x, y, tostring(options.text or ""),
                      options.color or self.theme.text,
                      options.scale or 1)
    return true
end

function Canvas:button(options)
    local x, y, width, height = valid_rect(options)
    if width <= 0 or height <= 0 then
        return false, "button dimensions must be positive"
    end
    local label = tostring(options.label or "")
    local color = options.color or self.theme.accent
    self.display:fill(x, y, width, height, color)
    self.display:rect(x, y, width, height, self.theme.border, 1)

    local text_width = #label * 8
    if type(self.display.measure) == "function" then
        text_width = self.display:measure(label, 1)
    end
    local text_x = x + math.max(0, math.floor((width - text_width) / 2))
    local text_y = y + math.max(0, math.floor((height - 13) / 2))
    self.display:text(text_x, text_y, label, self.theme.text, 1)

    if type(options.on_press) == "function" then
        self.hits[#self.hits + 1] = {
            id = options.id or ("button-" .. (#self.hits + 1)),
            x = x,
            y = y,
            width = width,
            height = height,
            callback = options.on_press
        }
    end
    return true
end

function Canvas:toggle(options)
    local x, y, width, height = valid_rect(options)
    if width <= 0 or height <= 0 then
        return false, "toggle dimensions must be positive"
    end
    local value = options.value == true
    local track = value and self.theme.accent or self.theme.track
    local inset = math.max(1, math.floor(height / 5))
    local thumb_width = math.max(1, math.floor(width / 2))
    self.display:fill(x, y, width, height, track)
    self.display:fill(value and (x + width - thumb_width) or x,
                      y + inset, thumb_width, math.max(1, height - 2 * inset),
                      self.theme.text)
    if type(options.on_change) == "function" then
        self.hits[#self.hits + 1] = {
            id = options.id or ("toggle-" .. (#self.hits + 1)),
            x = x,
            y = y,
            width = width,
            height = height,
            callback = function()
                options.on_change(not value)
            end
        }
    end
    return true
end

function Canvas:progress(options)
    local x, y, width, height = valid_rect(options)
    if width <= 0 or height <= 0 then
        return false, "progress dimensions must be positive"
    end
    local value = options.value or 0
    if type(value) ~= "number" then
        return false, "progress value must be a number"
    end
    value = math.max(0, math.min(1, value))
    self.display:fill(x, y, width, height, self.theme.track)
    self.display:fill(x, y, math.floor(width * value), height,
                      options.color or self.theme.accent)
    return true
end

function Canvas:dispatch(event)
    if type(event) ~= "table" or event.type ~= "touch" then
        return false
    end
    if event.action == "down" then
        self.pressed = nil
        self.pointer_id = event.id
        for index = #self.hits, 1, -1 do
            if point_in_rect(event.x, event.y, self.hits[index]) then
                self.pressed = self.hits[index]
                break
            end
        end
        return self.pressed ~= nil
    end
    if event.action == "up" then
        local hit = self.pressed
        local activate = hit ~= nil and event.id == self.pointer_id and
            point_in_rect(event.x, event.y, hit)
        self.pressed = nil
        self.pointer_id = nil
        if activate then
            hit.callback()
            return true
        end
    end
    return false
end

function Canvas:present()
    if type(self.display.present) == "function" then
        return self.display:present()
    end
    return true
end

return framework
