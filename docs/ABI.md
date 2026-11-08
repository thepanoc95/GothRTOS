# GothRTOS ABI

This document records the first supported GothRTOS ABI contracts. ABI v1 is
additive to the existing C interfaces: syscall numbers 1-6 and the GOTH95
version-1 file format retain their existing meanings.

## System-call ABI v1

`goth_syscall_dispatch()` is the current C-level dispatch boundary. It is
called directly by the user-mode implementation; it is not yet a hardware
trap, privilege transition, or serialized wire protocol. Request arguments
have the target's native `uintptr_t` width so they can carry pointers in this
in-process implementation.

The request is `goth_syscall_request_t`: `number` is a `uint32_t`, followed by
four `uintptr_t` arguments. The result is `goth_syscall_result_t`: `status` is
a `goth_status_t`, and `value` is a `uint64_t`. `GOTH_SYSCALL_GET_ABI_VERSION`
returns `GOTH_SYSCALL_ABI_VERSION` (currently `1`) in `value`; as with other
calls, dispatch requires an initialized kernel.

| Number | Call | Arguments | Result value |
|---:|---|---|---|
| 1 | `GOTH_SYSCALL_TASK_YIELD` | - | ID of the newly scheduled task, or 0 |
| 2 | `GOTH_SYSCALL_TASK_SLEEP` | `[0]` positive tick count | ID of the newly scheduled task, or 0 |
| 3 | `GOTH_SYSCALL_TASK_EXIT` | - | ID of the newly scheduled task, or 0 |
| 4 | `GOTH_SYSCALL_KERNEL_TICKS` | - | Current 64-bit tick count |
| 5 | `GOTH_SYSCALL_ENDPOINT_SEND` | `[0]` endpoint pointer; `[1]` message type; `[2..3]` values | 0 |
| 6 | `GOTH_SYSCALL_ENDPOINT_RECEIVE` | `[0]` endpoint pointer; `[1]` message output pointer | 0 |
| 7 | `GOTH_SYSCALL_GET_ABI_VERSION` | - | System-call ABI version |

The status field carries success or the applicable `goth_status_t` error.
Task calls that fail validation return an error status and a zero value.
Endpoint calls report their outcome in `status`.

Existing syscall numbers and argument meanings are reserved. Add new calls at
new numbers; do not repurpose a number. The ABI version changes only when a
future change cannot preserve this contract. The C structs are native-layout
types, not structures to persist or send across machines.

## GOTH95 program-image ABI v1

GOTH95 is a little-endian image container. Version 1 accepts only
`GOTH95_MACHINE_I386` (machine value 3) and uses 32-bit virtual addresses.
The eight-byte magic is `95EXC\0\r\n`. The 64-byte header stores the version
and header size at offsets 8 and 10, type and machine at 12 and 14, flags and
entry at 16 and 20, segment-table offset at 24, segment-entry size/count at
32/34, dynamic-table offset/size at 36/44, and metadata offset/size at 52/60.
All multibyte values are little-endian.

The v1 fixed record sizes are exposed by `gothrtos/95exc.h`: 40-byte segment
records, 16-byte dynamic records, 20-byte symbol records, and 12-byte
relocation records. Supported image types are executable (2), shared (3), and
module (4); segment type 1 is loadable. The loader accepts the i386 relocation
types `NONE`, `R_32`, `R_PC32`, `R_GLOB_DAT`, `R_JMP_SLOT`, and `R_RELATIVE`,
as listed in the public header. Any nonzero entry must refer to an executable
load segment. Shared objects and modules may have a zero entry.

`goth95_load()` validates, loads, and relocates an image through supplied
loader operations; it deliberately does not transfer control to the entry
point. Consequently, v1 does not establish a function calling convention,
startup stack, register contract, or process-entry routine. Those must be
specified before GothRTOS adds execution of GOTH95 entries. The ELF32 exporter
converts supported loaded-image metadata to i386 ELF32; it does not add another
GOTH95 machine type.

## Compatibility stages

1. **Current v1:** expose syscall ABI version discovery and freeze the existing
   syscall numbering/semantics; name the existing GOTH95 file-format version
   and document its supported i386 image contract.
2. **Before user execution:** specify an entry/calling convention and connect
   it to a process/task execution model. This is intentionally deferred because
   the loader currently does not execute entry points.
3. **Additional architecture or privilege boundary:** define per-target
   transition and data-layout rules without changing v1 syscall numbers or
   silently treating an i386 GOTH95 image as a different machine format.
