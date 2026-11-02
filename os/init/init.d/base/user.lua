-- Identity and environment for the GothRTOS Lua userland (base/user).
--
-- Identity, environment and the current-user pointer are userland policy, not
-- kernel objects. When the kernel binding is present the calls are forwarded
-- to it; otherwise a self-contained user table backs the module.

local host = require("base/host")

local user = {}

local state = {
    users = {},
    current = "root",
    environment = {}
}

local function reset()
    state.users = {
        root = {
            name = "root",
            uid = 0,
            gid = 0,
            home = "/root",
            shell = "/bin/sh"
        }
    }
    state.current = "root"
    state.environment = {
        PATH = "/bin:/usr/bin",
        HOME = "/root",
        USER = "root",
        SHELL = "/bin/sh"
    }
end

reset()

function user.reset()
    reset()
    return true
end

function user.addUser(name, uid, gid, home)
    if type(name) ~= "string" or name == "" then
        return false, "invalid user name"
    end
    if host.bound then
        return goth.addUser(name, uid, gid, home)
    end
    if state.users[name] ~= nil then
        return false, "user already exists: " .. name
    end
    if type(uid) ~= "number" or type(gid) ~= "number" then
        return false, "uid and gid must be numbers"
    end

    state.users[name] = {
        name = name,
        uid = uid,
        gid = gid,
        home = home or ("/home/" .. name),
        shell = "/bin/sh"
    }
    return true
end

function user.removeUser(name)
    if host.bound then
        return goth.removeUser(name)
    end
    if name == "root" or name == state.current then
        return false, "cannot remove the current user"
    end
    if state.users[name] == nil then
        return false, "unknown user: " .. tostring(name)
    end

    state.users[name] = nil
    return true
end

function user.setUser(name)
    if host.bound then
        return goth.setUser(name)
    end
    local target = state.users[name]
    if type(name) ~= "string" or target == nil then
        return false, "unknown user: " .. tostring(name)
    end

    state.current = name
    state.environment.USER = target.name
    state.environment.HOME = target.home
    state.environment.SHELL = target.shell
    return true
end

function user.currentUser()
    if host.bound then
        return goth.currentUser()
    end
    return state.users[state.current]
end

function user.getUser(name)
    if host.bound then
        return goth.getUser(name)
    end
    if name == nil then
        return state.users[state.current]
    end
    return state.users[name]
end

function user.isRoot()
    if host.bound then
        return goth.isRoot() == true
    end
    local current = user.currentUser()
    return current ~= nil and current.uid == 0
end

function user.getUid()
    local current = user.currentUser()
    return current ~= nil and current.uid or nil
end

function user.getGid()
    local current = user.currentUser()
    return current ~= nil and current.gid or nil
end

function user.setEnv(key, value)
    if type(key) ~= "string" or key == "" then
        return false, "invalid environment key"
    end
    if host.bound then
        return goth.setEnv(key, value)
    end

    if value == nil then
        state.environment[key] = nil
    else
        state.environment[key] = tostring(value)
    end
    return true
end

function user.getEnv(key)
    if type(key) ~= "string" or key == "" then
        return nil
    end
    if host.bound then
        return goth.getEnv(key)
    end
    return state.environment[key]
end

return user
