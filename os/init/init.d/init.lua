-- GothRTOS boot script.
--
-- Loaded by kern.ldr through -init. Module and image paths are resolved
-- relative to this script first, then through the kernel filesystem
-- (/kern/...), so the same script works from the source tree and from an
-- installed image.

local function script_directory()
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)
    end
    source = source:gsub("\\", "/")
    return source:match("^(.*)/[^/]+$") or "."
end

local here = script_directory()

local function load_sibling(name)
    local file = here .. "/" .. name .. ".lua"
    local chunk, load_error = loadfile(file)
    if chunk == nil then
        error("OSBoot: cannot load " .. file .. ": " .. tostring(load_error), 0)
    end
    return chunk()
end

local base = load_sibling("os")
package.loaded["gothos"] = base

-- Keep module lookup independent of the process working directory.
package.path = here .. "/?.lua;" .. here .. "/../../?.lua;" .. package.path

local path = require("path")
local mounts = require("kernel_mounts")
local kernelfs = require("kernelmode")
local shell = require("sh")
local userland = require("userapi")

local function read_system_source(kernelfs_module)
    local candidates = {
        "/kern/init.d/sys/System.lua",
        "/kern/System.lua",
        "os/System.lua",
        here .. "/../../System.lua"
    }

    for _, candidate in ipairs(candidates) do
        local source = kernelfs_module:readFile(candidate)
        if source == nil then
            local handle = io.open(candidate, "r")
            if handle ~= nil then
                source = handle:read("*a")
                handle:close()
            end
        end

        if source ~= nil then
            return source, candidate
        end
    end
    return nil, nil
end

local function load_system(kernelfs_module)
    local source, source_name = read_system_source(kernelfs_module)
    if source == nil then
        error("OSBoot: cannot find System.lua", 0)
    end

    local chunk, load_error = load(source, "@" .. source_name)
    if chunk == nil then
        error("OSBoot: cannot load System.lua: " .. tostring(load_error), 0)
    end

    local system = chunk()
    if type(system) ~= "table" then
        error("OSBoot: System.lua did not return a module table", 0)
    end

    package.loaded["System"] = system
    package.loaded["kern/init.d/sys/System"] = system
    return system
end

local function declare_mounts()
    local root_source = base:getOption("root")
    local driver_source = base:getOption("drv_root")

    if type(root_source) ~= "string" or root_source == "" then
        root_source = "/img/root.bin"
    end
    if type(driver_source) ~= "string" or driver_source == "" then
        driver_source = "/kern.builts"
    end

    mounts:reset()
    if not mounts:declare(root_source, "/", "rootfs") then
        return false, mounts:getError()
    end
    if not mounts:declare(driver_source, "/drivers", "hostfs") then
        return false, mounts:getError()
    end
    return true
end

local function loadShell()
    print("OSBoot: luaexec: /kern/init.d/sh/lua")
    shell:run()
end


function main()
    print("OSBoot: init 0")

    if not kernelfs:mountAll() then
        base:halt(kernelfs:getError())
        return false
    end

    print("OSBoot: loading /kern/init.d/sys/System.lua... ")
    local System = load_system(kernelfs)
    print("OSBoot: System.lua: Now booting.")

    System:setRoot(base:getRoot())
    if not System:boot() then
        print("OSBoot: boot failed: " .. tostring(System:getError()))
        base:halt(System:getError())
        return false
    end

    local ok, mount_error = declare_mounts()
    if ok then
        ok, mount_error = mounts:mountAll()
    end
    if not ok then
        print("OSBoot: Mounts failed: " .. tostring(mount_error))
        System:setError(mount_error)
        base:halt(mount_error)
        return false
    end
    print("OSBoot: All mounts successful.")

    if not userland:setUser("root") then
        print("OSBoot: user setup failed: " .. tostring(userland:getError()))
        base:halt(userland:getError())
        return false
    end

    local shell_directory = path.join(base:getRoot(), "/root")
    local task_id, spawn_error = userland:spawn("sh", { shell_directory })
    if task_id == nil then
        print("OSBoot: shell spawn failed: " .. tostring(spawn_error))
        base:halt(spawn_error)
        return false
    end

    local shell_ok, shell_error = shell:run(shell_directory)
    userland:kill(task_id)
    if not shell_ok then
        print("OSBoot: Shell failed: " .. tostring(shell_error))
        base:halt(shell_error)
        return false
    end

    print("OSBoot: Boot complete.")
    return true
end

local ok, result = pcall(main)
if not ok then
    error(result, 0)
end
if result == false then
    error("OSBoot: " .. tostring(base:getError() or "boot halted"), 0)
end
