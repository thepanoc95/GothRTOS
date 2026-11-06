"""GothRTOS System Builder — the build system UI.

Command line front end. The board/device data and the make runner live in the
``buildui`` package, and the Textual screens live in ``buildui.ui`` (imported
lazily, so ``--list`` and ``--list-ports`` work without Textual installed).

    python OSBuild.py              # start the interactive UI
    python OSBuild.py --list       # list boards, ports and devices
    python OSBuild.py --list-ports # list the ports found under pexpert/

Or use ``make build-ui``.
"""

from __future__ import annotations

import argparse
import sys

from buildui import registry


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="GothRTOS build system UI")
    parser.add_argument("--list", action="store_true",
                        help="list boards and exit")
    parser.add_argument("--list-ports", action="store_true",
                        help="list discovered ports and exit")
    args = parser.parse_args(argv)

    if args.list:
        registry.list_boards()
        return 0
    if args.list_ports:
        registry.list_ports()
        return 0

    try:
        from buildui.ui import MainActivity
    except ModuleNotFoundError as error:
        print(f"the interactive UI needs Textual: {error}\n"
              "install it with: pip install textual pyfiglet",
              file=sys.stderr)
        return 2

    MainActivity().run()
    return 0


def __getattr__(name: str):
    """Lazily expose the UI classes (``OSBuild.MainActivity`` and friends)."""
    if name in {"MainActivity", "WelcomeScreen", "BoardsScreen",
                "BoardDetailScreen", "PortsScreen", "NewPortScreen",
                "banner"}:
        from buildui import ui

        return getattr(ui, name)
    raise AttributeError(name)


if __name__ == "__main__":
    sys.exit(main())
