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
