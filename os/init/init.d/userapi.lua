-- Userland API surface for the GothRTOS boot environment.
--
-- The user-mode kernel does not create real processes yet. This module keeps
-- the identity, environment, and task bookkeeping that init scripts use, and
-- records spawn requests so a future process backend can consume them.

local userapi = {
    users = {},
    current = "root",
    environment = {},
    tasks = {},
    next_task_id = 1,
    error = nil
}

userapi.users.root = {
    name = "root",
    uid = 0,
    gid = 0,
    home = "/root",
    shell = "/bin/sh"
}

userapi.environment = {
    PATH = "/bin:/usr/bin",
    HOME = userapi.users.root.home,
    USER = userapi.users.root.name,
    SHELL = userapi.users.root.shell
}

function userapi:getError()
    return self.error
end

function userapi:setError(message)
    self.error = message ~= nil and tostring(message) or nil
    return self.error
end

function userapi:getUser(name)
    if name == nil then
        return self.users[self.current]
    end
    return self.users[name]
end

function userapi:currentUser()
    return self.users[self.current]
end

function userapi:setUser(name)
    if type(name) ~= "string" or self.users[name] == nil then
        return false, self:setError("unknown user: " .. tostring(name))
    end

    self.current = name
    local user = self.users[name]
    self.environment.USER = user.name
    self.environment.HOME = user.home
    self.environment.SHELL = user.shell
    self.error = nil
    return true
end

function userapi:addUser(name, uid, gid, home)
    if type(name) ~= "string" or name == "" then
        return false, self:setError("invalid user name")
    end
    if self.users[name] ~= nil then
        return false, self:setError("user already exists: " .. name)
    end
    if type(uid) ~= "number" or type(gid) ~= "number" then
        return false, self:setError("uid and gid must be numbers")
    end

    self.users[name] = {
        name = name,
        uid = uid,
        gid = gid,
        home = home or ("/home/" .. name),
        shell = "/bin/sh"
    }
    self.error = nil
    return true
end

function userapi:removeUser(name)
    if name == "root" or name == self.current then
        return false, self:setError("cannot remove the current user")
    end
    if self.users[name] == nil then
        return false, self:setError("unknown user: " .. tostring(name))
    end

    self.users[name] = nil
    self.error = nil
    return true
end

function userapi:isRoot()
    local user = self:currentUser()
    return user ~= nil and user.uid == 0
end

function userapi:getUid()
    local user = self:currentUser()
    return user ~= nil and user.uid or nil
end

function userapi:getGid()
    local user = self:currentUser()
    return user ~= nil and user.gid or nil
end

function userapi:getEnv(key)
    if type(key) ~= "string" or key == "" then
        return nil
    end
    return self.environment[key]
end

function userapi:setEnv(key, value)
    if type(key) ~= "string" or key == "" then
        return false, self:setError("invalid environment key")
    end

    if value == nil then
        self.environment[key] = nil
    else
        self.environment[key] = tostring(value)
    end
    self.error = nil
    return true
end

function userapi:spawn(program, arguments)
    if type(program) ~= "string" or program == "" then
        return nil, self:setError("invalid program name")
    end
    if arguments ~= nil and type(arguments) ~= "table" then
        return nil, self:setError("program arguments must be a table")
    end

    local id = self.next_task_id
    self.next_task_id = id + 1
    self.tasks[id] = {
        id = id,
        program = program,
        arguments = arguments or {},
        owner = self.current,
        state = "ready"
    }
    self.error = nil
    return id
end

function userapi:getTask(id)
    return self.tasks[id]
end

function userapi:listTasks()
    local copy = {}
    local ids = {}

    for id in pairs(self.tasks) do
        ids[#ids + 1] = id
    end
    table.sort(ids)
    for _, id in ipairs(ids) do
        copy[#copy + 1] = self.tasks[id]
    end
    return copy
end

function userapi:kill(id)
    local task = self.tasks[id]
    if task == nil then
        return false, self:setError("unknown task: " .. tostring(id))
    end

    task.state = "terminated"
    task.exit_code = task.exit_code or 0
    self.error = nil
    return true
end

function userapi:reap(id)
    local task = self.tasks[id]
    if task == nil then
        return false, self:setError("unknown task: " .. tostring(id))
    end
    if task.state ~= "terminated" then
        return false, self:setError("task is still running: " .. tostring(id))
    end

    self.tasks[id] = nil
    self.error = nil
    return true
end

return userapi
