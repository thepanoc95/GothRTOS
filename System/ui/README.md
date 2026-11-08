# GothRTOS userland UI

The UI framework is a Lua userland layer over the existing `gothgfx` display
surface and touch-event API. `require("ui")` returns the drawing toolkit,
`ui.server` provides the foreground UI service, and `System/UIGothic.lua` is a
legacy entry point to the same module. The visual language uses flat surfaces,
compact borders, and restrained accent colors; dark mode is the default.

## Starting a UI service

The service owns one display loop and one focused foreground app at a time.
Apps register a `render(canvas, theme, capabilities)` callback and can receive
input with `on_event(event, handled)`. A real display is opened with GothGfx
when no display is injected; tests or platform-specific adapters can provide a
GothGfx-compatible display object directly.

```lua
local ui = require("ui")
local gfx = require("gothgfx")
local service = ui.server.new({
    target = "linux-framebuffer",
    display_options = {rotate = 90}
})

assert(service:register("home", {
    render = function(canvas, theme)
        canvas:panel({x = 12, y = 12, width = 216, height = 120})
        canvas:label({x = 24, y = 24, text = "GothRTOS"})
        canvas:button({
            id = "open",
            x = 24, y = 64, width = 120, height = 28, label = "Open",
            on_press = function() print("opened") end
        })
    end,
    on_event = function(event)
        if event.type == "key" and event.action == "down" and
            event.code == gfx.KEY_BACK then
            service:stop()
        end
    end
}))

assert(service:start())
service:run(16) -- blocks, polling input and redrawing dirty frames
```

`ui.new(display, options)` creates a canvas directly. Canvas primitives are
`begin_frame`, `panel`, `label`, `button`, `toggle`, `progress`, `dispatch`,
and `present`. They use the display's existing `clear`, `fill`, `rect`, `text`,
`measure`, `pollEvents`, and `present` operations rather than introducing a
second framebuffer or input stack. Panels are flat and opaque by default.

The service is a userland ownership/API boundary, not a kernel task or an IPC
protocol. GothRTOS currently has no general userland display-device service,
window system, or bare-metal display/input drivers, so this API does not claim
cross-process windowing or embedded display support.

## Theme and blur policy

The default palette is `dark`. `ui.theme.get("light")` is available for apps
that explicitly request a light palette. Blur is an optional backend operation,
not an effect emulated by the software renderer. It is enabled only when the
caller explicitly declares `blur = true`, a capable `gpu = true` or
`cpu_blur = true`, `render_budget = true`, and the display implements
`blur(x, y, width, height, radius)`. Missing or unknown capability data
conservatively disables it. `panel({translucent = true})` falls back to a
solid surface when blur is unavailable.

Pico/RP2040 and Pico 2/RP2350 target names always disable blur, even if a
caller claims those capabilities. The current GothGfx framebuffer backend does
not provide blur, and the repository does not yet expose a hardware capability
probe or GPU-backed UI renderer; therefore blur remains off on the built-in
backend. Do not opt in a target without verifying its actual rendering path
and available frame budget.

## Tests

`make test </dev/null` includes `tests/test_ui.lua`. The focused Lua test also
runs with `build/lua tests/test_ui.lua` and verifies the default palette,
unknown/Pico blur fallback, explicit capability gating, touch activation, and
service lifecycle with an in-memory display double.
