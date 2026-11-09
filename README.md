# GothRTOS PureNext
Well, I got tired of it, like, I was literally vibe coding the **whole system**,
and I don't wanna make this OS' be known as vibe coded, so, this branch will be made only and just by myself, the whole system nanokernel core, made by me.

## Current TODO
- [x] initial cooperative nanokernel base (fixed task table; no preemption)
- [x] initial Hardware Driver Foundation (fixed device/driver registry)
- [ ] the HAL
- [ ] the KAL for usermode execution (like a parasite OS lol)
- [ ] desktop support
- [ ] mobile support (parasited on Android 7.1.1 and higher.)

## Current Kernel Foundation
- The nanokernel dispatches up to eight cooperative task-step callbacks round-robin. A step must return promptly and report ready, blocked, or exited.
- HDF matches up to eight registered devices with drivers by compatible string, then calls probe, start, and stop hooks.
- The RP2350 UART is the first HDF-managed device. Task switching is cooperative and stackless; timer preemption, interrupt-safe APIs, and device discovery are not implemented yet.

Build the Pico 2 firmware with `cmake -S . -B build && cmake --build build`.
Run the host-side foundation tests with `cmake -S tests -B build/host-tests && cmake --build build/host-tests && ctest --test-dir build/host-tests --output-on-failure`.
