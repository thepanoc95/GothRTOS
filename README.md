# GothRTOS

GothRTOS is a small, statically allocated microkernel foundation written in C11.
The core provides task lifecycle management, a priority-based round-robin
scheduler, tick-based sleep, and bounded message endpoints. It does not allocate
memory dynamically.

## Build and test

Use a C11 compiler, GNU Make, and an archiver:

```sh
make
make test
```

The static library is written to `build/libgothrtos.a` and the user-mode
kernel executable is written to `build/kern.ldr`. `make clean` removes the
generated build directory.

The user-mode backend is selected from the host OS: Windows builds use the
Windows fiber backend and Linux builds use the POSIX `ucontext` backend. Set
`UM_PLATFORM=windows` or `UM_PLATFORM=linux` to override the selection when
cross-building.

## Kernel binary

`build/kern.ldr` is the user-mode kernel launcher. `make kernel` builds just
this executable. It accepts boot-loader style options in the form used by
`os/loader/sysgoth.cfg`, either as separate arguments or as one
whitespace-separated command line:

```sh
build/kern.ldr -kern_sym=/kern.symbols -verbose=1 -root=/img/root.bin \
    -mem=dynamic -drv_root=/kern.builts
```

Options may use one or two leading dashes. `-key=value` stores a value, while a
bare `-key` stores a flag; a single-value `-verbose` or `-debug` enables the
corresponding setting. `-verbose` prints the parsed command line, and `-help`
prints usage. Unknown options are retained rather than rejected. The launcher
initializes pexpert and the kernel, runs an optional Lua init script, starts a
boot task, and runs the cooperative scheduler. It parses but does not yet act on
`-kern_sym`, `-root`, `-drv_root`, or `-mem`; those are reserved for the future
loader and init pipeline.

`-init=PATH` runs a Lua script after kernel initialization and before the boot
task starts. A bare `-init` uses `os/init/init.d/init.lua`. The script executes
in the same process through `goth_lua_exec_file()`; a compile or runtime error
is reported on stderr and makes `kern.ldr` exit with a nonzero status.

```sh
build/kern.ldr -verbose=1 -init=tests/test_system.lua
```

The allocation-free parser is available through `gothrtos/cmdline.h`.
`goth_cmdline_parse_args()` reads null-terminated argument arrays, while
`goth_cmdline_parse_string()` tokenizes a mutable string in place. Duplicate
keys are allowed and lookup returns the last occurrence. The parser stores
pointers into the caller-provided argument strings, so they must remain valid
for as long as the parsed `goth_cmdline_t` is in use.

## Boot init scripts

The default init script is `os/init/init.d/init.lua`. It loads its sibling
modules relative to its own location, so it runs from the source tree or from
an installed `/kern/init.d` image without depending on the process working
directory. Kernel command-line options are passed to the script as Lua `arg`
entries in `key=value` form (for example `root=/img/root.bin`), and can be read
with `base:getOption("root")`.

The init modules are:

- `os.lua` — base OS state: name, version, build, root, arguments, memory
  policy, error state, and `halt`/`reboot`. Because Lua's standard library owns
  the module name `os`, init.lua loads this file directly and republishes it as
  `gothos`.
- `path.lua` — portable path normalization and `join`, `dirname`, `basename`,
  `extname`, and `exists` helpers.
- `kernel_mounts.lua` — logical mount table with `declare`, `mount`,
  `mountAll`, `unmount`, `isMounted`, and longest-prefix `find`.
- `kernelmode.lua` — `/kern` namespace bridge. In the user-mode build it serves
  kernel files from the source tree, including
  `/kern/init.d/sys/System.lua` and `/kern/init.d/*.lua`.
- `sh.lua` — shell front end with `execute`, `source`, `run`, and an optional
  `interactive` REPL. `run` does not block on input.
- `userapi.lua` — user identity, environment, and task bookkeeping with
  `setUser`, `getEnv`/`setEnv`, `spawn`, `kill`, and `reap`.

Boot mounts `/` from `-root` (default `/img/root.bin`) and `/drivers` from
`-drv_root` (default `/kern.builts`). These mounts are logical records in the
current user-mode kernel; they do not attach host block devices. Likewise,
`userapi.lua` records spawn requests instead of creating real processes. The
non-interactive shell run completes without waiting for terminal input, so
`kern.ldr -init=os/init/init.d/init.lua` is safe in automated builds.

## Kernel integration

Include `gothrtos/kernel.h`, call `goth_kernel_init()`, then create tasks with
caller-owned task objects and stacks. Task objects must initially be zero
initialized (for example, `static goth_task_t worker = {0};`). Larger numeric
priority values are scheduled before smaller values; tasks at the same priority
are selected round-robin.

The platform's timer interrupt should call `goth_kernel_tick()`. Cooperative
tasks can call `goth_task_yield()`, `goth_task_sleep(ticks)`, or
`goth_task_exit()`. Each returns the task selected by the scheduler, or `NULL`
if there is no runnable task. The board/CPU port is responsible for saving and
restoring CPU context when the returned task differs from the active one,
initializing each task's initial stack frame, and providing an idle context.
Serialize kernel API calls against timer interrupts in the port.

Endpoints use caller-provided message storage and are non-blocking: sends on a
full queue return `GOTH_STATUS_FULL`, and receives on an empty queue return
`GOTH_STATUS_EMPTY`. Messages include the sending task ID (zero if sent outside
a running task), a type, and two machine-word values.

## Flat Device Tree

`gothrtos/fdt.h` provides an allocation-free parser for FDT version 16 and 17
blobs. Pass the blob pointer and its available byte size to `goth_fdt_open()`,
then use the node, path, property, compatible-string, and reservation-map
accessors. The blob must remain available and unchanged while the parsed
`goth_fdt_t` is in use.

`goth_fdt_get_reg()` decodes a node's `reg` entries using its parent's
`#address-cells` and `#size-cells` properties (defaulting to 2 and 1); up to
two cells per value are supported. Returned addresses are relative to the
parent bus: `ranges` translation is not implemented. Header reservation-map
entries are exposed separately from `/reserved-memory` nodes, which can be
queried through the regular tree and `reg` accessors.

The loader configuration currently names DTB files, but no loader-to-kernel
handoff is wired yet. The platform must provide the blob pointer and size to
the parser; FDT data is not yet used to initialize the kernel memory map or
discover devices automatically.

## Platform expert

The pexpert layer isolates platform services behind `gothrtos/pexpert.h`.
Call `goth_pe_init()` before querying platform information, reading monotonic
nanoseconds with `goth_pe_time_ns()`, or writing bytes with
`goth_pe_console_write()`. The included user-mode backends use
`QueryPerformanceCounter`, processor information, and standard output on
Windows, and `CLOCK_MONOTONIC`, `sysconf`, and standard output on Linux. Add a
platform backend by implementing the internal operations table and selecting
its source in the build.

The pexpert services do not provide a hardware context switch, memory
protection, or blocking IPC. The current backend is a host/user-mode platform,
not a board support package.

## System calls

`gothrtos/syscall.h` defines a stable syscall number, four machine-word
arguments, and a status/value result. `goth_syscall_dispatch()` is the common
service dispatcher for yield, sleep, exit, tick queries, and endpoint
send/receive. Scheduler-changing calls return the selected task ID in `value`
(zero means no runnable task); other successful calls return their result or
zero as documented by the request type.

The current dispatcher is a direct C entry point, not yet a hardware syscall
instruction or privilege boundary. Endpoint arguments are pointers in the
kernel's address space. A platform trap handler must validate/copy user
arguments before dispatching if tasks are later isolated into separate address
spaces.

## 95exc executable format

`gothrtos/95exc.h` defines the version-1 95exc format and loader API; the
implementation is in `os/init/95exc.binfmt.c`. 95exc is a distinct format
inspired by ELF32, not an ELF32 file-layout extension. Its fixed-width fields
are little-endian, file offsets are 64-bit, and initial executable relocation
support targets i386. The parser rejects unsupported machine IDs rather than
trying to interpret them as i386.

All offsets below are byte offsets from the start of the file. The 64-byte
header fields are: `0` magic[8], `8` version u16, `10` header size u16, `12`
file type u16, `14` machine u16, `16` flags u32, `20` entry u32, `24` segment
table offset u64, `32` segment-entry size u16, `34` segment count u16, `36`
dynamic-table offset u64, `44` dynamic-table size u64, `52` metadata offset
u64, and `60` metadata size u32. Version 1 has 40-byte load-segment entries:
type u32, permissions u32, file offset u64, virtual address u32, file size
u32, memory size u32, alignment u32, compression ID u32, and reserved u32.
Header and segment entries may be extended by increasing their declared
sizes; unknown trailing bytes are ignored. Metadata is currently opaque and
reserved for a later versioned record schema.

The dynamic table consists of 16-byte tag u32 / flags u32 / value u64 records
and must end with a null record. `NEEDED` and `SONAME` values are string-table
offsets; string-table, symbol-table, and relocation-table values are virtual
addresses within the loaded image. Symbol entries are 20 bytes: name offset
u32, value u32, size u32, info u8, reserved u8, section index u16, version ID
u16, and reserved u16. Relocation entries are 12-byte RELA records: target
offset u32, symbol/type info u32 (symbol index in the high 24 bits, type in
the low 8), and explicit addend u32. Defined symbols use section index zero
for undefined and `0xfff1` for absolute. Global and weak symbol bindings are
1 and 2 in the high nibble of `info`. Dynamic symbol names are limited to
1023 bytes plus the terminator. The loader applies `R_386_32` as `S + A`,
`R_386_PC32` as `S + A - P`, `R_386_GLOB_DAT` and `R_386_JMP_SLOT` as `S`,
and `R_386_RELATIVE` as `B + A`, with 32-bit wraparound; `S` is the resolved
symbol, `A` the explicit addend, `P` the relocated target, and `B` the load
bias. The low nibble of symbol `info` is currently ignored.

`goth95_image_open()` validates the header and segment ranges without
allocating. `goth95_load()` uses caller-supplied callbacks to reserve guest
address space, transfer or zero segment contents, set final permissions,
resolve symbols, publish exports, and load dependencies. Executable files are
loaded at their specified addresses; shared objects and modules receive a
callback-selected load bias. Supported i386 RELA relocations are `R_386_32`,
`R_386_PC32`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, and `R_386_RELATIVE`.
Dependencies use repeated `NEEDED` tags; symbol versions are 16-bit IDs passed
to the resolver callback. Unknown optional dynamic tags can be skipped.
The reserve callback receives an inclusive minimum and exclusive maximum; its
returned bias must satisfy the requested alignment. `UINT32_MAX` as the
preferred bias means the backend may choose the placement. Keep the source
immutable through load/unload, and pass the same loader callbacks to both
operations so dependency and symbol registrations can be undone.

Compressed segments are delegated to the supplied decompressor callback and
temporary allocator; codec IDs other than zero are backend-defined. This
keeps codecs and memory policy out of the format core. Call
`goth95_unload()` to unpublish module symbols, release dependency
references, and release the mapped range. Dependency callbacks are responsible
for cycle handling, shared-library reference counts, and module registry
policy.

`goth95_export_elf32()` writes a loadable i386 ELF32 image through a caller
sink. It converts 95exc dynamic symbols and hash data, changes RELA records to
ELF32 REL by materializing each addend at its target, and emits standard
`PT_LOAD`/`PT_DYNAMIC` program headers. Nonzero 95exc symbol-version IDs are
rejected because the initial exporter does not encode ELF symbol-version
sections. Unknown optional 95exc dynamic tags and opaque metadata are omitted;
the exporter does not create ELF section or debug tables.

The loader never executes an entry point. The current UM kernel has no
per-process address spaces or executable mapping API, so callers must provide
a suitable backend; the returned entry address is for a future process
backend. The loader is not a security boundary: do not load untrusted images
until the backend supplies isolation, resource limits, and independent
validation appropriate to its threat model.

## KAPI library

`make` also builds `build/libkapi.a`. Include `gothrtos/kapi.h` and link the
library with `build/libgothrtos.a`. KAPI provides wrappers for the currently
implemented syscall services and starts statically declared programs from
caller-owned task and stack storage. Call `goth_kapi_start_programs()` after
`goth_kernel_init()`, then `goth_um_run()` to execute them. Program startup is
not a binary loader or privilege boundary; descriptors are ordinary in-process
C functions.

## Klib utility library

The vendored `lib` directory contains Klib, a set of C utility/data-structure
libraries; it is not a libc implementation. `make` builds the portable core
sources into `build/libklib.a`, using the C runtime supplied by the selected
host toolchain for standard functions such as allocation and string handling.
The source list intentionally leaves out optional file/network modules that
depend on zlib, curl, or host-specific interfaces outside the core user-mode
build. Klib's pthread-based helper is built against the host toolchain's
pthread support; it creates host threads, not GothRTOS tasks.

`make test` also runs a Klib smoke test covering strings, expressions, and
allocators. Override `KLIB_CPPFLAGS`, `KLIB_CFLAGS`, or `KLIB_LIBS` when using a
different host toolchain.

## GothRTOS C library

The separate freestanding C library is built as `build/libgothc.a`. Add
`libc/include` to the system-header search path and link this archive when
building a freestanding GothRTOS program. Its initial core provides memory and
string operations, `malloc`/`calloc`/`realloc`/`free`, integer parsing and
conversion, limits, and `errno`. It intentionally does not replace the host C
runtime used by the user-mode build and does not yet include formatted I/O,
files, environment handling, or process startup.

Before using allocation, provide a suitably aligned memory region with
`goth_libc_heap_init(memory, size)`. The allocator is first-fit, coalesces
adjacent free blocks, performs no dynamic backing allocation, and is currently
single-threaded/non-reentrant; initialize/reset it only when no allocations are
live. The standard symbols are intended for a freestanding link, not for
interposing on the host C runtime.

## Lua runtime

The Lua 5.5.1 source in `os/lua` is built into `build/liblua.a`, and
`os/init/luaexec.c` provides the GothRTOS embedding interface in
`gothrtos/luaexec.h`. `goth_lua_exec_file()` executes a script file, while
`goth_lua_exec_string()` executes a null-terminated source string. Both create
a fresh Lua state, open Lua's standard libraries, populate the global `arg`
table from the supplied arguments, execute the chunk, report errors to stderr,
and close the state. Link the runner object and Lua archive with the math
library (`-lm`).

`make lua` builds the standalone interpreter at `build/lua`; `make` also builds
it by default. `make test` runs the `System.lua` and init-module test scripts
both through `build/lua` and through `build/kern.ldr -init=...`.

This UM build uses the host C runtime for Lua's allocation, file, and standard
library facilities. It does not route Lua allocation through the freestanding
heap or provide an isolated/sandboxed Lua process.

## User-mode execution

The user-mode (UM) backend runs tasks cooperatively on the host process. After
kernel initialization and task creation, call `goth_um_run(tick_period_ms)` to
run until all tasks are terminated or no task is ready or sleeping. A task
yields with `goth_task_yield()`, sleeps with `goth_task_sleep(ticks)`, and is
marked terminated when its entry function returns or it calls
`goth_task_exit()`. Elapsed ticks are collected at cooperative task switches
and while the run loop waits for sleepers. Tasks that run without yielding are
not preempted. All kernel and UM run-loop calls must be made from the thread
that initialized the kernel.

On Windows, tasks are cooperative Windows fibers. Windows allocates fiber
stacks; the stack pointer passed to `goth_task_create()` is retained as task
metadata but is not used as the fiber's execution stack.

On Linux, tasks are `ucontext` contexts with heap-allocated stacks of at least
64 KiB. As on Windows, the stack passed to `goth_task_create()` is retained as
task metadata rather than used as the execution stack. The Linux backend uses
`CLOCK_MONOTONIC` for tick collection and `nanosleep()` while waiting for
sleeping tasks. `ucontext` is used only for cooperative scheduling here, not as
a portable context-switch or process-isolation mechanism.
