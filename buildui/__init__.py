"""GothRTOS build system UI support package.

Exposes the board/port registry, the make runner and the port scaffolder that
OSBuild.py drives. Importing this package has no side effects and does not
require Textual, so the registry can be used from scripts and tests.
"""

from . import registry, runner

__all__ = ["registry", "runner"]
