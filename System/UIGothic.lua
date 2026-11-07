local gfx = require("gothgfx")
local screen = assert(gfx.open{ rotate = 90 })

screen:clear(0x101018)
screen:fill(40, 40, 200, 80, 0x2b6cb0)
screen:text(56, 64, "GothRTOS", 0xffffff, 2)

for _, ev in ipairs(screen:pollEvents(16)) do ... end
screen:close()
