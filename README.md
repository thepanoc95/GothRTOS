# GothRTOS

GothRTOS is a small, statically allocated microkernel written in C11 with a
lua-oriented userspace execution.

> The name just started as a joke and I was like "Huh, why not?" and now there
> it is.

## Build system UI

`OSBuild` is a TypeScript/Bun terminal UI for browsing boards and ports,
checking toolchains, streaming builds, and scaffolding new platform experts:

```sh
make build-ui
bun run osbuild/src/cli.ts --list
```

See `osbuild/README.md` for details.

## LVGL and GothLibC

The vendored LVGL is version 9.2.2. Its configured C-library adapters use
GothLibC for allocation (`malloc`, `realloc`, `free`), string/memory
operations (`memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `strncpy`,
`strcpy`, `strcmp`, `strcat`, `strncat`), and formatting (`vsnprintf`).
These adapters let the freestanding LVGL archive link against GothLibC; the
normal GothLibC process entry initializes its heap before LVGL allocation.

`make test </dev/null` includes a freestanding LVGL smoke test that boots via
GothLibC, initializes LVGL, exercises its allocator and formatter, and runs on
the Linux host. It does not exercise a physical display or device driver.

## GothRTOS userland UI

`System/ui.lua` provides a dark-first UI toolkit and a single-owner foreground
UI service over the existing GothGfx framebuffer and input API. The service
owns display polling, focused-app event routing, and redraw/present cycles;
see [`System/ui/README.md`](System/ui/README.md) for the API and hardware
capability limits. The framework is intentionally software-rendered and does
not add a display or input driver to bare-metal targets.
