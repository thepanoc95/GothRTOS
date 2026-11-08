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

## GothRTOS userland UI

`System/ui.lua` provides a dark-first UI toolkit and a single-owner foreground
UI service over the existing GothGfx framebuffer and input API. The service
owns display polling, focused-app event routing, and redraw/present cycles;
see [`System/ui/README.md`](System/ui/README.md) for the API and hardware
capability limits. The framework is intentionally software-rendered and does
not add a display or input driver to bare-metal targets.
