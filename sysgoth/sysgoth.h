#ifndef SYSGOTH_H
#define SYSGOTH_H

#include "gothrtos/sparsekernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SYSGOTH bootloader.
 *
 * SYSGOTH is the GothRTOS first-stage loader for embedded boards. It does not
 * contain the kernel: it waits on the UART for a SparseKernel.img3 image, then
 * loads it into memory and jumps to its entry point. That lets a board be
 * reflashed with a new OS build over the serial link alone, without erasing
 * and reprogramming the whole device.
 *
 * The core in sysgoth.c is portable: it talks to the board through the
 * sg_target_t vtable and shares the SparseKernel parser with the kernel. A
 * board port supplies the UART, a millisecond clock, the writable memory
 * regions, the jump and the reset.
 *
 * Wire protocol (host -> board), all little-endian:
 *
 *   [8]  magic "SPRSK3\0\0"
 *   [56] rest of the 64-byte SparseKernel header
 *   [N]  chunk stream, N = header.total_size - 64
 *
 * The loader scans the UART for the magic so a host that reconnects mid-stream
 * cannot wedge it, validates the header and every DATA chunk CRC, applies the
 * chunks, then acknowledges with "SYSGOTH: OK <entry>\n" before jumping. Any
 * failure is reported as "SYSGOTH: ERR <reason>\n" and the loader returns to
 * waiting. */

#define SYSGOTH_VERSION "0.1.0"
#define SYSGOTH_MAGIC_SIZE GOTH_SPARSE_MAGIC_SIZE

/* Regions the loader is allowed to write. A store outside every region is
 * rejected, so a corrupt image cannot scribble over the loader itself. */
typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
} sg_region_t;

typedef struct {
    const char *name;
    /* Human-readable target name, e.g. "pico2". */
    const char *target;
    /* Brings up the UART at the configured baud rate. */
    void (*uart_init)(uint32_t baud);
    void (*uart_putc)(char character);
    /* Returns the next received byte or -1 when the FIFO is empty. */
    int (*uart_getc)(void);
    uint64_t (*time_ms)(void);
    /* Writable memory regions for the image. */
    const sg_region_t *regions;
    size_t region_count;
    /* Executes the loaded image. Must not return. */
    void (*jump)(uint64_t entry) __attribute__((noreturn));
    /* Reboots the board into the loader again. Must not return. */
    void (*reset)(void) __attribute__((noreturn));
} sg_target_t;

typedef struct {
    uint32_t image_size;
    uint64_t entry;
    uint32_t chunks_applied;
    uint32_t bytes_written;
    uint32_t zero_bytes;
} sg_boot_stats_t;

/* Reads one byte, blocking up to timeout_ms. Returns -1 on timeout. */
int sg_target_getc_timeout(const sg_target_t *target, uint32_t timeout_ms);

/* Runs the loader loop: wait for an image, verify, apply, jump. Returns false
 * only if the target has no usable UART; a rejected image returns to waiting. */
bool sysgoth_run(const sg_target_t *target, uint32_t baud);

/* One receive-and-load cycle. Exposed so a test harness can drive the protocol
 * against a memory-backed target. `timeout_ms` applies to both the initial
 * magic scan and subsequent bytes. */
goth_sparse_status_t sysgoth_receive(const sg_target_t *target,
                                     uint8_t *scratch,
                                     size_t scratch_size,
                                     uint32_t timeout_ms,
                                     sg_boot_stats_t *stats);

/* Like sysgoth_receive, but uses a separate (typically shorter) timeout while
 * scanning for the magic. Once magic is found the body is read with
 * `body_timeout_ms`, so callers can poll for an incoming image without
 * aborting a transfer mid-stream. Returns GOTH_SPARSE_TRUNCATED if no magic
 * arrives within `magic_timeout_ms`. */
goth_sparse_status_t sysgoth_receive_ex(const sg_target_t *target,
                                        uint8_t *scratch,
                                        size_t scratch_size,
                                        uint32_t magic_timeout_ms,
                                        uint32_t body_timeout_ms,
                                        sg_boot_stats_t *stats);

/* Parses and validates an image already held in memory, then applies it. */
goth_sparse_status_t sysgoth_apply_buffer(const sg_target_t *target,
                                          const uint8_t *bytes,
                                          size_t size,
                                          sg_boot_stats_t *stats);

const char *sysgoth_status_name(goth_sparse_status_t status);

#endif
