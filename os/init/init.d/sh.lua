-- GothRTOS Shell
-- sh.lua
-- Version 0.1.0
--
-- A small, POSIX-free command interpreter. Lines are first matched against the
-- registered command table; anything else is evaluated as Lua, which lets the
-- boot environment use the shell both as a command prompt and as a script
-- console.

local sh = {
    version = "0.1.0",
    running = true,
    cwd = "/",
    env = {},
    commands = {},
    history = {},
    prompt = "$> "
}

local std = require("base/std")

local function write(value)
    std.write(tostring(value))
end

local function println(value)
    write(tostring(value or "") .. "\n")
end

local function read()
    return std.read()
end

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

local function command(name, callback)
    sh.commands[name] = callback
end

function sh:setCwd(path)
    if type(path) ~= "string" or path == "" then
        return false
    end

    self.cwd = normalize_path(path)
    return true
end

function sh:getCwd()
    return self.cwd
end

command("help", function()
    println("GothRTOS Shell " .. sh.version)
    println("Available commands:")

    local names = {}
    for name in pairs(sh.commands) do
        names[#names + 1] = name
    end
    table.sort(names)
    for _, name in ipairs(names) do
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
    for index, entry in ipairs(sh.history) do
        println(index .. "  " .. entry)
    end
end)

command("exit", function()
    sh.running = false
end)

local function assign_variable(line)
    local key, value = line:match("^([%a_][%w_]*)=(.*)$")

    if not key then
        return false
    end

    sh.env[key] = value
    return true
end

local function expand_variables(line)
    return (line:gsub("%$([%a_][%w_]*)", function(key)
        return sh.env[key] or ""
    end))
end

local function parse(line)
    local args = {}
    local current = ""
    local quote = nil

    for index = 1, #line do
        local character = line:sub(index, index)

        if quote then
            if character == quote then
                quote = nil
            else
                current = current .. character
            end
        elseif character == '"' or character == "'" then
            quote = character
        elseif character:match("%s") then
            if #current > 0 then
                args[#args + 1] = current
                current = ""
            end
        else
            current = current .. character
        end
    end

    if #current > 0 then
        args[#args + 1] = current
    end

    return args
end

local function run_command(name, args)
    local callback = sh.commands[name]
    local ok, err = pcall(callback, args)

    if not ok then
        println(name .. ": " .. tostring(err))
        return false, err
    end
    return true
end

local function run_lua(source)
    local chunk, compile_error = load(source, "=(sh)", "t")

    if chunk == nil then
        return false, compile_error
    end
    return pcall(chunk)
end

-- Executes a single shell line. Registered commands run directly; everything
-- else is treated as a Lua chunk. Returns (ok, result_or_error).
function sh:execute(line)
    if type(line) ~= "string" then
        return false, "invalid command line"
    end

    self.history[#self.history + 1] = line
    line = expand_variables(line)

    if line:match("^%s*$") then
        return true
    end
    if assign_variable(line) then
        return true
    end

    local args = parse(line)
    local name = args[1]

    if name ~= nil and self.commands[name] ~= nil then
        table.remove(args, 1)
        return run_command(name, args)
    end

    return run_lua(line)
end

function sh:getHistory()
    local copy = {}
    for index, entry in ipairs(self.history) do
        copy[index] = entry
    end
    return copy
end

-- Runs the interactive shell. When a directory is supplied the shell only
-- starts if that directory exists; a missing directory is a graceful no-op so
-- callers such as the boot script do not fail on a partial root.
function sh:run(directory)
    if directory ~= nil then
        if type(directory) ~= "string" or directory == "" then
            return false
        end
        if not std.exists(directory) then
            return true
        end
        self.cwd = normalize_path(directory)
    end

    self.running = true
    println("GothRTOS Shell " .. self.version .. " (type 'help' for commands)")

    while self.running do
        write(self.prompt)

        local line = read()
        if line == nil then
            break
        end
        if line ~= "" then
            self:execute(line)
        end
    end
    return true
end

return sh
