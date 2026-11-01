-- Path helpers for the GothRTOS init environment.
--
-- Paths use forward slashes. Backslashes are accepted on input and
-- normalized, which keeps boot scripts usable on Windows hosts.

local path = {}

function path.normalize(value)
    if type(value) ~= "string" or value == "" then
        return "."
    end

    value = value:gsub("\\", "/")
    local absolute = value:sub(1, 1) == "/"
    local parts = {}

    for part in value:gmatch("[^/]+") do
        if part == "." then
            -- Skip the current-directory marker.
        elseif part == ".." then
            if #parts > 0 and parts[#parts] ~= ".." then
                table.remove(parts)
            elseif not absolute then
                parts[#parts + 1] = ".."
            end
        else
            parts[#parts + 1] = part
        end
    end

    local result = table.concat(parts, "/")
    if absolute then
        result = "/" .. result
    end
    if result == "" then
        return absolute and "/" or "."
    end
    return result
end

function path.isAbsolute(value)
    return type(value) == "string" and value:sub(1, 1) == "/"
end

function path.join(...)
    local result = ""

    for index = 1, select("#", ...) do
        local part = select(index, ...)
        if type(part) ~= "string" then
            error("path.join: string expected, got " .. type(part), 2)
        end

        if part ~= "" then
            if path.isAbsolute(part) or part:match("^%a:[/\\]") then
                result = part
            elseif result == "" then
                result = part
            elseif result:sub(-1) == "/" then
                result = result .. part
            else
                result = result .. "/" .. part
            end
        end
    end

    return path.normalize(result)
end

function path.dirname(value)
    if type(value) ~= "string" or value == "" then
        return "."
    end

    local normalized = path.normalize(value)
    if normalized == "/" then
        return "/"
    end

    local directory = normalized:match("^(.*)/[^/]+$")
    if directory == nil or directory == "" then
        return "."
    end
    return directory
end

function path.basename(value)
    if type(value) ~= "string" or value == "" then
        return "."
    end

    local normalized = path.normalize(value)
    if normalized == "/" then
        return "/"
    end
    return normalized:match("([^/]+)$") or normalized
end

function path.extname(value)
    local name = path.basename(value)
    local extension = name:match("(%.[^%.]+)$")
    return extension or ""
end

function path.exists(value)
    if type(value) ~= "string" or value == "" then
        return false
    end

    local file = io.open(value, "r")
    if file == nil then
        return false
    end
    file:close()
    return true
end

return path
