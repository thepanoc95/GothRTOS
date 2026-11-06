"""Board, port and device registry for the GothRTOS build system UI.

This module is the non-interactive core of OSBuild.py: it knows what targets
the GNUmakefile can build, which pexpert port backs each one, which linker
script and architecture it uses, and which device configuration it selects. The
Textual UI is a thin layer on top of this, so the same data also drives a
headless listing (``python OSBuild.py --list``).

Nothing here shells out to make: discovery is file-system based so the UI works
before any build has run.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable

REPO_ROOT = Path(__file__).resolve().parent.parent


# --------------------------------------------------------------------------- #
# Architecture descriptions
# --------------------------------------------------------------------------- #
@dataclass(frozen=True)
class Architecture:
    """A CPU architecture the build can target."""

    key: str
    display: str
    toolchain: str
    cpu: str = ""
    abi: str = ""

    @property
    def toolchain_installed(self) -> bool:
        import shutil

        return shutil.which(self.toolchain) is not None


ARCHITECTURES: dict[str, Architecture] = {
    "um": Architecture(
        key="um",
        display="User mode (host)",
        toolchain="cc",
        cpu="host",
    ),
    "arm32": Architecture(
        key="arm32",
        display="ARM32 (A-profile)",
        toolchain="arm-none-eabi-gcc",
        cpu="cortex-a15",
        abi="soft",
    ),
    "armv6m": Architecture(
        key="armv6m",
        display="ARMv6-M (Cortex-M0+)",
        toolchain="arm-none-eabi-gcc",
        cpu="cortex-m0plus",
        abi="soft",
    ),
    "armv8m": Architecture(
        key="armv8m",
        display="ARMv8-M (Cortex-M33)",
        toolchain="arm-none-eabi-gcc",
        cpu="cortex-m33",
        abi="soft",
    ),
    "amd64": Architecture(
        key="amd64",
        display="AMD64 (x86-64)",
        toolchain="cc",
        cpu="x86-64",
    ),
}


# --------------------------------------------------------------------------- #
# Devices
# --------------------------------------------------------------------------- #
@dataclass(frozen=True)
class Device:
    """A peripheral described by the Hardware Driver Foundation."""

    name: str
    compatible: str
    hdf_class: str
    description: str = ""


# The devices the current ports publish. Kept in sync with the HDF
# registrations in pexpert/arm/pe_bcm2835.c, pexpert/arm/pe_rp2040.c and
# pexpert/amd64/pe_amd64_qemu.c.
DEVICE_CATALOG: list[Device] = [
    Device("bcm2835-uart0", "brcm,bcm2835-pl011", "console",
           "PL011 UART on the BCM2835 platform bus"),
    Device("bcm2835-systimer", "brcm,bcm2835-system-timer", "timer",
           "1 MHz free-running system timer"),
    Device("uart0", "raspberrypi,rp2040-uart", "console",
           "PL011 UART on GPIO0/1 (Pico)"),
    Device("timer0", "raspberrypi,rp2040-timer", "timer",
           "Free-running microsecond timer (Pico)"),
    Device("uart0", "raspberrypi,rp2350-uart", "console",
           "PL011 UART on GPIO0/1 (Pico 2)"),
    Device("timer0", "raspberrypi,rp2350-timer", "timer",
           "Free-running microsecond timer (Pico 2)"),
    Device("com1", "ns16550a", "console",
           "16550 COM1 serial on the QEMU pc machine"),
    Device("tsc", "x86,tsc", "timer",
           "Time-stamp counter used as the monotonic clock"),
]


# --------------------------------------------------------------------------- #
# Boards / targets
# --------------------------------------------------------------------------- #
@dataclass(frozen=True)
class Board:
    """A build target: one board plus the port that supports it."""

    key: str
    display: str
    arch: str
    make_target: str
    run_target: str
    test_target: str
    build_dir: str
    port_dir: str
    linker_script: str
    image: str
    sparse_image: str = ""
    devices: tuple[str, ...] = ()
    device_config: str = ""
    notes: str = ""

    @property
    def architecture(self) -> Architecture:
        return ARCHITECTURES[self.arch]


BOARDS: list[Board] = [
    Board(
        key="um",
        display="User mode (host)",
        arch="um",
        make_target="all",
        run_target="",
        test_target="test",
        build_dir="build",
        port_dir="pexpert/um",
        linker_script="",
        image="build/kern.ldr",
        device_config="os/config/OSConfig.lua",
        notes="Host build: libgothrtos.a, libkapi.a, libc, lua, kern.ldr.",
    ),
    Board(
        key="arm32",
        display="ARM32 QEMU virt",
        arch="arm32",
        make_target="arm32",
        run_target="run-arm32",
        test_target="test-arm32",
        build_dir="build/arm32",
        port_dir="pexpert/arm",
        linker_script="pexpert/arm/arm32.ld",
        image="build/arm32/kern.bin",
        sparse_image="build/arm32/SparseKernel.img3",
        devices=(),
        notes="Cortex-A15 under QEMU virt, semihosting console.",
    ),
    Board(
        key="rpi-zero",
        display="Raspberry Pi Zero (BCM2835)",
        arch="arm32",
        make_target="rpi-zero",
        run_target="run-rpi-zero",
        test_target="test-rpi-zero",
        build_dir="build/rpi-zero",
        port_dir="pexpert/arm",
        linker_script="pexpert/arm/bcm2835.ld",
        image="build/rpi-zero/kernel.img",
        sparse_image="build/rpi-zero/SparseKernel.img3",
        devices=("brcm,bcm2835-pl011", "brcm,bcm2835-system-timer"),
        device_config="os/config/Device/raspberrypi/",
        notes="ARM1176JZF-S / ARMv6K, PL011 UART, system timer.",
    ),
    Board(
        key="pico",
        display="Raspberry Pi Pico (RP2040)",
        arch="armv6m",
        make_target="pico",
        run_target="",
        test_target="",
        build_dir="build/pico",
        port_dir="pexpert/arm",
        linker_script="pexpert/arm/cm.ld",
        image="build/pico/kernel.elf",
        sparse_image="build/pico/SparseKernel.img3",
        devices=("raspberrypi,rp2040-uart", "raspberrypi,rp2040-timer"),
        device_config="",
        notes="Cortex-M0+, flat SRAM image, SYSGOTH streams img3 over UART.",
    ),
    Board(
        key="pico2",
        display="Raspberry Pi Pico 2 (RP2350)",
        arch="armv8m",
        make_target="pico2",
        run_target="",
        test_target="",
        build_dir="build/pico2",
        port_dir="pexpert/arm",
        linker_script="pexpert/arm/cm.ld",
        image="build/pico2/kernel.elf",
        sparse_image="build/pico2/SparseKernel.img3",
        devices=("raspberrypi,rp2350-uart", "raspberrypi,rp2350-timer"),
        device_config="os/config/Device/raspberrypi/pico2.lua",
        notes="Cortex-M33, flat SRAM image, SYSGOTH streams img3 over UART.",
    ),
    Board(
        key="amd64",
        display="AMD64 QEMU pc",
        arch="amd64",
        make_target="amd64",
        run_target="run-amd64",
        test_target="test-amd64",
        build_dir="build/amd64",
        port_dir="pexpert/amd64",
        linker_script="pexpert/amd64/amd64.ld",
        image="build/amd64/kernel.bin",
        sparse_image="build/amd64/SparseKernel.img3",
        devices=("ns16550a", "x86,tsc"),
        notes="Multiboot1 ELF64, COM1 serial, TSC clock.",
    ),
]


def board_by_key(key: str) -> Board:
    for board in BOARDS:
        if board.key == key:
            return board
    raise KeyError(key)


def devices_for(board: Board) -> list[Device]:
    """Returns the Device objects a board publishes, resolved by compatible
    string (device names are not unique across boards)."""
    resolved: list[Device] = []
    for compatible in board.devices:
        for device in DEVICE_CATALOG:
            if device.compatible == compatible:
                resolved.append(device)
                break
    return resolved


# --------------------------------------------------------------------------- #
# Port discovery
# --------------------------------------------------------------------------- #
@dataclass
class Port:
    """A platform expert on disk, discovered from the tree."""

    directory: str
    arch: str
    sources: list[str] = field(default_factory=list)
    headers: list[str] = field(default_factory=list)
    linker_scripts: list[str] = field(default_factory=list)
    assembly: list[str] = field(default_factory=list)

    @property
    def name(self) -> str:
        return Path(self.directory).name


def _arch_for_port(directory: Path) -> str:
    name = directory.name
    if name == "um":
        return "um"
    if name == "amd64":
        return "amd64"
    if name == "i386":
        return "i386"
    if name == "arm":
        # The ARM directory holds several architecture families; classify by
        # the files it contains.
        names = {entry.name for entry in directory.iterdir()}
        if any(entry.startswith("pe_cortexm") or entry.startswith("cm_")
               for entry in names):
            return "armv6m"
        return "arm32"
    return "unknown"


def discover_ports(root: Path | None = None) -> list[Port]:
    """Walks ``pexpert/`` and describes every port directory it finds."""
    base = (root or REPO_ROOT) / "pexpert"
    ports: list[Port] = []
    if not base.is_dir():
        return ports

    for directory in sorted(p for p in base.iterdir() if p.is_dir()):
        if directory.name.startswith("."):
            continue
        port = Port(directory=str(directory.relative_to(root or REPO_ROOT)),
                    arch=_arch_for_port(directory))
        for entry in sorted(directory.iterdir()):
            if not entry.is_file():
                continue
            rel = str(entry.relative_to(root or REPO_ROOT))
            if entry.suffix == ".c":
                port.sources.append(rel)
            elif entry.suffix == ".h":
                port.headers.append(rel)
            elif entry.suffix == ".ld":
                port.linker_scripts.append(rel)
            elif entry.suffix == ".S":
                port.assembly.append(rel)
        ports.append(port)
    return ports


# --------------------------------------------------------------------------- #
# Board support matrix
# --------------------------------------------------------------------------- #
def board_support(board: Board) -> list[tuple[str, bool, str]]:
    """Returns (check, ok, detail) rows describing whether a board can build.

    Checks the toolchain, the port directory, the linker script and the source
    list, without invoking make.
    """
    rows: list[tuple[str, bool, str]] = []
    arch = board.architecture

    toolchain_ok = arch.toolchain_installed
    rows.append(("Toolchain", toolchain_ok,
                 f"{arch.toolchain}" + ("" if toolchain_ok else " (missing)")))

    port_path = REPO_ROOT / board.port_dir
    rows.append(("Port", port_path.is_dir(), board.port_dir))

    if board.linker_script:
        script = REPO_ROOT / board.linker_script
        rows.append(("Linker script", script.is_file(), board.linker_script))

    sources = port_sources(board)
    rows.append(("Port sources", bool(sources), f"{len(sources)} file(s)"))

    return rows


def port_sources(board: Board) -> list[str]:
    """Returns the pexpert sources the board's port contributes."""
    port_path = REPO_ROOT / board.port_dir
    if not port_path.is_dir():
        return []
    return sorted(
        str(entry.relative_to(REPO_ROOT))
        for entry in port_path.iterdir()
        if entry.is_file() and entry.suffix in {".c", ".S"}
    )


# --------------------------------------------------------------------------- #
# GNUmakefile introspection
# --------------------------------------------------------------------------- #
_MAKE_TARGET_RE = re.compile(r"^([A-Za-z0-9][A-Za-z0-9_.-]*)\s*:(?!=)")


def makefile_targets(path: Path | None = None) -> list[str]:
    """Returns the phony target names declared in the GNUmakefile.

    This reads the ``.PHONY`` line rather than every rule, so it lists the
    user-facing entry points (arm32, pico, test, ...) and not the object rules.
    """
    makefile = path or (REPO_ROOT / "GNUmakefile")
    if not makefile.is_file():
        return []

    text = makefile.read_text(encoding="utf-8", errors="replace")
    targets: list[str] = []
    match = re.search(r"^\.PHONY:(.*?)(?:\n\S|\Z)", text, re.M | re.S)
    if match:
        for token in match.group(1).replace("\\\n", " ").split():
            if token and token not in targets:
                targets.append(token)
    if not targets:
        for line in text.splitlines():
            m = _MAKE_TARGET_RE.match(line)
            if m and not m.group(1).startswith("."):
                targets.append(m.group(1))
    return targets


def makefile_variable(name: str, path: Path | None = None) -> str | None:
    """Returns the first assignment of a make variable, with continuations
    joined and tabs collapsed. Returns None when the variable is absent."""
    makefile = path or (REPO_ROOT / "GNUmakefile")
    if not makefile.is_file():
        return None

    lines = makefile.read_text(encoding="utf-8", errors="replace").splitlines()
    value_parts: list[str] = []
    collecting = False
    for line in lines:
        if not collecting:
            match = re.match(rf"^{re.escape(name)}\s*[?:]?=\s*(.*)$", line)
            if not match:
                continue
            collecting = True
            value_parts.append(match.group(1))
        else:
            value_parts.append(line)
        if not line.rstrip().endswith("\\"):
            break
    if not collecting:
        return None
    joined = " ".join(part.rstrip("\\").strip() for part in value_parts)
    return " ".join(joined.split())


# --------------------------------------------------------------------------- #
# Device configuration discovery
# --------------------------------------------------------------------------- #
def discover_device_configs(root: Path | None = None) -> list[str]:
    """Lists the Lua device configuration files under os/config."""
    base = (root or REPO_ROOT) / "os" / "config"
    if not base.is_dir():
        return []
    return sorted(
        str(entry.relative_to(root or REPO_ROOT))
        for entry in base.rglob("*.lua")
    )


def all_boards() -> Iterable[Board]:
    return iter(BOARDS)


# --------------------------------------------------------------------------- #
# Script entry points
# --------------------------------------------------------------------------- #
def format_board(board: Board) -> str:
    """One-line summary of a board, with a readiness marker."""
    ready = all(ok for _, ok, _ in board_support(board))
    marker = "ready" if ready else "needs setup"
    return (f"{board.key:10} {board.display:35} "
            f"arch={board.arch:8} make={board.make_target:12} [{marker}]")


def format_device(device: Device) -> str:
    return (f"           device: {device.name} "
            f"({device.hdf_class}, {device.compatible})")


def format_port(port: Port) -> str:
    return (f"{port.directory:16} arch={port.arch:8} "
            f"sources={len(port.sources)} asm={len(port.assembly)} "
            f"ld={len(port.linker_scripts)}")


def list_boards(emit: Callable[[str], None] = print) -> None:
    """Prints every board and its devices, one board per block."""
    for board in BOARDS:
        emit(format_board(board))
        for device in devices_for(board):
            emit(format_device(device))


def list_ports(emit: Callable[[str], None] = print) -> None:
    """Prints every port discovered under ``pexpert/``."""
    for port in discover_ports():
        emit(format_port(port))


# --------------------------------------------------------------------------- #
# Port scaffolding
# --------------------------------------------------------------------------- #
PORT_TEMPLATE_C = '''#include "{include}"

#include "gothrtos/{helper}.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* {display} board port.
 *
 * Scaffolded by OSBuild.py. Fill in the register bases and the four hardware
 * operations below; the scheduler, pexpert glue and console formatting are
 * shared in pe_{ops_arch}.c. */

#define {prefix}_UART_BASE UINT32_C(0x00000000)
#define {prefix}_UART_BAUD UINT32_C(115200)

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t){prefix}_UART_BASE;

static void {prefix}_uart_init(void)
{{
    /* TODO: configure GPIO/pin muxing and the UART divisor for the clock. */
}}

static void {prefix}_uart_putc(char character)
{{
    (void)character;
    /* TODO: wait for space, then write the character to the data register. */
}}

static int {prefix}_uart_getc(void)
{{
    /* TODO: return the next received byte, or -1 when the FIFO is empty. */
    return -1;
}}

static uint64_t {prefix}_time_ns(void)
{{
    /* TODO: return the board's monotonic clock in nanoseconds. */
    return 0U;
}}

static void {prefix}_exit(int status)
{{
    (void)status;
    for (;;) {{
        goth_{ops_arch}_cpu_relax();
    }}
}}

static void {prefix}_hdf_register(void)
{{
    /* TODO: describe the board's devices and register them with the HDF. */
}}

static const goth_{ops_arch}_board_ops_t {prefix}_board_ops = {{
    "{display}",
    {prefix}_uart_init,
    {prefix}_uart_putc,
    {prefix}_uart_getc,
    {prefix}_time_ns,
    {prefix}_exit,
    {prefix}_hdf_register
}};

const goth_{ops_arch}_board_ops_t *goth_{ops_arch}_board_get_ops(void)
{{
    return &{prefix}_board_ops;
}}
'''

PORT_TEMPLATE_LD = '''ENTRY(_start)

/* {display} link script (scaffolded by OSBuild.py). */
SECTIONS
{{
    . = {load_address};
    __image_start = .;

    .text : ALIGN(4)
    {{
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }}

    .data : ALIGN(4)
    {{
        *(.data .data.*)
        . = ALIGN(4);
    }}

    .bss (NOLOAD) : ALIGN(4)
    {{
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }}

    .stack (NOLOAD) : ALIGN(8)
    {{
        __stack_bottom = .;
        . = . + 0x4000;
        __stack_top = .;
    }}

    . = ALIGN(8);
    __image_end = .;

    /DISCARD/ :
    {{
        *(.comment)
        *(.note*)
    }}
}}
'''

PORT_TEMPLATE_README = '''# {display} port

Scaffolded by `OSBuild.py`. This directory is not yet wired into the
`GNUmakefile`; add its sources to the matching `*_C_SOURCES` list and its
header dependencies to the matching `*_HEADERS` list.

Checklist:

- [ ] Fill in the UART, timer and exit operations in `pe_{arch}.c`.
- [ ] Publish the board's devices from `{prefix}_hdf_register`.
- [ ] Point the linker script at the real load address.
- [ ] Add a `kern_*.c` entry under `os/loader/` if the board needs its own
      bring-up order.
- [ ] Add a `<board>` target to the `GNUmakefile`.
'''


@dataclass
class ScaffoldResult:
    directory: Path
    files: list[Path]


def scaffold_port(
    arch: str,
    board: str,
    load_address: str = "0x20000000",
    destination: Path | None = None,
) -> ScaffoldResult:
    """Writes a new port skeleton under pexpert/<arch>/.

    ``board`` is the board slug used for the file names; ``arch`` selects the
    board-ops type the skeleton implements (arm32, armv6m, amd64, ...). The
    skeleton compiles to the point of the TODO bodies but is intentionally not
    added to the GNUmakefile, so it cannot break an existing build.
    """
    arch_key = arch.strip().lower().replace("-", "")
    if arch_key in {"armv6m", "armv8m", "cortexm", "cm"}:
        ops_arch = "cm"
        include = "pe_cortexm.h"
        helper = "cortexm"
        port_subdir = "arm"
    elif arch_key == "amd64":
        ops_arch = "amd64"
        include = "pe_amd64.h"
        helper = "amd64"
        port_subdir = "amd64"
    elif arch_key in {"arm32", "arm"}:
        ops_arch = "arm32"
        include = "pe_arm32.h"
        helper = "arm32"
        port_subdir = "arm"
    else:
        raise ValueError(f"unknown architecture '{arch}'")

    slug = re.sub(r"[^a-z0-9_]+", "_", board.strip().lower()).strip("_")
    if not slug:
        raise ValueError("board name must contain at least one letter or digit")

    directory = destination or (REPO_ROOT / "pexpert" / port_subdir)
    directory.mkdir(parents=True, exist_ok=True)

    prefix = slug.upper()
    display = board
    c_path = directory / f"pe_{slug}.c"
    ld_path = directory / f"{slug}.ld"
    readme_path = directory / "README.md"

    c_path.write_text(
        PORT_TEMPLATE_C.format(
            include=include,
            helper=helper,
            ops_arch=ops_arch,
            prefix=prefix,
            display=display,
        ),
        encoding="utf-8",
    )
    ld_path.write_text(
        PORT_TEMPLATE_LD.format(display=display, load_address=load_address),
        encoding="utf-8",
    )
    readme_path.write_text(
        PORT_TEMPLATE_README.format(
            display=display, arch=ops_arch, prefix=prefix
        ),
        encoding="utf-8",
    )

    return ScaffoldResult(directory=directory,
                          files=[c_path, ld_path, readme_path])
