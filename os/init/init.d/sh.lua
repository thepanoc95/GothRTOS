-- Non-interactive and interactive Lua shell used by the GothRTOS init script.
--
-- Boot uses shell:run() to establish a working directory and source an optional
-- startup file. It does not block waiting for input; a REPL is available only
-- through shell:interactive() or shell:repl().

local path = require("path")

local shell = {
    cwd = "/",
    running = false,
    error = nil,
    history = {},
    startup_files = { ".shrc", "startup.lua" }
}

function shell:getCwd()
    return self.cwd
end

function shell:setCwd(value)
    if type(value) ~= "string" or value == "" then
        return false, self:setError("invalid working directory")
    end

    self.cwd = path.normalize(value)
    self.error = nil
    return true
end

function shell:getError()
    return self.error
end

function shell:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

function shell:execute(line, chunk_name)
    if type(line) ~= "string" or line == "" then
        return false, self:setError("empty shell command")
    end

    self.history[#self.history + 1] = line

    local chunk, load_error = load(line, chunk_name or "=(shell)")
    if chunk == nil then
        return false, self:setError(load_error)
    end

    local ok, result = pcall(chunk)
    if not ok then
        return false, self:setError(result)
    end

    self.error = nil
    return true, result
end

function shell:source(file)
    if type(file) ~= "string" or file == "" then
        return false, self:setError("invalid script path")
    end

    local handle, open_error = io.open(file, "r")
    if handle == nil then
        return false, self:setError(open_error)
    end

    local source = handle:read("*a")
    handle:close()
    return self:execute(source, "@" .. file)
end

function shell:run(directory)
    if directory ~= nil then
        local ok, error_message = self:setCwd(directory)
        if not ok then
            return false, error_message
        end
    end

    self.running = true
    self.error = nil

    for _, name in ipairs(self.startup_files) do
        local file = path.join(self.cwd, name)
        local handle = io.open(file, "r")
        if handle ~= nil then
            handle:close()
            local ok, error_message = self:source(file)
            if not ok then
                self.running = false
                return false, error_message
            end
            break
        end
    end

    self.running = false
    return true
end

function shell:interactive(input, output)
    input = input or io.stdin
    output = output or io.stdout

    self.running = true
    while true do
        output:write("goth> ")
        output:flush()

        local line = input:read("*l")
        if line == nil then
            output:write("\n")
            break
        end

        line = line:gsub("^%s+", ""):gsub("%s+$", "")
        if line == "exit" or line == "quit" then
            break
        elseif line ~= "" then
            local ok, result = self:execute(line)
            if ok then
                if result ~= nil then
                    output:write(tostring(result) .. "\n")
                end
            else
                output:write("sh: " .. tostring(result) .. "\n")
            end
        end
    end

    self.running = false
    return true
end

function shell:repl()
    return self:interactive(io.stdin, io.stdout)
end

return shell
