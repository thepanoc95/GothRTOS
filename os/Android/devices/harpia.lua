-- Device profile for the Motorola "harpia" family (Moto G4 Play).
--
-- The profile is intentionally data-only: SystemUI.lua merges it with the
-- command-line options supplied by gothdroid.  A device file may omit any
-- field; gothgfx then falls back to its own defaults and evdev auto-detection.

return {
    name = "Motorola harpia",
    model = "Moto G4 Play",
    display = {
        fb = "/dev/graphics/fb0",
        width = 720,
        height = 1280,
        bpp = 32,
        rotate = 0,
        double = false,
        -- The harpia touch panel reports raw coordinates in this range.
        -- gothgfx normally reads the range from EVIOCGABS; these values
        -- override it for images whose evdev metadata is incomplete.
        touch = {
            input = nil, -- auto-detect /dev/input/event*
            x_min = 0,
            x_max = 720,
            y_min = 0,
            y_max = 1280,
            swap_xy = false,
            invert_x = false,
            invert_y = false,
        },
    },
    theme = {
        background = 0x080b12,
        surface = 0x121826,
        surface_alt = 0x1b2335,
        accent = 0x7c5cff,
        accent_alt = 0x26d0ce,
        text = 0xf4f7ff,
        muted = 0x8c98b8,
        danger = 0xff5c7a,
        success = 0x59d98e,
    },
    keys = {
        leave = "back",
        rotate = "volumeup",
        diagnostics = "volumedown",
    },
}
