-- System scheduler module.
--
-- Injected into System.lua by SysInject. It turns the declarative PREEMPT and
-- quantum fields into a small configuration API that the boot pipeline reads.

function System:getSchedulerConfig()
    local preemptive = self.PREEMPT ~= false

    return {
        mode = preemptive and "preemptive" or "cooperative",
        preempt = preemptive,
        quantum = self.quantum or 1
    }
end

function System:setPreempt(enabled)
    if type(enabled) ~= "boolean" then
        return false, "PREEMPT must be a boolean"
    end
    self.PREEMPT = enabled
    return true
end

function System:setQuantum(ticks)
    if type(ticks) ~= "number" or ticks < 1 or ticks % 1 ~= 0 then
        return false, "quantum must be a positive integer"
    end
    self.quantum = ticks
    return true
end

modules[#modules + 1] = {
    name = "scheduler",
    mode = System:getSchedulerConfig().mode
}
