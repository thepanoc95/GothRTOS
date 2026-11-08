/**
 * Board, port and device registry for the GothRTOS build system.
 *
 * This is the non-interactive core of the terminal UI. It knows which targets the
 * GNUmakefile can build, which pexpert port backs each one, which linker script
 * and architecture it uses, and which device configuration it selects.
 *
 * Nothing here shells out to make: discovery is filesystem based, so the UI
 * works before any build has run.
 */

import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { basename, dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
export const REPO_ROOT = resolve(HERE, "..", "..");

// --------------------------------------------------------------------------- //
// Architecture descriptions
// --------------------------------------------------------------------------- //

export interface Architecture {
  key: string;
  display: string;
  toolchain: string;
  cpu?: string;
  abi?: string;
}

export const ARCHITECTURES: Record<string, Architecture> = {
  um: {
    key: "um",
    display: "User mode (host)",
    toolchain: "cc",
    cpu: "host",
  },
  arm32: {
    key: "arm32",
    display: "ARM32 (A-profile)",
    toolchain: "arm-none-eabi-gcc",
    cpu: "cortex-a15",
    abi: "soft",
  },
};

export function toolchainInstalled(arch: Architecture): boolean {
  return Bun.which(arch.toolchain) !== null;
}

// --------------------------------------------------------------------------- //
// Devices
// --------------------------------------------------------------------------- //

export interface Device {
  name: string;
  compatible: string;
  category: string;
  description: string;
}

/** Hardware visible on board ports supported by this checkout. */
export const DEVICE_CATALOG: Device[] = [
  {
    name: "bcm2835-uart0",
    compatible: "brcm,bcm2835-pl011",
    category: "console",
    description: "PL011 UART on the BCM2835 platform bus",
  },
  {
    name: "bcm2835-systimer",
    compatible: "brcm,bcm2835-system-timer",
    category: "timer",
    description: "1 MHz free-running system timer",
  },
];

// --------------------------------------------------------------------------- //
// Boards / targets
// --------------------------------------------------------------------------- //

export interface Board {
  key: string;
  display: string;
  arch: string;
  makeTarget: string;
  runTarget: string;
  testTarget: string;
  buildDir: string;
  portDir: string;
  linkerScript: string;
  image: string;
  sparseImage?: string;
  devices: string[];
  deviceConfig?: string;
  notes?: string;
}

export const BOARDS: Board[] = [
  {
    key: "um",
    display: "User mode (host)",
    arch: "um",
    makeTarget: "all",
    runTarget: "",
    testTarget: "test",
    buildDir: "build",
    portDir: "pexpert/um",
    linkerScript: "",
    image: "build/kern.ldr",
    devices: [],
    deviceConfig: "os/config/OSConfig.lua",
    notes: "Host build: libgothrtos.a, libkapi.a, libc, lua, kern.ldr.",
  },
  {
    key: "arm32",
    display: "ARM32 QEMU virt",
    arch: "arm32",
    makeTarget: "arm32",
    runTarget: "run-arm32",
    testTarget: "test-arm32",
    buildDir: "build/arm32",
    portDir: "pexpert/arm",
    linkerScript: "pexpert/arm/arm32.ld",
    image: "build/arm32/kern.bin",
    devices: [],
    notes: "Cortex-A15 under QEMU virt, semihosting console.",
  },
  {
    key: "rpi-zero",
    display: "Raspberry Pi Zero (BCM2835)",
    arch: "arm32",
    makeTarget: "rpi-zero",
    runTarget: "run-rpi-zero",
    testTarget: "test-rpi-zero",
    buildDir: "build/rpi-zero",
    portDir: "pexpert/arm",
    linkerScript: "pexpert/arm/bcm2835.ld",
    image: "build/rpi-zero/kernel.img",
    devices: ["brcm,bcm2835-pl011", "brcm,bcm2835-system-timer"],
    deviceConfig: "",
    notes: "ARM1176JZF-S / ARMv6K, PL011 UART, system timer.",
  },
];

export function boardByKey(key: string): Board {
  const board = BOARDS.find((candidate) => candidate.key === key);
  if (!board) {
    throw new Error(`unknown board: ${key}`);
  }
  return board;
}

export function architectureFor(board: Board): Architecture {
  const arch = ARCHITECTURES[board.arch];
  if (!arch) {
    throw new Error(`board ${board.key} references unknown arch ${board.arch}`);
  }
  return arch;
}

/**
 * Returns the Device objects a board publishes, resolved by compatible string
 * (device names are not unique across boards).
 */
export function devicesFor(board: Board): Device[] {
  const resolved: Device[] = [];
  for (const compatible of board.devices) {
    const device = DEVICE_CATALOG.find(
      (candidate) => candidate.compatible === compatible,
    );
    if (device) resolved.push(device);
  }
  return resolved;
}

// --------------------------------------------------------------------------- //
// Port discovery
// --------------------------------------------------------------------------- //

export interface Port {
  directory: string;
  name: string;
  arch: string;
  sources: string[];
  headers: string[];
  linkerScripts: string[];
  assembly: string[];
}

function archForPort(directory: string): string {
  const name = basename(directory);
  if (name === "um") return "um";
  if (name === "arm") return "arm32";
  return "unknown";
}

/** Walks pexpert/ and describes every port directory it finds. */
export function discoverPorts(root: string = REPO_ROOT): Port[] {
  const base = join(root, "pexpert");
  if (!existsSync(base) || !statSync(base).isDirectory()) return [];

  const ports: Port[] = [];
  const directories = readdirSync(base)
    .filter((entry) => !entry.startsWith("."))
    .map((entry) => join(base, entry))
    .filter((entry) => statSync(entry).isDirectory())
    .sort();

  for (const directory of directories) {
    const port: Port = {
      directory: relative(root, directory),
      name: basename(directory),
      arch: archForPort(directory),
      sources: [],
      headers: [],
      linkerScripts: [],
      assembly: [],
    };

    for (const entry of readdirSync(directory).sort()) {
      const path = join(directory, entry);
      if (!statSync(path).isFile()) continue;
      const rel = relative(root, path);
      if (entry.endsWith(".c")) port.sources.push(rel);
      else if (entry.endsWith(".h")) port.headers.push(rel);
      else if (entry.endsWith(".ld")) port.linkerScripts.push(rel);
      else if (entry.endsWith(".S")) port.assembly.push(rel);
    }

    ports.push(port);
  }
  return ports;
}

// --------------------------------------------------------------------------- //
// Board support matrix
// --------------------------------------------------------------------------- //

export interface SupportRow {
  check: string;
  ok: boolean;
  detail: string;
}

/** Returns rows describing whether a board can build, without invoking make. */
export function boardSupport(board: Board): SupportRow[] {
  const rows: SupportRow[] = [];
  const arch = architectureFor(board);

  const installed = toolchainInstalled(arch);
  rows.push({
    check: "Toolchain",
    ok: installed,
    detail: arch.toolchain + (installed ? "" : " (missing)"),
  });

  rows.push({
    check: "Port",
    ok: isDirectory(join(REPO_ROOT, board.portDir)),
    detail: board.portDir,
  });

  if (board.linkerScript) {
    rows.push({
      check: "Linker script",
      ok: isFile(join(REPO_ROOT, board.linkerScript)),
      detail: board.linkerScript,
    });
  }

  const sources = portSources(board);
  rows.push({
    check: "Port sources",
    ok: sources.length > 0,
    detail: `${sources.length} file(s)`,
  });

  return rows;
}

/** Returns the pexpert sources the board's port contributes. */
export function portSources(board: Board): string[] {
  const portPath = join(REPO_ROOT, board.portDir);
  if (!isDirectory(portPath)) return [];
  return readdirSync(portPath)
    .filter(
      (entry) =>
        (entry.endsWith(".c") || entry.endsWith(".S")) &&
        isFile(join(portPath, entry)),
    )
    .sort()
    .map((entry) => relative(REPO_ROOT, join(portPath, entry)));
}

export function boardReady(board: Board): boolean {
  return boardSupport(board).every((row) => row.ok);
}

function isDirectory(path: string): boolean {
  try {
    return statSync(path).isDirectory();
  } catch {
    return false;
  }
}

function isFile(path: string): boolean {
  try {
    return statSync(path).isFile();
  } catch {
    return false;
  }
}

// --------------------------------------------------------------------------- //
// GNUmakefile introspection
// --------------------------------------------------------------------------- //

const MAKE_TARGET_RE = /^([A-Za-z0-9][A-Za-z0-9_.-]*)\s*:(?!=)/;

/**
 * Returns the phony target names declared in the GNUmakefile.
 *
 * This reads the .PHONY line rather than every rule, so it lists the
 * user-facing entry points (arm32, test, ...) and not object rules.
 */
export function makefileTargets(path: string = join(REPO_ROOT, "GNUmakefile")): string[] {
  if (!isFile(path)) return [];

  const text = readFileSync(path, "utf8");
  const targets: string[] = [];
  let inPhony = false;

  for (const line of text.split(/\r?\n/)) {
    if (!inPhony) {
      if (!line.startsWith(".PHONY:")) continue;
      inPhony = true;
    }
    const content = line
      .replace(/^\.PHONY:/, "")
      .replace(/\\\s*$/, "");
    for (const token of content.split(/\s+/)) {
      if (token && !targets.includes(token)) targets.push(token);
    }
    if (!line.trimEnd().endsWith("\\")) break;
  }

  if (targets.length === 0) {
    for (const line of text.split(/\r?\n/)) {
      const found = line.match(MAKE_TARGET_RE);
      if (found && !found[1].startsWith(".")) targets.push(found[1]);
    }
  }
  return targets;
}

/**
 * Returns the first assignment of a make variable, with continuations joined
 * and whitespace collapsed. Returns null when the variable is absent.
 */
export function makefileVariable(
  name: string,
  path: string = join(REPO_ROOT, "GNUmakefile"),
): string | null {
  if (!isFile(path)) return null;

  const lines = readFileSync(path, "utf8").split(/\r?\n/);
  const assignment = new RegExp(
    `^${escapeRegExp(name)}\\s*[?:]?=\\s*(.*)$`,
  );
  const parts: string[] = [];
  let collecting = false;

  for (const line of lines) {
    if (!collecting) {
      const match = line.match(assignment);
      if (!match) continue;
      collecting = true;
      parts.push(match[1]);
    } else {
      parts.push(line);
    }
    if (!line.trimEnd().endsWith("\\")) break;
  }

  if (!collecting) return null;
  const joined = parts
    .map((part) => part.replace(/\\+$/, "").trim())
    .join(" ");
  return joined.split(/\s+/).filter(Boolean).join(" ");
}

function escapeRegExp(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

// --------------------------------------------------------------------------- //
// Device configuration discovery
// --------------------------------------------------------------------------- //

/** Lists the Lua device configuration files under os/config. */
export function discoverDeviceConfigs(root: string = REPO_ROOT): string[] {
  const base = join(root, "os", "config");
  if (!isDirectory(base)) return [];

  const found: string[] = [];
  const walk = (directory: string): void => {
    for (const entry of readdirSync(directory).sort()) {
      const path = join(directory, entry);
      if (isDirectory(path)) walk(path);
      else if (entry.endsWith(".lua")) found.push(relative(root, path));
    }
  };
  walk(base);
  return found;
}

// --------------------------------------------------------------------------- //
// Script entry points
// --------------------------------------------------------------------------- //

export function formatBoard(board: Board): string {
  const marker = boardReady(board) ? "ready" : "needs setup";
  return (
    `${board.key.padEnd(10)} ${board.display.padEnd(35)} ` +
    `arch=${board.arch.padEnd(8)} make=${board.makeTarget.padEnd(12)} [${marker}]`
  );
}

export function formatDevice(device: Device): string {
  return `           device: ${device.name} (${device.category}, ${device.compatible})`;
}

export function formatPort(port: Port): string {
  return (
    `${port.directory.padEnd(16)} arch=${port.arch.padEnd(8)} ` +
    `sources=${port.sources.length} asm=${port.assembly.length} ` +
    `ld=${port.linkerScripts.length}`
  );
}

export type Emit = (line: string) => void;

/** Prints every board and its devices, one board per block. */
export function listBoards(emit: Emit = console.log): void {
  for (const board of BOARDS) {
    emit(formatBoard(board));
    for (const device of devicesFor(board)) emit(formatDevice(device));
  }
}

/** Prints every port discovered under pexpert/. */
export function listPorts(emit: Emit = console.log): void {
  for (const port of discoverPorts()) emit(formatPort(port));
}

// --------------------------------------------------------------------------- //
// Port scaffolding
// --------------------------------------------------------------------------- //

const PORT_TEMPLATE_C = `#include "pe_arm32.h"

#include "gothrtos/arm32.h"

#include <stdint.h>

/* {display} board port.
 *
 * Scaffolded by OSBuild. Fill in the hardware operations below; the
 * scheduler, pexpert glue and console formatting are shared in pe_arm32.c. */

#define {prefix}_UART_BASE UINT32_C(0x00000000)
#define {prefix}_UART_BAUD UINT32_C(115200)

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t){prefix}_UART_BASE;

static void {prefix}_uart_init(void)
{
    (void)uart;
    /* TODO: configure GPIO/pin muxing and the UART divisor for the clock. */
}

static void {prefix}_uart_putc(char character)
{
    (void)character;
    (void)uart;
    /* TODO: wait for space, then write the character to the data register. */
}

static int {prefix}_uart_getc(void)
{
    (void)uart;
    /* TODO: return the next received byte, or -1 when the FIFO is empty. */
    return -1;
}

static uint64_t {prefix}_time_ns(void)
{
    /* TODO: return the board's monotonic clock in nanoseconds. */
    return 0U;
}

static void {prefix}_exit(int status)
{
    (void)status;
    for (;;) {
        goth_arm32_cpu_relax();
    }
}

static const goth_arm_board_ops_t {prefix}_board_ops = {
    "{display}",
    {prefix}_uart_init,
    {prefix}_uart_putc,
    {prefix}_uart_getc,
    {prefix}_time_ns,
    {prefix}_exit
};

const goth_arm_board_ops_t *goth_arm_board_get_ops(void)
{
    return &{prefix}_board_ops;
}
`;

const PORT_TEMPLATE_LD = `ENTRY(_start)

/* {display} link script (scaffolded by OSBuild). */
SECTIONS
{
    . = {load_address};
    __image_start = .;

    .text : ALIGN(4)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }

    .data : ALIGN(4)
    {
        *(.data .data.*)
        . = ALIGN(4);
    }

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(8)
    {
        __stack_bottom = .;
        . = . + 0x4000;
        __stack_top = .;
    }

    . = ALIGN(8);
    __image_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}
`;

const PORT_TEMPLATE_README = `# {display} port

Scaffolded by \`OSBuild\`. This directory is not yet wired into the
\`GNUmakefile\`; add its sources to the matching \`*_C_SOURCES\` list and its
header dependencies to the matching \`*_HEADERS\` list.

Checklist:

- [ ] Fill in the UART, timer and exit operations in \`pe_{arch}.c\`.
- [ ] Add board-specific drivers using \`gothrtos/driver.h\` where needed.
- [ ] Point the linker script at the real load address.
- [ ] Add a \`kern_*.c\` entry under \`os/loader/\` if the board needs its own
      bring-up order.
- [ ] Add a \`<board>\` target to the \`GNUmakefile\`.
`;

export interface ScaffoldResult {
  directory: string;
  files: string[];
}

const DRIVER_TEMPLATE = `#include "gothrtos/driver.h"

static const char *const {symbol}_compatible[] = {
{compatible_entries}
};

static goth_status_t {symbol}_probe(goth_device_t *device)
{
    (void)device;
    /* TODO: initialize hardware and attach driver state. */
    return GOTH_STATUS_PLATFORM_ERROR;
}

goth_status_t goth_{symbol}_driver_init(goth_driver_t *driver)
{
    return goth_driver_init(driver,
                            "{name}",
                            {symbol}_compatible,
                            sizeof({symbol}_compatible) /
                                sizeof({symbol}_compatible[0]),
                            {symbol}_probe,
                            NULL);
}
`;

export interface DriverScaffoldResult {
  file: string;
}

/** Writes a driver skeleton using the kernel's public static driver model. */
export function scaffoldDriver(
  name: string,
  compatibles: string[],
  destination = join(REPO_ROOT, "drivers"),
): DriverScaffoldResult {
  const trimmedName = name.trim();
  if (!/^[A-Za-z0-9][A-Za-z0-9_.-]*$/.test(trimmedName)) {
    throw new Error("driver name must use letters, digits, dot, underscore, or hyphen");
  }
  if (compatibles.length === 0) {
    throw new Error("at least one compatible string is required");
  }
  for (const compatible of compatibles) {
    if (
      compatible.length === 0 ||
      compatible.trim() !== compatible ||
      /[^\x20-\x7e]/.test(compatible) ||
      compatible.includes('"') ||
      compatible.includes("\\")
    ) {
      throw new Error(`invalid compatible string: ${compatible}`);
    }
  }

  const slug = trimmedName.toLowerCase().replace(/[^a-z0-9_]+/g, "_");
  const directory = resolve(destination);
  mkdirSync(directory, { recursive: true });
  const file = join(directory, `goth_${slug}.c`);
  const source = fill(DRIVER_TEMPLATE, {
    symbol: slug,
    name: trimmedName,
    compatible_entries: compatibles
      .map((compatible) => `    "${compatible}",`)
      .join("\n"),
  });
  try {
    writeFileSync(file, source, { encoding: "utf8", flag: "wx" });
  } catch (error) {
    if (error instanceof Error && "code" in error && error.code === "EEXIST") {
      throw new Error(`driver source already exists: ${file}`);
    }
    throw error;
  }
  return { file };
}

/**
 * Writes a new port skeleton under pexpert/<arch>/.
 *
 * The current kernel only exposes the ARM32 board-ops contract. The skeleton
 * is intentionally not added to the GNUmakefile, so it cannot break an
 * existing build.
 */
export function scaffoldPort(
  arch: string,
  board: string,
  loadAddress = "0x20000000",
  destination?: string,
): ScaffoldResult {
  const archKey = arch.trim().toLowerCase().replace(/-/g, "");
  if (!["arm32", "arm"].includes(archKey)) {
    throw new Error(`unknown architecture '${arch}'`);
  }

  const slug = board
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9_]+/g, "_")
    .replace(/^_+|_+$/g, "");
  if (!slug) {
    throw new Error("board name must contain at least one letter or digit");
  }

  const directory = destination ?? join(REPO_ROOT, "pexpert", "arm");
  mkdirSync(directory, { recursive: true });

  const prefix = slug.toUpperCase();
  const display = board;
  const cPath = join(directory, `pe_${slug}.c`);
  const ldPath = join(directory, `${slug}.ld`);
  const readmePath = join(directory, "README.md");

  writeFileSync(
    cPath,
    fill(PORT_TEMPLATE_C, { prefix, display }),
    "utf8",
  );
  writeFileSync(
    ldPath,
    fill(PORT_TEMPLATE_LD, { display, load_address: loadAddress }),
    "utf8",
  );
  writeFileSync(
    readmePath,
    fill(PORT_TEMPLATE_README, {
      display,
      arch: "arm32",
      prefix,
    }),
    "utf8",
  );

  return { directory, files: [cPath, ldPath, readmePath] };
}

/** Tiny {name} template filler that does not treat ${...} as JS syntax. */
function fill(template: string, values: Record<string, string>): string {
  return template.replace(/\{(\w+)\}/g, (whole, key: string) =>
    key in values ? values[key] : whole,
  );
}
