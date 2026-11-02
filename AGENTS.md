# AGENTS.md — GothRTOS

Repository-specific guidance for agents working in this tree.

## What this is

GothRTOS is a small statically allocated nanokernel in C11 with a Lua-oriented
usermode userland. It is deliberately **not** POSIX, UNIX or JME. The kernel
only schedules cooperative tasks and moves messages between endpoints;
everything else (console, time, identity, environment, the process table) is
usermode policy.

## Build and test

```sh
make            # host build: libgothrtos.a, libkapi.a, libgothc.a, liblua.a,
                # build/lua, build/kern.ldr
make test       # full host suite (C tests, Lua tests, kernel boot smoke tests)
make arm32      # bare-metal QEMU virt image (needs arm-none-eabi-gcc)
make rpi-zero   # Raspberry Pi Zero image
```

`make test` runs `build/kern.ldr` several times. It boots the init script and
the shell, which read from stdin. Always run it as `make test </dev/null` in a
non-interactive session, otherwise the shell blocks forever waiting on a TTY.

The ARM/RPi toolchains are not installed in the default dev container, so
`make arm32` / `make rpi-zero` cannot be exercised here. Syntax-check ARM port
changes with the host compiler instead:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -ffreestanding -fno-builtin \
   -Iinclude -Ipexpert -Ipexpert/arm -fsyntax-only pexpert/arm/pe_arm32.c
```

## Layout

- `rtos/` — kernel core: `kernel.c` (scheduler), `ipc.c` (endpoints),
  `syscall.c` (syscall dispatch), `userapi.c` (userland API). Shared by the
  user-mode and bare-metal builds, so it must stay freestanding.
- `include/gothrtos/` — public headers. `userapi.h` is the userland surface.
- `libkapi/kapi.c` — thin kernel API wrapper over the syscalls.
- `pexpert/` — platform experts. `common.c` dispatches to per-platform ops
  (`pexpert/um/pe_linux.c`, `pexpert/um/pe_w64.c`, `pexpert/arm/`). The ops
  table lives in `pexpert/pe_internal.h`; add new ops to all three ports.
- `os/init/init.d/*.lua` — boot environment (`os.lua`, `path.lua`,
  `kernel_mounts.lua`, `kernelmode.lua`, `sh.lua`, `userapi.lua`).
- `os/init/init.d/base/*.lua` — the GothRTOS userland OS API in Lua (`io`,
  `sys`, `user`, `proc`, `ipc`, `app`, `std`, plus the `host` bridge). This is
  the primary OS API surface.
- `os/init/gothlib.c` — Lua binding that publishes the `goth` table (and the
  legacy `goth_*` globals). The `base/*` modules forward to it when present.
  Wired into every Lua state by `open_environment()` in `os/init/luaexec.c`.
- `os/System.lua` — system state module loaded by the boot script.

## Conventions

- C: C11, `-Wall -Wextra -Werror -pedantic`. `rtos/` and the ARM ports are
  freestanding (`-ffreestanding -fno-builtin`, no libc). Provide local
  `mem*`/`str*` helpers there; do not include `<string.h>`.
- Static allocation only. Fixed-size tables and arrays, no `malloc` in
  `rtos/`.
- Status enums are returned by value; functions that can fail use
  `goth_status_t`. Lua bindings map failures to `(false, message)` and
  lookups to `nil`.
- New source files must be added to the relevant source list in `GNUmakefile`
  (`SOURCES`, `ARM_C_SOURCES`, `RPI_C_SOURCES`) and to the matching header
  dependency lists.
- Lua userland modules must keep working under the standalone `build/lua`
  interpreter (no `goth` table). Guard on `type(goth) == "table"` and fall back
  to Lua's `io`/`os`.

## Tests

- `tests/test_kernel.c` — scheduler, IPC, syscalls, kapi.
- `tests/test_userapi.c` — C userland API.
- `tests/test_userapi.lua` — `goth` Lua binding, run through `build/kern.ldr`.
- `tests/test_userland_api.lua` — the `base/*` Lua OS API and the `userapi`
  facade; runs both standalone and through `build/kern.ldr`.
- `tests/test_system.lua`, `tests/test_init_modules.lua` — Lua boot modules,
  run both standalone and through the kernel.
