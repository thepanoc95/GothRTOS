#ifndef GOTHRTOS_SPARSEKERNEL_H
#define GOTHRTOS_SPARSEKERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SparseKernel image format (.img3).
 *
 * SparseKernel.img3 is the payload that sysgoth loads onto an embedded board
 * over the UART. A full kernel image is mostly zero bytes (padding between
 * sections, .bss, alignment gaps), and pushing those zeros down a 115200 baud
 * link would dominate the transfer. The format therefore stores the image as a
 * list of chunks:
 *
 *   - a DATA chunk carries a run of non-zero bytes for an absolute load
 *     address;
 *   - a ZERO chunk describes a run of zero bytes to be filled at an absolute
 *     load address (a gap between sections, or .bss).
 *
 * The header records the entry point, the architecture and the total memory
 * footprint so the loader can size its destination before it starts writing.
 * Both the header and every DATA chunk carry a CRC32 so a corrupted UART
 * transfer is detected rather than booted.
 *
 * Everything here is freestanding: no libc, no allocation. The host generator
 * (tools/sparsekernel_gen.c), the sysgoth bootloader and the kernel-side
 * loader all share this file, so the format has exactly one implementation. */

#define GOTH_SPARSE_MAGIC_SIZE 8U
#define GOTH_SPARSE_HEADER_SIZE 64U
#define GOTH_SPARSE_CHUNK_HEADER_SIZE 24U
#define GOTH_SPARSE_VERSION 3U
#define GOTH_SPARSE_DEFAULT_CHUNK_LIMIT 4096U

/* "SPRSK3\0\0" */
extern const uint8_t goth_sparse_magic[GOTH_SPARSE_MAGIC_SIZE];

typedef enum {
    GOTH_SPARSE_OK = 0,
    GOTH_SPARSE_INVALID_ARGUMENT,
    GOTH_SPARSE_BAD_MAGIC,
    GOTH_SPARSE_BAD_VERSION,
    GOTH_SPARSE_TRUNCATED,
    GOTH_SPARSE_BAD_HEADER_CRC,
    GOTH_SPARSE_BAD_PAYLOAD_CRC,
    GOTH_SPARSE_BAD_CHUNK,
    GOTH_SPARSE_NO_MEMORY
} goth_sparse_status_t;

typedef enum {
    GOTH_SPARSE_ARCH_ARM32 = 0,
    GOTH_SPARSE_ARCH_AMD64,
    GOTH_SPARSE_ARCH_ARMV6M,   /* RP2040 (Cortex-M0+) */
    GOTH_SPARSE_ARCH_ARMV8M    /* RP2350 (Cortex-M33) */
} goth_sparse_arch_t;

/* Matches any architecture. A plain macro because it does not fit the int
 * range that ISO C requires of enumerators. */
#define GOTH_SPARSE_ARCH_ANY UINT32_C(0xffffffff)

typedef enum {
    GOTH_SPARSE_CHUNK_ZERO = 0,
    GOTH_SPARSE_CHUNK_DATA = 1
} goth_sparse_chunk_type_t;

typedef struct {
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t arch;
    uint32_t chunk_count;
    uint64_t entry;
    uint64_t total_size;   /* whole image, header included */
    uint64_t memory_size;  /* sum of segment memory sizes */
    uint32_t payload_crc32;
    uint32_t header_crc32;
} goth_sparse_header_t;

typedef struct {
    uint32_t type;
    uint32_t length;
    uint64_t load_address;
    uint32_t crc32;
} goth_sparse_chunk_header_t;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    goth_sparse_header_t header;
} goth_sparse_image_t;

typedef struct {
    const goth_sparse_image_t *image;
    size_t offset;         /* next chunk header offset */
    uint32_t index;        /* chunks consumed so far */
} goth_sparse_iterator_t;

typedef struct {
    uint32_t type;
    uint64_t load_address;
    uint32_t length;
    uint32_t crc32;
    /* Points into the image for DATA chunks; NULL for ZERO chunks. */
    const uint8_t *data;
} goth_sparse_chunk_t;

/* Input to the encoder: one loadable region. `memory_size` may exceed
 * `file_size`; the difference is emitted as a ZERO run (bss). */
typedef struct {
    uint64_t load_address;
    const uint8_t *data;
    uint64_t file_size;
    uint64_t memory_size;
    uint32_t flags;
} goth_sparse_segment_t;

/* Sink used by the encoder. Must return false on write failure. */
typedef bool (*goth_sparse_write_fn)(void *context,
                                     const void *data,
                                     size_t size);

/* Store callback used by goth_sparse_apply: writes `length` bytes at
 * `address`. For a ZERO chunk `data` is NULL and the destination must be
 * zero-filled. */
typedef bool (*goth_sparse_store_fn)(void *context,
                                     uint64_t address,
                                     const void *data,
                                     size_t length,
                                     bool zero);

uint32_t goth_sparse_crc32(const void *data, size_t size);
uint32_t goth_sparse_crc32_update(uint32_t crc, const void *data, size_t size);

const char *goth_sparse_arch_name(uint32_t arch);

/* Parses and validates the header of a buffer. Does not verify payload CRCs
 * (call goth_sparse_verify for that). */
goth_sparse_status_t goth_sparse_open(const void *bytes,
                                      size_t size,
                                      goth_sparse_image_t *image);

/* Verifies the header CRC and the CRC of every DATA chunk. */
goth_sparse_status_t goth_sparse_verify(const goth_sparse_image_t *image);

void goth_sparse_iterator_begin(const goth_sparse_image_t *image,
                                goth_sparse_iterator_t *iterator);
/* Fills `chunk` with the next chunk and returns true, or returns false once
 * the chunk list is exhausted. A malformed chunk also ends the iteration;
 * use goth_sparse_verify to tell a clean end from a corrupt one. */
bool goth_sparse_next_chunk(goth_sparse_iterator_t *iterator,
                            goth_sparse_chunk_t *chunk);

/* Writes every chunk through `store`. Stops at the first failure. */
goth_sparse_status_t goth_sparse_apply(const goth_sparse_image_t *image,
                                       goth_sparse_store_fn store,
                                       void *context);

/* Encodes segments into a sparse image through `write`. `chunk_limit` bounds
 * the payload of a single DATA chunk (0 selects the default). The CRC fields
 * are filled in. */
goth_sparse_status_t goth_sparse_encode(const goth_sparse_segment_t *segments,
                                        size_t segment_count,
                                        uint64_t entry,
                                        uint32_t arch,
                                        uint32_t flags,
                                        uint32_t chunk_limit,
                                        goth_sparse_write_fn write,
                                        void *context,
                                        uint64_t *output_size);

#endif
