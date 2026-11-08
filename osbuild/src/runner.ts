/**
 * Build/test runner for the GothRTOS build system.
 *
 * Wraps make so the terminal UI can stream output without blocking, and knows how to
 * turn a board selection into the right command. Also builds the portability
 * environment matrix (architecture x platform) so the UI can show which
 * combinations are expected to work.
 */

import {
  REPO_ROOT,
  ARCHITECTURES,
  BOARDS,
  type Board,
  toolchainInstalled,
} from "./registry";

/** Host platforms the user-mode build supports (UM_PLATFORM in the GNUmakefile). */
export const UM_PLATFORMS = ["linux", "windows"] as const;

export interface Command {
  argv: string[];
  cwd: string;
}

export function commandDisplay(command: Command): string {
  return command.argv.join(" ");
}

export function makeAvailable(): boolean {
  return Bun.which("make") !== null;
}

function extraArgv(extraArgs: string): string[] {
  const trimmed = extraArgs.trim();
  return trimmed ? trimmed.split(/\s+/) : [];
}

/** Builds one board (`make <target>`). */
export function buildCommand(
  board: Board,
  jobs = 0,
  extraArgs = "",
): Command {
  const argv = ["make"];
  if (jobs > 0) argv.push(`-j${jobs}`);
  argv.push(board.makeTarget, ...extraArgv(extraArgs));
  return { argv, cwd: REPO_ROOT };
}

/** Runs a board under QEMU (`make run-<board>`). */
export function runCommand(board: Board, extraArgs = ""): Command {
  if (!board.runTarget) {
    throw new Error(`${board.display} has no run target`);
  }
  return {
    argv: ["make", board.runTarget, ...extraArgv(extraArgs)],
    cwd: REPO_ROOT,
  };
}

/** Runs a board's test target. */
export function testCommand(board: Board, extraArgs = ""): Command {
  if (!board.testTarget) {
    throw new Error(`${board.display} has no test target`);
  }
  return {
    argv: ["make", board.testTarget, ...extraArgv(extraArgs)],
    cwd: REPO_ROOT,
  };
}

export function cleanCommand(): Command {
  return { argv: ["make", "clean"], cwd: REPO_ROOT };
}

// --------------------------------------------------------------------------- //
// Environment / portability matrix
// --------------------------------------------------------------------------- //

export interface EnvironmentRow {
  arch: string;
  display: string;
  toolchain: string;
  toolchainOk: boolean;
  hostOk: boolean;
  note: string;
}

/**
 * Describes every architecture and whether it builds on this host.
 *
 * The bare-metal targets cross-compile from any host, but the user-mode build
 * is tied to the host OS (UM_PLATFORM). The matrix makes that explicit so a
 * developer can see what is runnable here.
 */
export function environmentMatrix(hostPlatform?: string): EnvironmentRow[] {
  const platform = hostPlatform ?? currentHostPlatform();
  const rows: EnvironmentRow[] = [];

  for (const arch of Object.values(ARCHITECTURES)) {
    let hostOk = true;
    let note = "cross-compiled";
    if (arch.key === "um") {
      hostOk = (UM_PLATFORMS as readonly string[]).includes(platform);
      note = `UM_PLATFORM=${platform}`;
    }
    rows.push({
      arch: arch.key,
      display: arch.display,
      toolchain: arch.toolchain,
      toolchainOk: toolchainInstalled(arch),
      hostOk,
      note,
    });
  }
  return rows;
}

export function currentHostPlatform(): string {
  if (process.platform === "win32") return "windows";
  if (process.platform === "linux") return "linux";
  return "unknown";
}

export function boardsForArch(arch: string): Board[] {
  return BOARDS.filter((board) => board.arch === arch);
}
