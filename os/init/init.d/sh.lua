
-- GothRTOS Shell
-- sh.lua
-- Version 0.1.0

local sh = {
    version = "0.1.0",
    running = true,
    cwd = "/",
    env = {},
    commands = {},
    history = {},
    prompt = "$> "
}

-- Standard library interface
local std = require("base/std")

local function write(s)
    std.write(tostring(s))
end

local function println(s)
    write(tostring(s or "") .. "\n")
end

local function read()
    return std.read()
end

-- Path handling
local function normalize_path(path)
    if path == "" then
        return sh.cwd
    end

    if path:sub(1, 1) ~= "/" then
        path = sh.cwd .. "/" .. path
    end

    local parts = {}

    for part in path:gmatch("[^/]+") do
        if part == ".." then
            table.remove(parts)
        elseif part ~= "." then
            parts[#parts + 1] = part
        end
    end

    return "/" .. table.concat(parts, "/")
end

-- Command registration
local function command(name, callback)
    sh.commands[name] = callback
end

-- Built-in commands
command("help", function()
    println("GothRTOS Shell " .. sh.version)
    println("Available commands:")

    for name in pairs(sh.commands) do
        println("  " .. name)
    end
end)

command("echo", function(args)
    println(table.concat(args, " "))
end)

command("pwd", function()
    println(sh.cwd)
end)

command("cd", function(args)
    local path = normalize_path(args[1] or "/")

    if std.exists(path) and std.is_dir(path) then
        sh.cwd = path
    else
        println("cd: directory not found: " .. path)
    end
end)

command("clear", function()
    std.clear()
end)

command("history", function()
    for i, entry in ipairs(sh.history) do
        println(i .. "  " .. entry)
    end
end)

command("exit", function()
    sh.running = false
end)

-- Variable assignment
local function assign_variable(line)
    local key, value = line:match("^([%a_][%w_]*)=(.*)$")

    if not key then
        return false
    end

    sh.env[key] = value
    return true
end

-- Variable expansion
local function expand_variables(line)
    return (line:gsub("%$([%a_][%w_]*)", function(key)
        return sh.env[key] or ""
    end))
end

-- Basic command parser
local function parse(line)
    local args = {}
    local current = ""
    local quote = nil

    for i = 1, #line do
        local c = line:sub(i, i)

        if quote then
            if c == quote then
                quote = nil
            else
                current = current .. c
            end
        elseif c == '"' or c == "'" then
            quote = c
        elseif c:match("%s") then
            if #current > 0 then
                args[#args + 1] = current
                current = ""
            end
        else
            current = current .. c
        end
    end

    if #current > 0 then
        args[#args + 1] = current
    end

    return args
end

-- Execute command
local function execute(line)
    line = expand_variables(line)

    if assign_variable(line) then
        return
    end

    local args = parse(line)

    if #args == 0 then
        return
    end

    local name = table.remove(args, 1)
    local callback = sh.commands[name]

    if callback then
        local ok, err = pcall(callback, args)

        if not ok then
            println(name .. ": " .. tostring(err))
        end
    else
        println(name .. ": command not found")
    end
end

-- Main shell loop
function sh.run()
    while sh.running do
        write(sh.prompt)

        local line = read()

        if line == nil then
            break
        end

        if line ~= "" then
            sh.history[#sh.history + 1] = line
            execute(line)
        end
    end
end

return sh
