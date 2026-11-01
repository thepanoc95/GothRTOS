local System = dofile("os/System.lua")

assert(System:getVersion() == "0.0-indev")
assert(System:getBuild() == "indev")
assert(System:getRoot() == "/")
assert(System:getError() == nil)
assert(System:isBooted() == false)

assert(System:boot() == true)
assert(System:isBooted() == true)
assert(System:getError() == nil)

assert(System:setRoot("") == false)
assert(System:setRoot("/new-root") == true)
assert(System:getRoot() == "/new-root")

assert(System:halt("halted") == true)
assert(System:isBooted() == false)
assert(System:getError() == "halted")

assert(System:boot() == true)
assert(System:getError() == nil)
assert(System:halt() == true)
assert(System:boot() == true)

print("test_system: OK")
