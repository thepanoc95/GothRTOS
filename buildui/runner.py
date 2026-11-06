"""Build/test runner for the GothRTOS build system UI.

Wraps ``make`` so the UI can stream output without blocking, and knows how to
turn a board selection into the right command. Also builds the portability
environment matrix (architecture x platform) so the UI can show which
combinations are expected to work.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

from . import registry

REPO_ROOT = registry.REPO_ROOT

# Host platforms the user-mode build supports (UM_PLATFORM in the GNUmakefile).
UM_PLATFORMS = ("linux", "windows")


@dataclass
class Command:
    """A prepared shell command plus the directory it runs in."""

    argv: list[str]
    cwd: Path

    def display(self) -> str:
        return " ".join(self.argv)


def make_available() -> bool:
    return shutil.which("make") is not None


def build_command(board: registry.Board, jobs: int = 0,
                  extra_args: str = "") -> Command:
    """Builds one board (``make <target>``)."""
    argv = ["make"]
    if jobs > 0:
        argv.append(f"-j{jobs}")
    argv.append(board.make_target)
    if extra_args.strip():
        argv.extend(extra_args.split())
    return Command(argv=argv, cwd=REPO_ROOT)


def run_command(board: registry.Board, extra_args: str = "") -> Command:
    """Runs a board under QEMU (``make run-<board>``)."""
    if not board.run_target:
        raise ValueError(f"{board.display} has no run target")
    argv = ["make", board.run_target]
    if extra_args.strip():
        argv.extend(extra_args.split())
    return Command(argv=argv, cwd=REPO_ROOT)


def test_command(board: registry.Board, extra_args: str = "") -> Command:
    """Runs a board's test target."""
    argv = ["make", board.test_target]
    if extra_args.strip():
        argv.extend(extra_args.split())
    return Command(argv=argv, cwd=REPO_ROOT)


def clean_command() -> Command:
    return Command(argv=["make", "clean"], cwd=REPO_ROOT)


def stream(command: Command,
           on_line: Callable[[str], None],
           on_done: Callable[[int], None] | None = None) -> subprocess.Popen:
    """Runs ``command`` and calls ``on_line`` for each output line.

    The child is started with ``stdin`` redirected from ``/dev/null`` so the
    interactive kernel smoke tests cannot block on a TTY, matching the
    ``make test </dev/null`` requirement from AGENTS.md.
    """
    env = dict(os.environ)
    env.setdefault("TERM", "dumb")

    process = subprocess.Popen(
        command.argv,
        cwd=str(command.cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL,
        text=True,
        bufsize=1,
        env=env,
    )

    def _reader() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            on_line(line.rstrip("\n"))
        process.wait()
        if on_done is not None:
            on_done(process.returncode)

    import threading

    thread = threading.Thread(target=_reader, daemon=True)
    thread.start()
    return process


# --------------------------------------------------------------------------- #
# Environment / portability matrix
# --------------------------------------------------------------------------- #
@dataclass
class EnvironmentRow:
    arch: str
    display: str
    toolchain: str
    toolchain_ok: bool
    host_ok: bool
    note: str


def environment_matrix(host_platform: str | None = None) -> list[EnvironmentRow]:
    """Describes every architecture and whether it builds on this host.

    The bare-metal targets cross-compile from any host, but the user-mode
    build is tied to the host OS (``UM_PLATFORM``). The matrix makes that
    explicit so a developer can see what is runnable here.
    """
    platform = host_platform or _host_platform()
    rows: list[EnvironmentRow] = []
    for arch in registry.ARCHITECTURES.values():
        if arch.key == "um":
            host_ok = platform in UM_PLATFORMS
            note = f"UM_PLATFORM={platform}"
        else:
            host_ok = True
            note = "cross-compiled"
        rows.append(EnvironmentRow(
            arch=arch.key,
            display=arch.display,
            toolchain=arch.toolchain,
            toolchain_ok=arch.toolchain_installed,
            host_ok=host_ok,
            note=note,
        ))
    return rows


def _host_platform() -> str:
    if os.name == "nt":
        return "windows"
    import sys

    if sys.platform.startswith("linux"):
        return "linux"
    return "unknown"


def boards_for_arch(arch: str) -> Iterable[registry.Board]:
    return (board for board in registry.BOARDS if board.arch == arch)
