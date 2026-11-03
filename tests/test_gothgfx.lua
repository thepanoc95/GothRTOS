-- Host tests for the libgothgfx Lua module.
--
-- Everything here uses in-memory surfaces (gothgfx.create / gothgfx.open with
-- memory = true), so the suite runs on machines without /dev/graphics/fb0 or
-- /dev/input/event*.

local gfx = assert(gothgfx, "gothgfx was not registered")

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

-- Module surface -----------------------------------------------------------

check_equal(gfx.name, "GothRTOS", "module name")
check(type(gfx.version) == "string", "module version")
check_equal(gfx.fontWidth, 8, "font width")
check_equal(gfx.fontHeight, 13, "font height")
check(type(gfx.now()) == "number", "gothgfx.now")
gfx.sleep(1)
check_equal(gfx.keyName(gfx.KEY_BACK), "back", "keyName(BACK)")
check_equal(gfx.keyName(gfx.KEY_VOLUMEUP), "volumeup", "keyName(VOLUMEUP)")
check_equal(gfx.keyName(99999), nil, "keyName(unknown)")
check(type(gfx.scanInputs()) == "table", "scanInputs")

-- Basic drawing on a 32-bit surface ---------------------------------------

local display = gfx.create(320, 240, 32)
check(type(display) == "userdata", "create returns a display")
check(tostring(display):find("gothgfx.display", 1, true) ~= nil,
      "display tostring")

local info = display:info()
check_equal(info.width, 320, "info width")
check_equal(info.height, 240, "info height")
check_equal(info.logical_width, 320, "logical width")
check_equal(info.logical_height, 240, "logical height")
check_equal(info.bpp, 32, "bpp")
check_equal(info.stride, 320 * 4, "stride")
check_equal(info.rotation, 0, "rotation")

local width, height = display:size()
check_equal(width, 320, "size width")
check_equal(height, 240, "size height")

display:clear(0x112233)
check_equal(display:getPixel(0, 0), 0x112233, "clear")

display:fill(10, 20, 30, 40, 0xff0000)
check_equal(display:getPixel(10, 20), 0xff0000, "fill top left")
check_equal(display:getPixel(39, 59), 0xff0000, "fill bottom right")
check_equal(display:getPixel(40, 59), 0x112233, "fill right edge")
check_equal(display:getPixel(9, 20), 0x112233, "fill left edge")

display:pixel(100, 100, 0x00ff00)
check_equal(display:getPixel(100, 100), 0x00ff00, "pixel")

-- Out-of-bounds writes are ignored; partially visible fills are clipped.
display:pixel(-1, 0, 0xffffff)
display:fill(300, 230, 100, 100, 0xffffff)
check_equal(display:getPixel(319, 239), 0xffffff, "clipped fill")
check_equal(display:getPixel(299, 239), 0x112233, "clipped fill edge")
check_equal(display:getPixel(320, 0), nil, "out-of-bounds read")

display:clear(0x000000)
display:rect(0, 0, 10, 10, 0x00ff00, 2)
check_equal(display:getPixel(0, 0), 0x00ff00, "rect corner")
check_equal(display:getPixel(9, 9), 0x00ff00, "rect far corner")
check_equal(display:getPixel(5, 5), 0x000000, "rect hollow center")

display:line(0, 0, 9, 9, 0x0000ff)
check_equal(display:getPixel(4, 4), 0x0000ff, "line")

-- Text: "A" is a full-height glyph with set pixels around (3, 2).
display:clear(0x000000)
local text_width, text_height = display:text(0, 0, "A", 0xffffff, 1)
check_equal(text_width, 8, "text width")
check_equal(text_height, 13, "text height")
check_equal(display:getPixel(3, 2), 0xffffff, "text pixel")
check_equal(display:getPixel(0, 0), 0x000000, "text padding pixel")

local measured_width, measured_height = display:measure("abc", 2)
check_equal(measured_width, 3 * 8 * 2, "measure width")
check_equal(measured_height, 13 * 2, "measure height")

-- Inputs are empty for a memory surface unless explicitly requested.
check_equal(#display:inputs(), 0, "memory surface inputs")
check_equal(#display:pollEvents(0), 0, "pollEvents")

-- Rotation ----------------------------------------------------------------

local rotated = gfx.create(100, 50, 32)
check(rotated:setRotation(90), "setRotation returns true")
local rotated_width, rotated_height = rotated:size()
check_equal(rotated_width, 50, "rotated logical width")
check_equal(rotated_height, 100, "rotated logical height")
rotated:pixel(0, 0, 0xff0000)
check_equal(rotated:getPixel(0, 0), 0xff0000, "rotated pixel")
check_equal(rotated:getPixel(49, 99), 0x000000, "rotated far pixel")
check_equal(rotated:getPixel(50, 0), nil, "rotated out-of-bounds read")
rotated:setRotation(180)
check_equal(rotated:size(), 100, "rotation 180 width")
rotated:setRotation(270)
check_equal(rotated:size(), 50, "rotation 270 width")
rotated:setRotation(0)
check_equal(rotated:size(), 100, "rotation 0 width")

-- 16-bit RGB565 packing and unpacking -------------------------------------

local rgb565 = gfx.create(4, 4, 16)
check_equal(rgb565:info().stride, 4 * 2, "rgb565 stride")
rgb565:clear(0xff0000)
check_equal(rgb565:getPixel(0, 0), 0xf80000, "rgb565 red")
rgb565:clear(0xffffff)
check_equal(rgb565:getPixel(0, 0), 0xf8fcf8, "rgb565 white")

-- 8-bit grayscale ---------------------------------------------------------

local gray = gfx.create(2, 2, 8)
gray:clear(0xffffff)
check_equal(gray:getPixel(0, 0), 0xffffff, "grayscale white")
gray:clear(0xff0000)
check_equal(gray:getPixel(0, 0), 0x4c4c4c, "grayscale luminance")

-- Options tables ----------------------------------------------------------

local memory = assert(gfx.open({ memory = true, width = 32, height = 16,
                                 bpp = 32, input = false }))
check_equal(memory:info().logical_width, 32, "open memory width")
check_equal(memory:info().logical_height, 16, "open memory height")
check_equal(#memory:inputs(), 0, "open memory inputs")
memory:close()
check(memory:close(), "double close returns true")

local ok, message = pcall(gfx.create, 0, 0, 32)
check(not ok and type(message) == "string", "create rejects empty size")
ok, message = pcall(gfx.create, 4, 4, 7)
check(not ok and type(message) == "string", "create rejects invalid bpp")
ok, message = pcall(gfx.open, { memory = true })
check(not ok and type(message) == "string", "open memory needs a size")
ok = pcall(gfx.open, { memory = true, width = 4, height = 4, rotate = 45 })
check(not ok, "open rejects invalid rotation")
ok = pcall(rotated.setRotation, rotated, 45)
check(not ok, "setRotation rejects invalid rotation")

display:close()

print("test_gothgfx.lua: all checks passed")
