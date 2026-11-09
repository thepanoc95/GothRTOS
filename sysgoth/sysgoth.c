#include "sysgoth.h"

/* Portable SYSGOTH core. This file has no board dependencies: it drives the
 * UART through sg_target_t and the SparseKernel parser through the shared
 * header, so the same logic runs on the Pico, the Pico 2 and the host test
 * harness. */

#define SYSGOTH_CHUNK_BUFFER 4096U

static void sg_puts(const sg_target_t *target, const char *text)
{
    while (*text != '\0') {
        target->uart_putc(*text++);
    }
}

static void sg_put_hex(const sg_target_t *target, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[18];
    int index = (int)sizeof(buffer);
    bool started = false;

    buffer[--index] = '\0';
    do {
        char digit = digits[value & 0xFU];

        buffer[--index] = digit;
        if (digit != '0') {
            started = true;
        }
        value >>= 4;
    } while (value != 0U);

    sg_puts(target, "0x");
    if (!started) {
        target->uart_putc('0');
    }
    sg_puts(target, &buffer[index]);
}

const char *sysgoth_status_name(goth_sparse_status_t status)
{
    switch (status) {
    case GOTH_SPARSE_OK:
        return "ok";
    case GOTH_SPARSE_INVALID_ARGUMENT:
        return "invalid argument";
    case GOTH_SPARSE_BAD_MAGIC:
        return "bad magic";
    case GOTH_SPARSE_BAD_VERSION:
        return "bad version";
    case GOTH_SPARSE_TRUNCATED:
        return "truncated";
    case GOTH_SPARSE_BAD_HEADER_CRC:
        return "bad header crc";
    case GOTH_SPARSE_BAD_PAYLOAD_CRC:
        return "bad payload crc";
    case GOTH_SPARSE_BAD_CHUNK:
        return "bad chunk";
    case GOTH_SPARSE_NO_MEMORY:
        return "write rejected";
    default:
        return "unknown error";
    }
}

int sg_target_getc_timeout(const sg_target_t *target, uint32_t timeout_ms)
{
    uint64_t start;

    if (target == NULL || target->uart_getc == NULL) {
        return -1;
    }
    start = target->time_ms != NULL ? target->time_ms() : 0U;
    for (;;) {
        int character = target->uart_getc();

        if (character >= 0) {
            return character;
        }
        if (target->time_ms == NULL ||
            target->time_ms() - start >= (uint64_t)timeout_ms) {
            return -1;
        }
    }
}

static bool region_contains(const sg_target_t *target, uint64_t address,
                            uint64_t length)
{
    size_t index;

    if (target->regions == NULL || target->region_count == 0U) {
        /* No region list means the target trusts the image. */
        return true;
    }
    for (index = 0U; index < target->region_count; index++) {
        const sg_region_t *region = &target->regions[index];

        if (address >= region->base &&
            length <= region->size &&
            address - region->base <= region->size - length) {
            return true;
        }
    }
    return false;
}

typedef struct {
    const sg_target_t *target;
    uint32_t bytes_written;
    uint32_t zero_bytes;
} sg_store_ctx_t;

static bool sg_store(void *context,
                     uint64_t address,
                     const void *data,
                     size_t length,
                     bool zero)
{
    sg_store_ctx_t *ctx = (sg_store_ctx_t *)context;
    uint8_t *destination = (uint8_t *)(uintptr_t)address;
    size_t index;

    if (!region_contains(ctx->target, address, length)) {
        return false;
    }
    if (zero) {
        for (index = 0U; index < length; index++) {
            destination[index] = 0U;
        }
        ctx->zero_bytes += (uint32_t)length;
    } else {
        const uint8_t *source = (const uint8_t *)data;

        for (index = 0U; index < length; index++) {
            destination[index] = source[index];
        }
        ctx->bytes_written += (uint32_t)length;
    }
    return true;
}

goth_sparse_status_t sysgoth_apply_buffer(const sg_target_t *target,
                                          const uint8_t *bytes,
                                          size_t size,
                                          sg_boot_stats_t *stats)
{
    goth_sparse_image_t image;
    sg_store_ctx_t store;
    goth_sparse_status_t status;

    if (target == NULL || bytes == NULL) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }

    status = goth_sparse_open(bytes, size, &image);
    if (status != GOTH_SPARSE_OK) {
        return status;
    }
    status = goth_sparse_verify(&image);
    if (status != GOTH_SPARSE_OK) {
        return status;
    }

    store.target = target;
    store.bytes_written = 0U;
    store.zero_bytes = 0U;
    status = goth_sparse_apply(&image, sg_store, &store);
    if (status != GOTH_SPARSE_OK) {
        return status;
    }

    if (stats != NULL) {
        stats->image_size = (uint32_t)image.size;
        stats->entry = image.header.entry;
        stats->chunks_applied = image.header.chunk_count;
        stats->bytes_written = store.bytes_written;
        stats->zero_bytes = store.zero_bytes;
    }
    return GOTH_SPARSE_OK;
}

/* Reads exactly `size` bytes, blocking. Returns false on timeout. */
static bool sg_read_exact(const sg_target_t *target,
                          uint8_t *buffer,
                          size_t size,
                          uint32_t timeout_ms)
{
    size_t index;

    for (index = 0U; index < size; index++) {
        int character = sg_target_getc_timeout(target, timeout_ms);

        if (character < 0) {
            return false;
        }
        buffer[index] = (uint8_t)character;
    }
    return true;
}

/* Scans the UART for the SparseKernel magic. Returns false on timeout. */
static bool sg_wait_for_magic(const sg_target_t *target,
                              uint8_t *magic,
                              uint32_t timeout_ms)
{
    size_t matched = 0U;

    for (;;) {
        int character = sg_target_getc_timeout(target, timeout_ms);

        if (character < 0) {
            return false;
        }
        if ((uint8_t)character == goth_sparse_magic[matched]) {
            magic[matched++] = (uint8_t)character;
            if (matched == SYSGOTH_MAGIC_SIZE) {
                return true;
            }
        } else {
            /* Restart, but keep a partial match that this byte continues. */
            matched = ((uint8_t)character == goth_sparse_magic[0]) ? 1U : 0U;
            if (matched == 1U) {
                magic[0] = (uint8_t)character;
            }
        }
    }
}

/* Validates the 64-byte header the loader just received. The chunk stream
 * follows, so the generic goth_sparse_open (which checks total_size against a
 * full buffer) is not usable here; the header fields are checked directly. */
static goth_sparse_status_t sg_validate_header(const uint8_t *header)
{
    uint16_t version;
    uint16_t header_size;
    uint32_t header_crc;
    uint8_t scratch[GOTH_SPARSE_HEADER_SIZE];
    size_t index;

    for (index = 0U; index < SYSGOTH_MAGIC_SIZE; index++) {
        if (header[index] != goth_sparse_magic[index]) {
            return GOTH_SPARSE_BAD_MAGIC;
        }
    }
    version = (uint16_t)(header[8] | (header[9] << 8));
    header_size = (uint16_t)(header[10] | (header[11] << 8));
    if (version != GOTH_SPARSE_VERSION ||
        header_size != GOTH_SPARSE_HEADER_SIZE) {
        return GOTH_SPARSE_BAD_VERSION;
    }

    for (index = 0U; index < GOTH_SPARSE_HEADER_SIZE; index++) {
        scratch[index] = header[index];
    }
    header_crc = (uint32_t)header[52] | ((uint32_t)header[53] << 8) |
                 ((uint32_t)header[54] << 16) | ((uint32_t)header[55] << 24);
    scratch[52] = 0U;
    scratch[53] = 0U;
    scratch[54] = 0U;
    scratch[55] = 0U;
    if (goth_sparse_crc32(scratch, 52U) != header_crc) {
        return GOTH_SPARSE_BAD_HEADER_CRC;
    }
    return GOTH_SPARSE_OK;
}

static uint32_t sg_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t sg_read_u64(const uint8_t *bytes)
{
    return (uint64_t)sg_read_u32(bytes) |
           ((uint64_t)sg_read_u32(bytes + 4) << 32);
}

goth_sparse_status_t sysgoth_receive_ex(const sg_target_t *target,
                                        uint8_t *scratch,
                                        size_t scratch_size,
                                        uint32_t magic_timeout_ms,
                                        uint32_t body_timeout_ms,
                                        sg_boot_stats_t *stats)
{
    uint8_t header[GOTH_SPARSE_HEADER_SIZE];
    uint32_t chunk_count;
    uint32_t chunk_index;
    uint64_t total_size;
    uint64_t entry;
    sg_store_ctx_t store;
    goth_sparse_status_t status;

    if (target == NULL || scratch == NULL ||
        scratch_size < GOTH_SPARSE_CHUNK_HEADER_SIZE) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }
    if (!sg_wait_for_magic(target, header, magic_timeout_ms)) {
        return GOTH_SPARSE_TRUNCATED;
    }
    if (!sg_read_exact(target, header + SYSGOTH_MAGIC_SIZE,
                       GOTH_SPARSE_HEADER_SIZE - SYSGOTH_MAGIC_SIZE,
                       body_timeout_ms)) {
        return GOTH_SPARSE_TRUNCATED;
    }

    status = sg_validate_header(header);
    if (status != GOTH_SPARSE_OK) {
        return status;
    }

    chunk_count = sg_read_u32(header + 20U);
    entry = sg_read_u64(header + 24U);
    total_size = sg_read_u64(header + 32U);

    store.target = target;
    store.bytes_written = 0U;
    store.zero_bytes = 0U;

    for (chunk_index = 0U; chunk_index < chunk_count; chunk_index++) {
        uint8_t chunk_header[GOTH_SPARSE_CHUNK_HEADER_SIZE];
        uint32_t type;
        uint32_t length;
        uint64_t load_address;
        uint32_t crc;

        if (!sg_read_exact(target, chunk_header,
                           GOTH_SPARSE_CHUNK_HEADER_SIZE, body_timeout_ms)) {
            return GOTH_SPARSE_TRUNCATED;
        }
        type = sg_read_u32(chunk_header);
        length = sg_read_u32(chunk_header + 4U);
        load_address = sg_read_u64(chunk_header + 8U);
        crc = sg_read_u32(chunk_header + 16U);

        if (type == GOTH_SPARSE_CHUNK_ZERO) {
            if (length == 0U ||
                !sg_store(&store, load_address, NULL, length, true)) {
                return GOTH_SPARSE_BAD_CHUNK;
            }
            continue;
        }
        if (type != GOTH_SPARSE_CHUNK_DATA || length == 0U) {
            return GOTH_SPARSE_BAD_CHUNK;
        }
        /* A chunk is CRC-checked as a whole, so it must fit the scratch
         * buffer. The encoder's default chunk limit keeps this true. */
        if (length > scratch_size) {
            return GOTH_SPARSE_BAD_CHUNK;
        }
        if (!sg_read_exact(target, scratch, length, body_timeout_ms)) {
            return GOTH_SPARSE_TRUNCATED;
        }
        if (goth_sparse_crc32(scratch, length) != crc) {
            return GOTH_SPARSE_BAD_PAYLOAD_CRC;
        }
        if (!sg_store(&store, load_address, scratch, length, false)) {
            return GOTH_SPARSE_NO_MEMORY;
        }
    }

    if (stats != NULL) {
        stats->image_size = (uint32_t)total_size;
        stats->entry = entry;
        stats->chunks_applied = chunk_count;
        stats->bytes_written = store.bytes_written;
        stats->zero_bytes = store.zero_bytes;
    }
    return GOTH_SPARSE_OK;
}

goth_sparse_status_t sysgoth_receive(const sg_target_t *target,
                                     uint8_t *scratch,
                                     size_t scratch_size,
                                     uint32_t timeout_ms,
                                     sg_boot_stats_t *stats)
{
    return sysgoth_receive_ex(target, scratch, scratch_size, timeout_ms,
                              timeout_ms, stats);
}

bool sysgoth_run(const sg_target_t *target, uint32_t baud)
{
    /* A chunk larger than the scratch buffer cannot be CRC-checked in one
     * piece, so the buffer is a full chunk. 4 KiB matches the encoder's
     * default chunk limit. */
    static uint8_t scratch[SYSGOTH_CHUNK_BUFFER];

    if (target == NULL || target->uart_init == NULL ||
        target->uart_getc == NULL || target->uart_putc == NULL) {
        return false;
    }

    target->uart_init(baud);

    for (;;) {
        sg_boot_stats_t stats;
        goth_sparse_status_t status;

        sg_puts(target, "\r\nSYSGOTH ");
        sg_puts(target, SYSGOTH_VERSION);
        sg_puts(target, " (");
        sg_puts(target, target->name != NULL ? target->name : "unknown");
        sg_puts(target, ")\r\n");
        sg_puts(target, "Waiting for SparseKernel.img3 on UART at ");
        {
            /* Print the baud rate in decimal. */
            char buffer[12];
            int index = (int)sizeof(buffer);
            uint32_t value = baud;

            buffer[--index] = '\0';
            if (value == 0U) {
                buffer[--index] = '0';
            }
            while (value > 0U) {
                buffer[--index] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            sg_puts(target, &buffer[index]);
        }
        sg_puts(target, " baud...\r\n");

        status = sysgoth_receive(target, scratch, sizeof(scratch), 0xffffffffU,
                                 &stats);
        if (status != GOTH_SPARSE_OK) {
            sg_puts(target, "SYSGOTH: ERR ");
            sg_puts(target, sysgoth_status_name(status));
            sg_puts(target, "\r\n");
            continue;
        }

        sg_puts(target, "SYSGOTH: OK entry=");
        sg_put_hex(target, stats.entry);
        sg_puts(target, " size=");
        {
            char buffer[12];
            int index = (int)sizeof(buffer);
            uint32_t value = stats.image_size;

            buffer[--index] = '\0';
            if (value == 0U) {
                buffer[--index] = '0';
            }
            while (value > 0U) {
                buffer[--index] = (char)('0' + (value % 10U));
                value /= 10U;
            }
            sg_puts(target, &buffer[index]);
        }
        sg_puts(target, " bytes\r\n");
        sg_puts(target, "SYSGOTH: booting GothRTOS\r\n");

        if (target->jump != NULL) {
            target->jump(stats.entry);
        }
        /* A target without a jump cannot boot: report and wait again. */
        sg_puts(target, "SYSGOTH: ERR target has no jump\r\n");
    }
}
