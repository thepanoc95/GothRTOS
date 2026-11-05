local app = require("base/app")
local console = require("base/io")

--[[ @@ProductionOnly
--  local System = require("System")
]]
local function words(line)
    local result = {}
    for word in line:gmatch("%S+") do
        result[#result + 1] = word
    end
    return result
end

local function shell()
    console.println("\n[SystemShell]: Now Starting.")
    console.println("\nWelcome to GothRTOS! version " .. System.version "\nThis is alpha quality software so obviosly it will have bugs. Bleh :3\n")

    while true do
        console.write("[SystemShell]:>>  ")

        local line = console.read()
        if line == nil then
            return true
        end

        local args = words(line)
        local command = args[1]

        if command == nil then
        elseif command == "help" then
            console.println("unimplemented") 
        elseif command == "echo" then
            table.remove(args, 1)
            console.println(table.concat(args, " "))
        elseif command == "apps" then
            for _, name in ipairs(app.list()) do
                console.println(name)
            end
        elseif command == "exit" then
            return true
        else
            local entry = app.lookup(command)
            if entry == nil then
                console.println("unknown command: " .. command)
            else
                table.remove(args, 1)
                local ok, result, detail =
                    app.run(command, table.unpack(args))

                if not ok then
                    console.println(command .. ": " .. tostring(result))
                elseif result == false then
                    console.println(command .. ": " .. tostring(detail))
                end
            end
        end
    end
end

local ok, err = app.register("shell", shell)
if not ok then
    console.println("could not register shell: " .. tostring(err))
    return false, err
end

return app.run("shell")
