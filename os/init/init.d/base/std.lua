local std = {}

function std.puts(value)
    goth_puts(tostring(value))
end

function std.write(value)
    goth_write(tostring(value))
end

function std.println(value)
    std.puts(value)
end

function std.exit(code)
    goth_exit(code or 0)
end

return std