# Test Board port

Scaffolded by `OSBuild`. This directory is not yet wired into the
`GNUmakefile`; add its sources to the matching `*_C_SOURCES` list and its
header dependencies to the matching `*_HEADERS` list.

Checklist:

- [ ] Fill in the UART, timer and exit operations in `pe_arm32.c`.
- [ ] Publish the board's devices from `TEST_BOARD_hdf_register`.
- [ ] Point the linker script at the real load address.
- [ ] Add a `kern_*.c` entry under `os/loader/` if the board needs its own
      bring-up order.
- [ ] Add a `<board>` target to the `GNUmakefile`.
