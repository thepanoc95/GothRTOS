#!/bin/sh
# Boot the Raspberry Pi Zero kernel under QEMU's raspi0 machine and check the
# PL011 UART output.
#
# Usage: tests/run_rpi_qemu.sh [kernel.elf] [logfile]
#
# The BCM2835 port has no PSCI or semihosting exit: after its halt report the
# CPU spins forever.  So this script starts QEMU in the background, polls the
# serial log for the halt line, and then stops the machine itself.

set -eu

elf=${1:-build/rpi-zero/kernel.elf}
log=${2:-build/rpi-zero/qemu.log}
qemu=${QEMU_ARM:-qemu-system-arm}

if [ ! -f "$elf" ]; then
    echo "test-rpi-zero: no kernel image at $elf (run 'make rpi-zero' first)" >&2
    exit 1
fi

if ! command -v "$qemu" >/dev/null 2>&1; then
    echo "test-rpi-zero: SKIP: '$qemu' not found; install qemu-system-arm to run"
    exit 0
fi

mkdir -p "$(dirname "$log")"
: >"$log"

echo "test-rpi-zero: booting $elf under $qemu (raspi0)"

"$qemu" \
    -M raspi0 -cpu arm1176 -m 512M -nographic \
    -kernel "$elf" >"$log" 2>&1 </dev/null &
qemu_pid=$!

cleanup()
{
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

elapsed=0
while [ "$elapsed" -lt 200 ]; do
    if grep -Fq "arm32: kernel halted after" "$log"; then
        break
    fi
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        break
    fi
    sleep 0.1
    elapsed=$((elapsed + 1))
done

if ! grep -Fq "arm32: kernel halted after" "$log"; then
    echo "test-rpi-zero: FAIL: no halt report; serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

# The kernel parks the CPU, so stop QEMU now that it has finished booting.
cleanup
trap - EXIT INT TERM

failed=0
for expected in \
    "GothRTOS ARM32 bare-metal boot" \
    "platform: Raspberry Pi Zero (BCM2835)" \
    "architecture: arm32, 32-bit, cpus: 1" \
    "arm32: task A done" \
    "arm32: task B done" \
    "arm32: kernel halted after"
do
    if ! grep -Fq "$expected" "$log"; then
        echo "test-rpi-zero: missing serial output: $expected" >&2
        failed=1
    fi
done

if [ "$failed" -ne 0 ]; then
    echo "test-rpi-zero: FAIL: serial log follows" >&2
    cat "$log" >&2
    exit 1
fi

echo "test-rpi-zero: OK (serial output verified)"
