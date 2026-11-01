#!/bin/sh
# Boot the bare-metal ARM32 kernel under QEMU and check its serial output.
#
# Usage: tests/run_arm32_qemu.sh [kernel.elf] [logfile]
#
# The kernel writes everything to the PL011 UART, which -nographic connects to
# stdio.  It leaves QEMU through the semihosting SYS_EXIT call, so a successful
# run terminates on its own instead of hitting the timeout.

set -eu

elf=${1:-build/arm32/kern.elf}
log=${2:-build/arm32/qemu.log}
qemu=${QEMU_ARM:-qemu-system-arm}

if [ ! -f "$elf" ]; then
    echo "test-arm32: no kernel image at $elf (run 'make arm32' first)" >&2
    exit 1
fi

if ! command -v "$qemu" >/dev/null 2>&1; then
    echo "test-arm32: SKIP: '$qemu' not found; install qemu-system-arm to run"
    exit 0
fi

mkdir -p "$(dirname "$log")"

echo "test-arm32: booting $elf under $qemu"

set +e
timeout 20 "$qemu" \
    -M virt -cpu cortex-a15 -m 128M -nographic \
    -semihosting-config enable=on,target=native \
    -kernel "$elf" >"$log" 2>&1
status=$?
set -e

if [ "$status" -eq 124 ]; then
    echo "test-arm32: FAIL: QEMU timed out; serial log follows" >&2
    cat "$log" >&2
    exit 1
fi
if [ "$status" -ne 0 ]; then
    echo "test-arm32: FAIL: QEMU exited with status $status; serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

failed=0
for expected in \
    "GothRTOS ARM32 bare-metal boot" \
    "platform: QEMU ARM virt (Cortex-A15)" \
    "architecture: arm32, 32-bit, cpus: 1" \
    "arm32: task A done" \
    "arm32: task B done" \
    "arm32: kernel halted after"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "test-arm32: missing serial output: $expected" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "test-arm32: FAIL: serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

echo "test-arm32: OK (serial output verified)"
