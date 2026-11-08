local theme = {}

local palettes = {
    dark = {
        name = "dark",
        background = 0x08090c,
        surface = 0x17191f,
        surface_alt = 0x20232b,
        border = 0x343842,
        text = 0xf4f5f7,
        text_muted = 0x9ca3af,
        accent = 0x78a9ff,
        accent_pressed = 0x4d83df,
        success = 0x63d6a5,
        track = 0x343842
    },
    light = {
        name = "light",
        background = 0xf2f3f5,
        surface = 0xffffff,
        surface_alt = 0xe7e9ed,
        border = 0xd5d8de,
        text = 0x17191f,
        text_muted = 0x666d78,
        accent = 0x246be8,
        accent_pressed = 0x174fae,
        success = 0x16865d,
        track = 0xc8cbd2
    }
}

function theme.get(name)
    name = name or "dark"
    local palette = palettes[name]
    if palette == nil then
        return nil, "unknown UI theme: " .. tostring(name)
    end

    local copy = {}
    for key, value in pairs(palette) do
        copy[key] = value
    end
    return copy
end

return theme
