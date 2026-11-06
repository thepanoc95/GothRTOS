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
make pico       # Raspberry Pi Pico (RP2040, Cortex-M0+) image
make pico2      # Raspberry Pi Pico 2 (RP2350, Cortex-M33) image
make amd64      # AMD64 bare-metal image (QEMU x86_64 "pc")
```

Every embedded target (`arm32`, `rpi-zero`, `pico`, `pico2`, `amd64`) also
produces `SparseKernel.img3` next to its kernel image. That sparse payload is
what the SYSGOTH bootloader streams over the UART, so a board can be reflashed
over the serial link alone (`sysgoth/main.cpp` is the host side).

`make test` runs `build/kern.ldr` several times. It boots the init script and
the shell, which read from stdin. Always run it as `make test </dev/null` in a
non-interactive session, otherwise the shell blocks forever waiting on a TTY.

The ARM/Pico/RPi toolchains are not installed in the default dev container, so
`make arm32` / `make rpi-zero` / `make pico` cannot be exercised here, and
`qemu-system-x86` is not installed so `make test-amd64` cannot run either.
Syntax-check bare-metal port changes with the host compiler instead:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -ffreestanding -fno-builtin \
   -Iinclude -Ipexpert -Ipexpert/arm -fsyntax-only pexpert/arm/pe_arm32.c
cc -std=c11 -Wall -Wextra -Werror -pedantic -ffreestanding -fno-builtin \
   -Iinclude -Ipexpert -Ipexpert/arm -fsyntax-only pexpert/arm/pe_cortexm.c
cc -std=c11 -Wall -Wextra -Werror -pedantic -ffreestanding -fno-builtin \
   -Iinclude -Ipexpert -Ipexpert/amd64 -fsyntax-only pexpert/amd64/pe_amd64.c
```

## Layout

- `rtos/` — kernel core: `kernel.c` (lifecycle/policy wiring), `sched.c`
  (task scheduler), `ipc.c` (endpoints), `syscall.c` (syscall dispatch).
  `userapi.c` is the userland support library that lives beside the kernel but
  is not part of it. All of `rtos/` is shared by the user-mode and bare-metal
  builds, so it must stay freestanding.
- `include/gothrtos/` — public headers. `userapi.h` is the userland surface.
- `libkapi/kapi.c` — thin kernel API wrapper over the syscalls.
- `pexpert/` — platform experts. `common.c` dispatches to per-platform ops
  (`pexpert/um/pe_linux.c`, `pexpert/um/pe_w64.c`, `pexpert/arm/`,
  `pexpert/amd64/`). The ops table lives in `pexpert/pe_internal.h`; add new
  ops to every port. The bare-metal ports each have a `kern_*.c` entry in
  `os/loader/` (`kern_arm.c`, `kern_cm.c`, `kern_amd64.c`) that boots the
  nanokernel and runs two demo tasks.
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
  (`SOURCES`, `ARM_C_SOURCES`, `RPI_C_SOURCES`, `PICO_C_SOURCES`,
  `AMD64_C_SOURCES`) and to the matching header dependency lists.
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
- `tests/test_kernlib.lua`, `tests/test_userkernel.lua` — the kernel Lua
  library and the UserKernel modes/profiles/services; run standalone and
  through the kernel.
- `tests/test_sparsekernel.c`, `tests/test_sysgoth.c` — the SparseKernel.img3
  format and the SYSGOTH loader core.
