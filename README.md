# GothRTOS

GothRTOS is a small, statically allocated microkernel written in C11 with a
lua-oriented userspace execution.

> The name just started as a joke and I was like "Huh, why not?" and now there
> it is.

GothRTOS runs in two configurations:

- **User mode (UM)** on Linux or Windows, where the kernel scheduler is hosted
  by the operating system.
- **Bare metal ARM32** on QEMU's `virt` machine (Cortex-A15) and on a
  Raspberry Pi Zero (BCM2835, ARM1176JZF-S), with all console output going over
  a PL011 serial port.

## Building and testing the user-mode kernel

```sh
make          # builds build/kern.ldr plus the host libraries and build/lua
make test     # runs the host test suite
```

The host build produces:

- `build/libgothrtos.a` — kernel, IPC, syscall, userland API and pexpert code
- `build/libkapi.a` — kernel API
- `build/libgothc.a` — freestanding C library
- `build/liblua.a` and `build/lua` — Lua 5.5.1 and a standalone interpreter
- `build/kern.ldr` — the user-mode kernel loader

`build/kern.ldr` accepts a sysgoth-style command line, for example:

```sh
build/kern.ldr -kern_sym=/kern.symbols -verbose=0 \
    -root=/img/root.bin -mem=dynamic -drv_root=/kern.builts
```

`-init=PATH` runs a Lua init script. The default boot script lives at
`os/init/init.d/init.lua` and loads the other init modules relative to itself,
so it works from the source tree or an installed `/kern/init.d` image:

```sh
build/kern.ldr -init=os/init/init.d/init.lua
```

Kernel command-line options are passed to the init script as Lua `arg` entries
in `key=value` form and are readable through `base:getOption("root")`.
`os.lua` (also published as `gothos`), `path.lua`, `kernel_mounts.lua`,
`kernelmode.lua`, `sh.lua` and `userapi.lua` provide the boot environment.

## The GothRTOS userland API

GothRTOS has no POSIX, UNIX or JME surface. The nanokernel only schedules
cooperative tasks and moves messages between endpoints; everything else is
usermode policy, and the userland is Lua. The OS API is therefore a set of Lua
modules under `os/init/init.d/base/`:

- `base/io` — console: `write`, `puts`, `println`, `print`, `printf`, `read`,
  `clear`
- `base/sys` — clock and task control: `ticks`, `timeNs`, `timeMs`, `sleep`,
  `yield`, `exit`
- `base/user` — identity and environment: `addUser`, `removeUser`, `setUser`,
  `currentUser`, `getUser`, `isRoot`, `getUid`, `getGid`, `getEnv`, `setEnv`
- `base/proc` — the process table: `spawn`, `getTask`, `listTasks`, `kill`,
  `reap`
- `base/ipc` — userland message endpoints: `create`, `destroy`, `send`,
  `receive`
- `base/app` — application registry: `register`, `unregister`, `lookup`,
  `list`, `run`, `spawn`
- `base/std` — re-exports all of the above under one table

`userapi.lua` is the composed facade the boot script and the shell use; it
wraps the same calls, records the last failure for `:getError()`, and returns
`true` or `(false, message)`. Lookups return `nil` when nothing matches.

Each `base/*` module works in two places. Inside the kernel it forwards to the
userland binding the kernel publishes (the `goth` table, built by
`os/init/gothlib.c`), so scripts and the kernel share one set of tables. Under
the standalone `build/lua` interpreter, or any host without the binding, it
falls back to a pure-Lua equivalent, so the modules stay usable and testable on
their own. `base/io.read` is line oriented: one console line with the trailing
newline stripped, an empty string for a blank line, and `nil` at end of input.

The C side of the binding lives in `rtos/userapi.c`
(`include/gothrtos/userapi.h`) and mirrors the same operations for user
programs written in C; the Lua modules are the primary API and the C library
exists so `gothlib.c` and C user programs have something to call.

## Bare-metal ARM32 on QEMU

The ARM32 port targets a generic ARMv7-A machine. It uses:

- QEMU `virt` with a Cortex-A15 CPU
- the PL011 UART at `0x09000000` for all console output
- the ARM generic timer for kernel time
- cooperative context switching on the task stacks
- PSCI `SYSTEM_OFF` over `HVC` to power the machine off when the demo finishes

Build the image:

```sh
make arm32
```

This produces `build/arm32/kern.elf` (ELF32, entry `0x40010000`) and a raw
`build/arm32/kern.bin`. The build uses `arm-none-eabi-gcc` and does not link
against a C library; `pexpert/arm/arm32_libc.c` provides the small set of
`mem*`/`str*` routines the kernel needs.

Run it:

```sh
make run-arm32
```

or invoke QEMU directly:

```sh
qemu-system-arm -M virt -cpu cortex-a15 -m 128M -nographic \
    -semihosting-config enable=on,target=native \
    -kernel build/arm32/kern.elf
```

The serial console prints the boot banner, platform information, two demo
tasks, and a halt report. `-nographic` connects the PL011 output to the
terminal; the kernel does not use the semihosting console.

Run the automated boot check:

```sh
make test-arm32
```

`tests/run_arm32_qemu.sh` starts QEMU with a 20-second timeout, captures the
serial log in `build/arm32/qemu.log`, and verifies the banner, platform line,
both demo tasks, and the halt report. If `qemu-system-arm` is not installed the
script reports `SKIP` instead of failing, so `make arm32` still works on
machines without QEMU. Set `QEMU_ARM` to point at a QEMU binary in a non-default
location.

## Bare-metal ARM32 on a Raspberry Pi Zero

The Raspberry Pi build targets the revision 1.2 Pi Zero: a BCM2835 SoC with a
single ARM1176JZF-S core (ARMv6K, 512 MB RAM). It runs from the SD card with no
firmware services or C library and writes its console to UART0 on GPIO14/15.

The port uses:

- `kernel.img` loaded by the Pi firmware at `0x8000`
- the PL011 UART0 at `0x20201000`, with GPIO14/15 muxed to ALT0 and configured
  for 115200 baud, 8N1
- the BCM2835 system timer at `0x20003000` (1 MHz) for kernel time
- the ARMv6 exception-vector layout: the vector table is copied to address `0x0`
  at boot because ARMv6 has no `VBAR`
- cooperative context switching on the task stacks
- a permanent halt loop when the demo finishes, since ARM1176 has no PSCI and
  semihosting is unavailable on the bare-metal target

Build the image:

```sh
make rpi-zero
```

This produces `build/rpi-zero/kernel.elf` (ELF32, entry `0x8000`) and
`build/rpi-zero/kernel.img`. The build uses `arm-none-eabi-gcc` with
`-mcpu=arm1176jzf-s -marm -mfloat-abi=soft` and does not link against a C
library.

### Writing an SD card

Prepare a FAT32 boot partition containing the standard Pi Zero firmware files
(`bootcode.bin`, `start.elf`, and `fixup.dat`), plus a `config.txt` and the
kernel image:

```ini
# config.txt
enable_uart=1
core_freq=250
```

```sh
cp build/rpi-zero/kernel.img /media/$USER/boot/kernel.img
```

`core_freq=250` keeps the core clock (and therefore the UART clock) at the
250 MHz default the baud divisor is calculated from. If you use a different
`core_freq`, rebuild with the matching value:

```sh
make rpi-zero GOTH_ARM32_BCM2835_UART_CLOCK=400000000
```

Connect a 3.3 V USB-serial adapter to the GPIO header:

- adapter RX to GPIO14 / pin 8 (Pi TX)
- adapter TX to GPIO15 / pin 10 (Pi RX)
- adapter GND to pin 6 (GND)

Open the port at 115200 baud, 8 data bits, no parity, 1 stop bit. The console
prints the boot banner, platform information, two demo tasks, and a halt
report.

### Running under QEMU

`make run-rpi-zero` boots the same image with QEMU's `raspi0` machine, or
invoke QEMU directly:

```sh
qemu-system-arm -M raspi0 -cpu arm1176 -m 512M -nographic \
    -kernel build/rpi-zero/kernel.elf
```

Run the automated boot check:

```sh
make test-rpi-zero
```

`tests/run_rpi_qemu.sh` starts QEMU in the background, captures the serial log
in `build/rpi-zero/qemu.log`, waits for the halt report, and then stops the
machine. It verifies the banner, platform line, both demo tasks, and the halt
report. If `qemu-system-arm` is not installed the script reports `SKIP` instead
of failing. Set `QEMU_ARM` to point at a QEMU binary in a non-default location.

### Layout

- `pexpert/arm/arm32_start.S` — reset entry, vector table (copied to `0x0` on
  ARMv6 or installed via `VBAR` on ARMv7), BSS clear, boot stack, and the
  context-switch/trampoline stubs
- `pexpert/arm/pe_arm32.c` — shared cooperative scheduler, port hooks, and
  console/platform glue used by every ARM32 board
- `pexpert/arm/pe_arm32.h` — the board-ops interface between the shared port
  and board-specific code
- `pexpert/arm/pe_arm32_qemu.c` — QEMU `virt` PL011, generic timer, and PSCI
  exit
- `pexpert/arm/pe_bcm2835.c` — Pi Zero UART0, GPIO mux, system timer, and halt
  exit
- `pexpert/arm/arm32.ld` — links the QEMU `virt` image at `0x40010000`
- `pexpert/arm/bcm2835.ld` — links the Pi image at `0x8000`
- `pexpert/arm/arm32_libc.c` — freestanding string/memory helpers
- `pexpert/arm/um_internal.h` — declarations for the scheduler port hooks
- `os/loader/kern_arm.c` — bare-metal `kmain` and the demo tasks


The kernel core in `rtos/` is shared between the user-mode and bare-metal
builds. It calls the same `goth_um_*` port hooks in both cases; only the
platform implementation behind those hooks differs.
