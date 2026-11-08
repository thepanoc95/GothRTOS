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

## Driver creation

Create a C skeleton that uses the kernel's static driver model:

```sh
bun run osbuild/src/cli.ts --create-driver bcm2835-uart \
  --compatible brcm,bcm2835-pl011 \
  --output-dir drivers
```

Repeat `--compatible` for additional compatible identifiers. OSBuild refuses
to overwrite an existing source file. The generated `probe` intentionally
returns `GOTH_STATUS_PLATFORM_ERROR` until the hardware initialization TODO is
implemented. Add the source to the relevant GNUmakefile source list, statically
allocate a `goth_driver_t`, call the generated `goth_<name>_driver_init()`, then
register it with `goth_driver_register()` during platform startup.

## UART image upload

On Linux or macOS with `stty`, upload a prebuilt SparseKernel v3 image to a
device running the repository's SYSGOTH bootloader:

```sh
bun run osbuild/src/cli.ts --upload /dev/ttyACM0 \
  --image build/pico2/SparseKernel.img3
```

OSBuild validates the v3 header and every DATA chunk CRC before opening the
serial port, configures it for raw 115200 baud, and streams the raw image bytes.
SYSGOTH scans for the image magic; there is no handshake. Verify that the image
architecture matches the connected hardware and put the target into its
SYSGOTH receive mode first. The transfer reports bytes transmitted, not device
acceptance or boot success. This checkout does not build the Pico/Pico 2
SparseKernel images; use an image produced by a compatible build.

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
| `src/upload.ts` | SparseKernel v3 validation and raw SYSGOTH UART streaming |
| `src/runner.ts` | `make` command construction and environment matrix |
| `src/process.ts` | Streaming `make` output (line pumping, ANSI stripping) |
| `src/tui.ts` | OpenTUI render tree, key handling and actions |
| `src/cli.ts` | CLI entry point (`--list`, `--list-ports`, `--jobs`, `--extra`) |
| `tests/` | Bun tests for the registry, runner, output streaming and TUI |

## Test

```sh
bun test osbuild/tests
# or
make test-osbuild
# or
make test
```
