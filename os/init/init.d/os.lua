-- GothRTOS base operating-system module.
--
-- The standard Lua library already owns the global module name "os", so
-- require("os") returns Lua's own table. Init scripts load this file directly
-- (or through init.lua); once loaded it is also published as "gothos" so the
-- other boot modules can share the same instance.

local base = {
    name = "GothRTOS",
    version = "0.0-indev",
    build = "indev",
    root = "/",
    halted = false,
    error = nil,
    arguments = {}
}

local function parse_arguments()
    local values = {}

    if type(arg) ~= "table" then
        return values
    end

    local indices = {}
    for key in pairs(arg) do
        if type(key) == "number" and key >= 1 then
            indices[#indices + 1] = key
        end
    end
    table.sort(indices)

    for _, index in ipairs(indices) do
        if type(arg[index]) == "string" then
            values[#values + 1] = arg[index]
        end
    end
    return values
end

local function split_option(token)
    local key, value = token:match("^%-?%-?([^=]+)=(.*)$")
    if key ~= nil then
        return key, value
    end

    key = token:match("^%-?%-?([^=]+)$")
    if key ~= nil then
        return key, true
    end
    return nil, nil
end

base.arguments = parse_arguments()

function base:getName()
    return self.name
end

function base:getVersion()
    return self.version
end

function base:getBuild()
    return self.build
end

function base:getRoot()
    return self.root
end

function base:setRoot(value)
    if type(value) ~= "string" or value == "" then
        return false, "invalid root path"
    end

    if value:sub(1, 1) ~= "/" then
        value = "/" .. value
    end
    self.root = value
    return true
end

function base:getArguments()
    local copy = {}
    for index, value in ipairs(self.arguments) do
        copy[index] = value
    end
    return copy
end

function base:getOption(key)
    if type(key) ~= "string" or key == "" then
        return nil
    end

    local result = nil
    for _, token in ipairs(self.arguments) do
        local name, value = split_option(token)
        if name == key then
            result = value
        end
    end
    return result
end

function base:hasOption(key)
    return self:getOption(key) ~= nil
end

function base:getOptions()
    local options = {}

    for _, token in ipairs(self.arguments) do
        local name, value = split_option(token)
        if name ~= nil then
            options[name] = value
        end
    end
    return options
end

function base:getMemoryPolicy()
    local memory = self:getOption("mem")
    if type(memory) == "string" and memory ~= "" then
        return memory
    end
    return "static"
end

function base:getSymbolPath()
    local symbol_path = self:getOption("kern_sym")
    if type(symbol_path) == "string" and symbol_path ~= "" then
        return symbol_path
    end
    return "/kern.symbols"
end

function base:getError()
    return self.error
end

function base:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

function base:isHalted()
    return self.halted == true
end

function base:halt(message)
    self.halted = true
    if message ~= nil then
        self:setError(message)
    end
    return true
end

function base:reboot()
    self.halted = false
    self.error = nil
    return true
end

package.loaded["gothos"] = base

return base
