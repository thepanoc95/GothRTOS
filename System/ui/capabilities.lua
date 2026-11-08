local capabilities = {}

local function normalize_target(target)
    if type(target) ~= "string" or target == "" then
        return "unknown"
    end
    return target:lower():gsub("[^%w]+", "-"):gsub("^%-", ""):gsub("%-$", "")
end

local function pico_target(target)
    return target:find("pico", 1, true) ~= nil or
        target:find("rp2040", 1, true) ~= nil or
        target:find("rp2350", 1, true) ~= nil
end

function capabilities.resolve(target, declared, display)
    local normalized = normalize_target(target)
    local result = {
        target = normalized,
        blur = false,
        blur_reason = "hardware capabilities are unknown"
    }

    if pico_target(normalized) then
        result.blur_reason = "blur is disabled on Pico and RP2040/RP2350 targets"
        return result
    end

    if type(declared) ~= "table" then
        return result
    end
    if declared.blur ~= true then
        result.blur_reason = "blur was not explicitly enabled"
        return result
    end
    if declared.gpu ~= true and declared.cpu_blur ~= true then
        result.blur_reason = "no capable GPU or CPU blur renderer was declared"
        return result
    end
    if declared.render_budget ~= true then
        result.blur_reason = "no sufficient render budget was declared"
        return result
    end
    local display_type = type(display)
    if (display_type ~= "table" and display_type ~= "userdata") or
        type(display.blur) ~= "function" then
        result.blur_reason = "the display backend does not implement blur"
        return result
    end

    result.blur = true
    result.blur_reason = nil
    return result
end

return capabilities
