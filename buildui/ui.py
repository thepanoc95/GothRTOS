"""Textual UI screens for the GothRTOS build system builder.

Imported lazily by OSBuild.py so the headless listings (``--list``,
``--list-ports``) work on machines without Textual installed.
"""

from __future__ import annotations

from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, VerticalScroll
from textual.screen import Screen
from textual.widgets import (
    Button,
    DataTable,
    Footer,
    Header,
    Input,
    Label,
    RichLog,
    Select,
    Static,
)

try:  # pyfiglet is optional: the app runs without the ASCII banner.
    import pyfiglet
except Exception:  # pragma: no cover
    pyfiglet = None

from pathlib import Path

from buildui import registry, runner

CSS_PATH = str(Path(__file__).resolve().parent.parent / "OSBuild.tcss")

BANNER = "GothRTOS System Builder"


def banner() -> str:
    if pyfiglet is None:
        return BANNER
    return pyfiglet.figlet_format(BANNER, font="slant")


# --------------------------------------------------------------------------- #
# Welcome screen
# --------------------------------------------------------------------------- #
class WelcomeScreen(Screen):
    BINDINGS = [Binding("q", "quit", "Quit")]

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with VerticalScroll(id="welcome"):
            yield Label(banner(), id="banner")
            yield Label(
                "Pick a board, see its port and devices, build it, or scaffold "
                "a new port.",
                id="subtitle",
            )
            with Horizontal(id="menu"):
                yield Button("Get Started", variant="primary", id="start")
                yield Button("Board list", id="boards")
                yield Button("Port list", id="ports")
                yield Button("New port", id="new-port")
                yield Button.error("Close", id="close")
        yield Footer()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        button_id = event.button.id
        if button_id == "close":
            self.app.exit()
        elif button_id in {"start", "boards"}:
            self.app.push_screen(BoardsScreen())
        elif button_id == "ports":
            self.app.push_screen(PortsScreen())
        elif button_id == "new-port":
            self.app.push_screen(NewPortScreen())


# --------------------------------------------------------------------------- #
# Board list
# --------------------------------------------------------------------------- #
class BoardsScreen(Screen):
    BINDINGS = [
        Binding("escape", "app.pop_screen", "Back"),
        Binding("q", "app.pop_screen", "Back"),
    ]

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield Label("Boards and ports", classes="header")
        yield DataTable(id="boards-table", cursor_type="row", zebra_stripes=True)
        yield Static(
            "Select a row to build, run or test the board. "
            "Ctrl+C stops a running build.",
            id="boards-hint",
        )
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#boards-table", DataTable)
        table.add_columns("Board", "Architecture", "Make target",
                          "Build dir", "Port", "Devices")
        for board in registry.BOARDS:
            table.add_row(
                board.display,
                board.architecture.display,
                board.make_target,
                board.build_dir,
                board.port_dir,
                ", ".join(board.devices) or "-",
                key=board.key,
            )
        table.focus()

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        board = registry.board_by_key(str(event.row_key.value))
        self.app.push_screen(BoardDetailScreen(board))


# --------------------------------------------------------------------------- #
# Board detail / build screen
# --------------------------------------------------------------------------- #
class BoardDetailScreen(Screen):
    BINDINGS = [
        Binding("escape", "app.pop_screen", "Back"),
        Binding("b", "build", "Build"),
        Binding("r", "run", "Run"),
        Binding("t", "test", "Test"),
        Binding("c", "clean", "Clean"),
    ]

    def __init__(self, board: registry.Board) -> None:
        super().__init__()
        self.board = board

    def compose(self) -> ComposeResult:
        board = self.board
        yield Header(show_clock=True)
        yield Label(f"{board.display}", classes="header")
        with VerticalScroll(id="detail"):
            yield Static(self._summary(), id="board-summary")
            yield Label("Support", classes="section")
            yield Static(self._support_text(), id="board-support")
            yield Label("Devices (HDF)", classes="section")
            yield Static(self._device_text(), id="board-devices")
            yield Label("Port sources", classes="section")
            yield Static(self._sources_text(), id="board-sources")
        with Horizontal(id="actions"):
            yield Button("Build", variant="primary", id="build")
            yield Button("Run", id="run", disabled=not board.run_target)
            yield Button("Test", id="test", disabled=not board.test_target)
            yield Button("Clean", id="clean")
        yield Label("Output", classes="section")
        yield RichLog(id="output", highlight=True, markup=False, wrap=True)
        yield Footer()

    # -- text builders ---------------------------------------------------- #
    def _summary(self) -> str:
        board = self.board
        arch = board.architecture
        lines = [
            f"Make target : {board.make_target}",
            f"Architecture: {arch.display} ({arch.toolchain}, cpu {arch.cpu})",
            f"Port        : {board.port_dir}",
            f"Linker      : {board.linker_script or '(host)'}",
            f"Image       : {board.image}",
        ]
        if board.sparse_image:
            lines.append(f"SparseKernel: {board.sparse_image}")
        if board.notes:
            lines.append(f"Notes       : {board.notes}")
        return "\n".join(lines)

    def _support_text(self) -> str:
        rows = registry.board_support(self.board)
        return "\n".join(
            f"[{'x' if ok else ' '}] {check:14} {detail}"
            for check, ok, detail in rows
        )

    def _device_text(self) -> str:
        devices = registry.devices_for(self.board)
        if not devices:
            return "(this port publishes no devices)"
        return "\n".join(
            f"{device.name:16} {device.hdf_class:10} {device.compatible}"
            for device in devices
        )

    def _sources_text(self) -> str:
        sources = registry.port_sources(self.board)
        return "\n".join(sources) if sources else "(no port sources found)"

    # -- build actions ---------------------------------------------------- #
    def on_button_pressed(self, event: Button.Pressed) -> None:
        action = {
            "build": self.action_build,
            "run": self.action_run,
            "test": self.action_test,
            "clean": self.action_clean,
        }.get(event.button.id or "")
        if action is not None:
            action()

    def action_build(self) -> None:
        self._run(runner.build_command(self.board, jobs=4))

    def action_run(self) -> None:
        if not self.board.run_target:
            self._log(f"{self.board.display} has no run target")
            return
        self._run(runner.run_command(self.board))

    def action_test(self) -> None:
        if not self.board.test_target:
            self._log(f"{self.board.display} has no test target")
            return
        self._run(runner.test_command(self.board))

    def action_clean(self) -> None:
        self._run(runner.clean_command())

    def _log(self, line: str) -> None:
        self.query_one("#output", RichLog).write(line)

    def _run(self, command: runner.Command) -> None:
        if not runner.make_available():
            self._log("make is not on PATH; cannot run the build")
            return
        self.query_one("#output", RichLog).write(f"$ {command.display()}")
        self._stream(command)

    @work(thread=True, exclusive=True)
    def _stream(self, command: runner.Command) -> None:
        """Runs the command in a thread and posts each line back to the UI."""

        def emit(line: str) -> None:
            self.app.call_from_thread(self._log, line)

        def done(returncode: int) -> None:
            status = "ok" if returncode == 0 else f"exit {returncode}"
            self.app.call_from_thread(self._log, f"[{status}]")

        runner.stream(command, emit, done)


# --------------------------------------------------------------------------- #
# Port list
# --------------------------------------------------------------------------- #
class PortsScreen(Screen):
    BINDINGS = [
        Binding("escape", "app.pop_screen", "Back"),
        Binding("q", "app.pop_screen", "Back"),
    ]

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield Label("Ports discovered under pexpert/", classes="header")
        yield DataTable(id="ports-table", cursor_type="row", zebra_stripes=True)
        yield Static(
            "Each row is a platform expert. Ports without a board entry in "
            "buildui/registry.py are not yet buildable from the UI.",
            id="ports-hint",
        )
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#ports-table", DataTable)
        table.add_columns("Port", "Architecture", "Sources", "Assembly",
                          "Linker scripts")
        for port in registry.discover_ports():
            table.add_row(
                port.directory,
                port.arch,
                str(len(port.sources)),
                str(len(port.assembly)),
                str(len(port.linker_scripts)),
            )
        table.focus()


# --------------------------------------------------------------------------- #
# New port scaffolder
# --------------------------------------------------------------------------- #
class NewPortScreen(Screen):
    BINDINGS = [Binding("escape", "app.pop_screen", "Back")]

    ARCH_CHOICES = [
        ("ARM32 (A-profile)", "arm32"),
        ("ARMv6-M (Cortex-M0+)", "armv6m"),
        ("ARMv8-M (Cortex-M33)", "armv8m"),
        ("AMD64 (x86-64)", "amd64"),
        ("x86 PC", "i386"),
        ("RISC-V", "rv32"),
    ]

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield Label("Scaffold a new board port", classes="header")
        with VerticalScroll(id="newport"):
            yield Label("Architecture")
            yield Select(self.ARCH_CHOICES, value="armv6m", id="arch")
            yield Label("Board name")
            yield Input(placeholder="e.g. Pico 2 W (RP2350)", id="board-name")
            yield Label("Load address")
            yield Input(value="0x20000000", id="load-address")
            yield Button("Scaffold port", variant="primary", id="scaffold")
            yield Static("", id="scaffold-result")
        yield Footer()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id != "scaffold":
            return
        arch = str(self.query_one("#arch", Select).value)
        name = self.query_one("#board-name", Input).value.strip()
        address = self.query_one("#load-address", Input).value.strip() or "0"
        result = self.query_one("#scaffold-result", Static)

        try:
            scaffold = registry.scaffold_port(arch, name,
                                              load_address=address)
        except ValueError as error:
            result.update(f"error: {error}")
            return

        files = "\n".join(str(path.relative_to(registry.REPO_ROOT))
                          for path in scaffold.files)
        result.update(
            f"Wrote {len(scaffold.files)} file(s):\n{files}\n\n"
            "Next: fill in the TODO bodies, then add the sources to the "
            "GNUmakefile and a board entry to buildui/registry.py."
        )


# --------------------------------------------------------------------------- #
# Application
# --------------------------------------------------------------------------- #
class MainActivity(App):
    CSS_PATH = CSS_PATH
    TITLE = "GothRTOS System Builder"
    SUB_TITLE = "just build it bruh"
    BINDINGS = [
        Binding("ctrl+c", "quit", "Quit", show=False, priority=True),
    ]

    def on_mount(self) -> None:
        self.push_screen(WelcomeScreen())


