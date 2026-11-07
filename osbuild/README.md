# OSBuild — GothRTOS System Builder

A terminal UI for the GothRTOS build system, written in TypeScript for Bun and
built on [OpenTUI](https://opentui.com). It keeps the non-interactive registry
and make runner from the previous front end.

## Run

Install the TUI dependencies once:

```sh
cd osbuild && bun install
```

Then, from the repository root:

```sh
make build-ui

# or directly
bun run osbuild/src/cli.ts

# choose parallel jobs or pass extra make arguments
bun run osbuild/src/cli.ts --jobs 8 --extra V=1
```

Headless listings still work:

```sh
bun run osbuild/src/cli.ts --list
bun run osbuild/src/cli.ts --list-ports
```

## Keys

| Key | Action |
| --- | --- |
| `1`–`4` | Boards, Ports, Environment, Scaffold |
| `[` / `]` | Cycle views |
| `j` / `k`, arrows | Select the next/previous board |
| `b` / `r` / `t` / `c` | Build, run, test, clean the selected board |
| `x` | Cancel the running command |
| `C` (Shift+C) | Clear the output pane |
| `PgUp` / `PgDn` | Scroll the output pane |
| `g` / `G` | Jump to the top/bottom of the output pane |
| `q` / `Ctrl+C` | Quit |

In the scaffold view, `Tab` moves between fields, `Ctrl+S` writes the port and
`Esc` returns to the board view.

## What it does

- Lists every board in `src/registry.ts`, its architecture, port, devices,
  support checks and SparseKernel output.
- Streams `make` output into the output pane while a build, run, test or clean
  is in progress, with cancel and clear controls.
- Lists the platform experts discovered under `pexpert/`.
- Shows the architecture × host environment matrix and the GNUmakefile's
  phony targets.
- Scaffolds a new port skeleton under `pexpert/<arch>/`.

The terminal palette borrows the butterfly design tokens from
`assets/butterfly.so.css`.

## Layout

| Path | Purpose |
| --- | --- |
| `src/registry.ts` | Board/device data, port discovery, makefile introspection, port scaffolder |
| `src/runner.ts` | `make` command construction and environment matrix |
| `src/process.ts` | Streaming `make` output (line pumping, ANSI stripping) |
| `src/tui.ts` | OpenTUI render tree, key handling and actions |
| `src/cli.ts` | CLI entry point (`--list`, `--list-ports`, `--jobs`, `--extra`) |
| `tests/` | Bun tests for the registry, runner, output streaming and TUI |

## Test

```sh
bun test osbuild/tests
# or
make test
```
