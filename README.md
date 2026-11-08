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

## Source layout

- `rtos/` contains the freestanding kernel core, including the hardware driver
  framework (`hdf.c`); public interfaces live under `include/gothrtos/`.
- `pexpert/` contains platform-specific ports and linker scripts.
- `kernel/` and `os/` contain Lua userland services, boot modules, and loaders.
- `libc/`, `libkapi/`, and `lib/` contain the C library, syscall wrapper, and
  reusable Klib components, respectively.
- `System/` contains the GothRTOS UI bindings and vendored LVGL sources;
  `osbuild/` contains the build-system UI.
- `tests/` contains the host test suite.

## GothRTOS userland UI

`System/ui.lua` provides a dark-first UI toolkit and a single-owner foreground
UI service over the existing GothGfx framebuffer and input API. The service
owns display polling, focused-app event routing, and redraw/present cycles;
see [`System/ui/README.md`](System/ui/README.md) for the API and hardware
capability limits. The framework is intentionally software-rendered and does
not add a display or input driver to bare-metal targets.
