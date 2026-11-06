#!/bin/sh
# Boot the bare-metal AMD64 kernel under QEMU and check its serial output.
#
# Usage: tests/run_amd64_qemu.sh [kernel.bin] [logfile]
#
# The kernel writes everything to COM1, which -nographic connects to stdio.
# It leaves QEMU through the isa-debug-exit device, so a successful run
# terminates on its own instead of hitting the timeout.

set -eu

image=${1:-build/amd64/kernel.bin}
log=${2:-build/amd64/qemu.log}
qemu=${QEMU_AMD64:-qemu-system-x86_64}

if [ ! -f "$image" ]; then
    echo "test-amd64: no kernel image at $image (run 'make amd64' first)" >&2
    exit 1
fi

if ! command -v "$qemu" >/dev/null 2>&1; then
    echo "test-amd64: SKIP: '$qemu' not found; install qemu-system-x86 to run"
    exit 0
fi

mkdir -p "$(dirname "$log")"

echo "test-amd64: booting $image under $qemu"

set +e
timeout 20 "$qemu" \
    -m 256M -nographic -no-reboot \
    -kernel "$image" >"$log" 2>&1
status=$?
set -e

if [ "$status" -eq 124 ]; then
    echo "test-amd64: FAIL: QEMU timed out; serial log follows" >&2
    cat "$log" >&2
    exit 1
fi
if [ "$status" -ne 0 ]; then
    echo "test-amd64: FAIL: QEMU exited with status $status; serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

failed=0
for expected in \
    "GothRTOS AMD64 bare-metal boot" \
    "platform: QEMU x86_64 (pc)" \
    "architecture: amd64, 64-bit, cpus: 1" \
    "amd64: task A done" \
    "amd64: task B done" \
    "amd64: kernel halted after"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "test-amd64: missing serial output: $expected" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "test-amd64: FAIL: serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

echo "test-amd64: OK (serial output verified)"
