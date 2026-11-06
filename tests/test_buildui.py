"""Tests for the build system UI core (buildui).

Exercises the board/port registry, the makefile introspection, the port
scaffolder and the make runner without starting the Textual app, so the suite
runs anywhere Python does. When Textual is installed the app is also smoke
tested through its headless test driver.

Run directly (``python3 tests/test_buildui.py``) or via ``make test``.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from buildui import registry, runner  # noqa: E402


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_registry_shape() -> None:
    keys = [board.key for board in registry.BOARDS]
    for expected in ("um", "arm32", "rpi-zero", "pico", "pico2", "amd64"):
        check(expected in keys, f"board {expected} missing from the registry")
    check(len(keys) == len(set(keys)), "board keys are not unique")


def test_board_architecture_resolves() -> None:
    for board in registry.BOARDS:
        arch = board.architecture
        check(arch.key == board.arch,
              f"{board.key}: architecture key mismatch")


def test_every_board_has_a_port() -> None:
    for board in registry.BOARDS:
        port = REPO_ROOT / board.port_dir
        check(port.is_dir(), f"{board.key}: port {board.port_dir} missing")
        if board.linker_script:
            check((REPO_ROOT / board.linker_script).is_file(),
                  f"{board.key}: linker script {board.linker_script} missing")


def test_embedded_boards_emit_sparsekernel() -> None:
    for board in registry.BOARDS:
        if board.key == "um":
            check(not board.sparse_image,
                  "the host build must not produce a SparseKernel image")
        else:
            check(board.sparse_image.endswith("SparseKernel.img3"),
                  f"{board.key}: missing SparseKernel.img3 output")


def test_devices_resolve_by_compatible() -> None:
    for board in registry.BOARDS:
        devices = registry.devices_for(board)
        check(len(devices) == len(board.devices),
              f"{board.key}: some devices did not resolve")
        for device in devices:
            check(device.hdf_class in
                  {"console", "timer", "block", "net", "gpio", "rtc",
                   "framebuffer", "misc"},
                  f"{board.key}: unexpected device class {device.hdf_class}")

    pico2 = registry.devices_for(registry.board_by_key("pico2"))
    compatibles = {device.compatible for device in pico2}
    check("raspberrypi,rp2350-uart" in compatibles,
          "pico2 should publish the RP2350 UART")


def test_makefile_targets_include_boards() -> None:
    targets = registry.makefile_targets()
    for board in registry.BOARDS:
        check(board.make_target in targets,
              f"make target {board.make_target} not found in the GNUmakefile")
    for target in ("pico", "pico2", "amd64", "arm32", "rpi-zero"):
        check(target in targets, f"phony target {target} missing")


def test_makefile_variable_reads_sources() -> None:
    sources = registry.makefile_variable("PICO_C_SOURCES")
    check(sources is not None and "pe_rp2040.c" in sources,
          "PICO_C_SOURCES should list the RP2040 board port")
    nanokernel = registry.makefile_variable("AMD64_C_SOURCES")
    check(nanokernel is not None and "rtos/nanokernel.c" in nanokernel,
          "AMD64_C_SOURCES should include the nanokernel")


def test_port_discovery() -> None:
    ports = {port.name for port in registry.discover_ports()}
    for expected in ("arm", "amd64", "um"):
        check(expected in ports, f"port {expected} not discovered")
    arm = next(p for p in registry.discover_ports() if p.name == "arm")
    check(any(path.endswith("pe_cortexm.c") for path in arm.sources),
          "the arm port should list pe_cortexm.c")


def test_environment_matrix() -> None:
    rows = runner.environment_matrix()
    archs = {row.arch for row in rows}
    check({"um", "arm32", "amd64"} <= archs, "environment matrix incomplete")
    for row in rows:
        check(row.toolchain, f"{row.arch}: empty toolchain")


def test_scaffold_writes_skeleton() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        destination = Path(tmp) / "arm"
        result = registry.scaffold_port("armv6m", "Test Board RP1",
                                        destination=destination)
        names = {path.name for path in result.files}
        check("pe_test_board_rp1.c" in names, "scaffold C file missing")
        check("test_board_rp1.ld" in names, "scaffold linker script missing")
        check("README.md" in names, "scaffold README missing")

        text = (destination / "pe_test_board_rp1.c").read_text()
        check('pe_cortexm.h' in text, "scaffold should include the Cortex-M ops")
        check("goth_cm_board_ops_t" in text,
              "scaffold should define the board ops struct")
        check("goth_cm_board_get_ops" in text,
              "scaffold should export the ops accessor")


def test_scaffold_rejects_empty_name() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        try:
            registry.scaffold_port("arm32", "   ",
                                   destination=Path(tmp) / "arm")
        except ValueError:
            return
    raise AssertionError("an empty board name should be rejected")


def test_scaffold_rejects_unknown_arch() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        try:
            registry.scaffold_port("sparc", "board",
                                   destination=Path(tmp) / "sparc")
        except ValueError:
            return
    raise AssertionError("an unknown architecture should be rejected")


def test_build_commands() -> None:
    pico = registry.board_by_key("pico")
    build = runner.build_command(pico, jobs=4)
    check(build.argv[:2] == ["make", "-j4"], f"unexpected argv {build.argv}")
    check(build.argv[-1] == "pico", "build command should target pico")

    test_cmd = runner.test_command(registry.board_by_key("amd64"))
    check(test_cmd.argv == ["make", "test-amd64"], str(test_cmd.argv))

    # Boards without a run target must refuse rather than guess.
    try:
        runner.run_command(pico)
    except ValueError:
        pass
    else:
        raise AssertionError("pico has no run target and should raise")


def test_ui_smoke_when_textual_present() -> None:
    try:
        import textual  # noqa: F401
    except Exception:
        print("  (textual not installed; skipping the UI smoke test)")
        return

    import asyncio

    from buildui import ui
    from textual.widgets import DataTable

    async def run() -> None:
        app = ui.MainActivity()
        async with app.run_test(size=(100, 40)) as pilot:
            await pilot.pause()
            await pilot.click("#boards")
            await pilot.pause()
            table = app.screen.query_one("#boards-table", DataTable)
            check(table.row_count == len(registry.BOARDS),
                  "board table row count mismatch")
            await pilot.press("enter")
            await pilot.pause()
            check(app.screen.__class__.__name__ == "BoardDetailScreen",
                  "selecting a board should open the detail screen")
            await pilot.press("escape")
            await pilot.pause()
            await pilot.press("escape")
            await pilot.pause()
            await pilot.click("#ports")
            await pilot.pause()
            check(app.screen.query_one("#ports-table", DataTable).row_count > 0,
                  "port table is empty")

    asyncio.run(run())


def main() -> int:
    tests = [
        test_registry_shape,
        test_board_architecture_resolves,
        test_every_board_has_a_port,
        test_embedded_boards_emit_sparsekernel,
        test_devices_resolve_by_compatible,
        test_makefile_targets_include_boards,
        test_makefile_variable_reads_sources,
        test_port_discovery,
        test_environment_matrix,
        test_scaffold_writes_skeleton,
        test_scaffold_rejects_empty_name,
        test_scaffold_rejects_unknown_arch,
        test_build_commands,
        test_ui_smoke_when_textual_present,
    ]
    for test in tests:
        test()
    print("test_buildui: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
