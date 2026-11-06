#include "gothrtos/sparsekernel.h"

/* Freestanding: the host generator, the sysgoth bootloader and the kernel all
 * compile this file, so it must not depend on a C library. Local little-endian
 * helpers replace <string.h>. */

const uint8_t goth_sparse_magic[GOTH_SPARSE_MAGIC_SIZE] = {
    'S', 'P', 'R', 'S', 'K', '3', 0, 0
};

static void sparse_memcpy(void *destination,
                          const void *source,
                          size_t size)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    size_t index;

    for (index = 0U; index < size; index++) {
        out[index] = in[index];
    }
}

static void sparse_memset(void *destination, uint8_t value, size_t size)
{
    uint8_t *out = (uint8_t *)destination;
    size_t index;

    for (index = 0U; index < size; index++) {
        out[index] = value;
    }
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) |
           ((uint64_t)read_u32(bytes + 4) << 32);
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u64(uint8_t *bytes, uint64_t value)
{
    write_u32(bytes, (uint32_t)value);
    write_u32(bytes + 4, (uint32_t)(value >> 32));
}

uint32_t goth_sparse_crc32_update(uint32_t crc,
                                  const void *data,
                                  size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    uint32_t value = crc ^ UINT32_C(0xffffffff);

    for (index = 0U; index < size; index++) {
        uint32_t bit;

        value ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(value & 1U));

            value = (value >> 1) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return value ^ UINT32_C(0xffffffff);
}

uint32_t goth_sparse_crc32(const void *data, size_t size)
{
    return goth_sparse_crc32_update(0U, data, size);
}

const char *goth_sparse_arch_name(uint32_t arch)
{
    switch (arch) {
    case GOTH_SPARSE_ARCH_ARM32:
        return "arm32";
    case GOTH_SPARSE_ARCH_AMD64:
        return "amd64";
    case GOTH_SPARSE_ARCH_ARMV6M:
        return "armv6-m";
    case GOTH_SPARSE_ARCH_ARMV8M:
        return "armv8-m";
    case GOTH_SPARSE_ARCH_ANY:
        return "any";
    default:
        return "unknown";
    }
}

static void encode_header(uint8_t *out, const goth_sparse_header_t *header)
{
    sparse_memset(out, 0, GOTH_SPARSE_HEADER_SIZE);
    sparse_memcpy(out, goth_sparse_magic, GOTH_SPARSE_MAGIC_SIZE);
    write_u16(out + 8U, header->version);
    write_u16(out + 10U, header->header_size);
    write_u32(out + 12U, header->flags);
    write_u32(out + 16U, header->arch);
    write_u32(out + 20U, header->chunk_count);
    write_u64(out + 24U, header->entry);
    write_u64(out + 32U, header->total_size);
    write_u64(out + 40U, header->memory_size);
    write_u32(out + 48U, header->payload_crc32);
    /* header_crc32 at offset 52 is written last by the caller. */
}

static void decode_header(const uint8_t *bytes, goth_sparse_header_t *header)
{
    header->version = read_u16(bytes + 8U);
    header->header_size = read_u16(bytes + 10U);
    header->flags = read_u32(bytes + 12U);
    header->arch = read_u32(bytes + 16U);
    header->chunk_count = read_u32(bytes + 20U);
    header->entry = read_u64(bytes + 24U);
    header->total_size = read_u64(bytes + 32U);
    header->memory_size = read_u64(bytes + 40U);
    header->payload_crc32 = read_u32(bytes + 48U);
    header->header_crc32 = read_u32(bytes + 52U);
}

goth_sparse_status_t goth_sparse_open(const void *bytes,
                                      size_t size,
                                      goth_sparse_image_t *image)
{
    const uint8_t *input = (const uint8_t *)bytes;

    if (input == NULL || image == NULL) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }
    if (size < GOTH_SPARSE_HEADER_SIZE) {
        return GOTH_SPARSE_TRUNCATED;
    }
    if (!(input[0] == 'S' && input[1] == 'P' && input[2] == 'R' &&
          input[3] == 'S' && input[4] == 'K' && input[5] == '3')) {
        return GOTH_SPARSE_BAD_MAGIC;
    }

    decode_header(input, &image->header);
    if (image->header.version != GOTH_SPARSE_VERSION) {
        return GOTH_SPARSE_BAD_VERSION;
    }
    if (image->header.header_size != GOTH_SPARSE_HEADER_SIZE) {
        return GOTH_SPARSE_BAD_VERSION;
    }
    if (image->header.total_size > (uint64_t)size) {
        return GOTH_SPARSE_TRUNCATED;
    }

    /* The header CRC covers the first 52 bytes with the CRC field zeroed. */
    {
        uint8_t scratch[GOTH_SPARSE_HEADER_SIZE];
        uint32_t computed;

        sparse_memcpy(scratch, input, GOTH_SPARSE_HEADER_SIZE);
        write_u32(scratch + 52U, 0U);
        computed = goth_sparse_crc32(scratch, 52U);
        if (computed != image->header.header_crc32) {
            return GOTH_SPARSE_BAD_HEADER_CRC;
        }
    }

    image->bytes = input;
    image->size = (size_t)image->header.total_size;
    return GOTH_SPARSE_OK;
}

bool goth_sparse_next_chunk(goth_sparse_iterator_t *iterator,
                            goth_sparse_chunk_t *chunk)
{
    const uint8_t *base;
    size_t offset;
    uint32_t index;
    uint32_t type;
    uint32_t length;
    uint64_t load_address;
    uint32_t crc;

    if (iterator == NULL || iterator->image == NULL || chunk == NULL) {
        return false;
    }
    if (iterator->index >= iterator->image->header.chunk_count) {
        return false;
    }

    base = iterator->image->bytes;
    offset = iterator->offset;
    index = iterator->index;
    if (offset > iterator->image->size ||
        iterator->image->size - offset < GOTH_SPARSE_CHUNK_HEADER_SIZE) {
        return false;
    }

    type = read_u32(base + offset);
    length = read_u32(base + offset + 4U);
    load_address = read_u64(base + offset + 8U);
    crc = read_u32(base + offset + 16U);

    if (type != GOTH_SPARSE_CHUNK_ZERO && type != GOTH_SPARSE_CHUNK_DATA) {
        return false;
    }
    if (type == GOTH_SPARSE_CHUNK_ZERO && length == 0U) {
        return false;
    }
    {
        size_t payload = type == GOTH_SPARSE_CHUNK_DATA ? (size_t)length : 0U;

        if (offset + GOTH_SPARSE_CHUNK_HEADER_SIZE > iterator->image->size ||
            payload > iterator->image->size - offset -
                          GOTH_SPARSE_CHUNK_HEADER_SIZE) {
            return false;
        }
    }

    chunk->type = type;
    chunk->length = length;
    chunk->load_address = load_address;
    chunk->crc32 = crc;
    chunk->data = type == GOTH_SPARSE_CHUNK_DATA
                      ? base + offset + GOTH_SPARSE_CHUNK_HEADER_SIZE
                      : NULL;

    iterator->offset = offset + GOTH_SPARSE_CHUNK_HEADER_SIZE +
                       (type == GOTH_SPARSE_CHUNK_DATA ? (size_t)length : 0U);
    iterator->index = index + 1U;
    return true;
}

void goth_sparse_iterator_begin(const goth_sparse_image_t *image,
                                goth_sparse_iterator_t *iterator)
{
    if (iterator == NULL) {
        return;
    }
    iterator->image = image;
    iterator->offset = GOTH_SPARSE_HEADER_SIZE;
    iterator->index = 0U;
}

goth_sparse_status_t goth_sparse_verify(const goth_sparse_image_t *image)
{
    goth_sparse_iterator_t iterator;
    goth_sparse_chunk_t chunk;
    uint32_t seen = 0U;

    if (image == NULL || image->bytes == NULL) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }

    goth_sparse_iterator_begin(image, &iterator);
    while (goth_sparse_next_chunk(&iterator, &chunk)) {
        if (chunk.type == GOTH_SPARSE_CHUNK_DATA) {
            if (goth_sparse_crc32(chunk.data, chunk.length) != chunk.crc32) {
                return GOTH_SPARSE_BAD_PAYLOAD_CRC;
            }
        }
        seen++;
    }

    if (seen != image->header.chunk_count) {
        return GOTH_SPARSE_BAD_CHUNK;
    }
    return GOTH_SPARSE_OK;
}

goth_sparse_status_t goth_sparse_apply(const goth_sparse_image_t *image,
                                       goth_sparse_store_fn store,
                                       void *context)
{
    goth_sparse_iterator_t iterator;
    goth_sparse_chunk_t chunk;

    if (image == NULL || store == NULL) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }

    goth_sparse_iterator_begin(image, &iterator);
    while (goth_sparse_next_chunk(&iterator, &chunk)) {
        if (chunk.type == GOTH_SPARSE_CHUNK_DATA) {
            if (!store(context, chunk.load_address, chunk.data,
                       chunk.length, false)) {
                return GOTH_SPARSE_NO_MEMORY;
            }
        } else {
            if (!store(context, chunk.load_address, NULL, chunk.length,
                       true)) {
                return GOTH_SPARSE_NO_MEMORY;
            }
        }
    }
    return GOTH_SPARSE_OK;
}

/* Encoder. The header must precede the chunks, but its chunk count, total
 * size and payload CRC are only known once every chunk has been seen, so the
 * segments are walked twice: a counting pass fills the header, then an
 * emitting pass streams the chunks to the sink. Both passes are pure and
 * allocation-free. */
typedef struct {
    goth_sparse_write_fn write;
    void *context;
    uint32_t payload_crc;
    uint32_t chunk_count;
    uint64_t memory_size;
    uint64_t body_size; /* bytes after the header */
    uint32_t chunk_limit;
    bool emit;
    bool failed;
} sparse_encoder_t;

static void encoder_emit(sparse_encoder_t *encoder,
                         const void *data,
                         size_t size)
{
    if (encoder->emit &&
        !encoder->write(encoder->context, data, size)) {
        encoder->failed = true;
    }
}

static void encoder_chunk_header(sparse_encoder_t *encoder,
                                 uint32_t type,
                                 uint64_t address,
                                 uint32_t length,
                                 uint32_t crc)
{
    uint8_t header[GOTH_SPARSE_CHUNK_HEADER_SIZE];

    write_u32(header, type);
    write_u32(header + 4U, length);
    write_u64(header + 8U, address);
    write_u32(header + 16U, crc);
    write_u32(header + 20U, 0U);
    encoder_emit(encoder, header, sizeof(header));
    encoder->chunk_count++;
    encoder->body_size += sizeof(header);
}

static void encoder_zero_run(sparse_encoder_t *encoder,
                             uint64_t address,
                             uint64_t length)
{
    if (length == 0U) {
        return;
    }
    encoder_chunk_header(encoder, GOTH_SPARSE_CHUNK_ZERO, address,
                         (uint32_t)length, 0U);
}

static void encoder_data_run(sparse_encoder_t *encoder,
                             uint64_t address,
                             const uint8_t *data,
                             uint64_t length)
{
    uint64_t offset = 0U;

    while (offset < length) {
        uint64_t remaining = length - offset;
        uint32_t piece = remaining > encoder->chunk_limit
                             ? encoder->chunk_limit
                             : (uint32_t)remaining;
        uint32_t crc = goth_sparse_crc32(data + offset, piece);

        if (encoder->emit) {
            encoder->payload_crc =
                goth_sparse_crc32_update(encoder->payload_crc, data + offset,
                                         piece);
        }
        encoder_chunk_header(encoder, GOTH_SPARSE_CHUNK_DATA,
                             address + offset, piece, crc);
        encoder_emit(encoder, data + offset, piece);
        encoder->body_size += piece;
        offset += piece;
    }
}

/* A zero run at least this long becomes a ZERO chunk; shorter runs stay in a
 * DATA chunk, since a chunk header costs 24 bytes. */
#define SPARSE_ZERO_THRESHOLD 32U

static void encoder_segment(sparse_encoder_t *encoder,
                            const goth_sparse_segment_t *segment)
{
    uint64_t offset = 0U;
    uint64_t file_size = segment->file_size;
    uint64_t memory_size = segment->memory_size;
    const uint8_t *data = segment->data;

    encoder->memory_size += memory_size;
    if (memory_size < file_size) {
        memory_size = file_size;
    }

    while (offset < file_size) {
        if (data[offset] == 0U) {
            uint64_t zero_start = offset;

            while (offset < file_size && data[offset] == 0U) {
                offset++;
            }
            if (offset - zero_start >= SPARSE_ZERO_THRESHOLD) {
                encoder_zero_run(encoder, segment->load_address + zero_start,
                                 offset - zero_start);
            } else {
                encoder_data_run(encoder, segment->load_address + zero_start,
                                 data + zero_start, offset - zero_start);
            }
            continue;
        }

        {
            uint64_t data_start = offset;

            while (offset < file_size && data[offset] != 0U) {
                offset++;
            }
            encoder_data_run(encoder, segment->load_address + data_start,
                             data + data_start, offset - data_start);
        }
    }

    if (file_size < memory_size) {
        encoder_zero_run(encoder, segment->load_address + file_size,
                         memory_size - file_size);
    }
}

goth_sparse_status_t goth_sparse_encode(const goth_sparse_segment_t *segments,
                                        size_t segment_count,
                                        uint64_t entry,
                                        uint32_t arch,
                                        uint32_t flags,
                                        uint32_t chunk_limit,
                                        goth_sparse_write_fn write,
                                        void *context,
                                        uint64_t *output_size)
{
    sparse_encoder_t encoder;
    uint8_t header[GOTH_SPARSE_HEADER_SIZE];
    goth_sparse_header_t out;
    size_t index;
    uint32_t limit;

    if (segments == NULL || write == NULL) {
        return GOTH_SPARSE_INVALID_ARGUMENT;
    }
    limit = chunk_limit != 0U ? chunk_limit : GOTH_SPARSE_DEFAULT_CHUNK_LIMIT;

    /* Pass 1: count chunks, size the body and accumulate the payload CRC. */
    sparse_memset(&encoder, 0, sizeof(encoder));
    encoder.write = write;
    encoder.context = context;
    encoder.chunk_limit = limit;
    encoder.emit = false;
    for (index = 0U; index < segment_count; index++) {
        encoder_segment(&encoder, &segments[index]);
    }
    if (encoder.failed) {
        return GOTH_SPARSE_NO_MEMORY;
    }

    sparse_memset(&out, 0, sizeof(out));
    out.version = GOTH_SPARSE_VERSION;
    out.header_size = GOTH_SPARSE_HEADER_SIZE;
    out.flags = flags;
    out.arch = arch;
    out.chunk_count = encoder.chunk_count;
    out.entry = entry;
    out.total_size = GOTH_SPARSE_HEADER_SIZE + encoder.body_size;
    out.memory_size = encoder.memory_size;
    out.payload_crc32 = encoder.payload_crc;

    encode_header(header, &out);
    write_u32(header + 52U, goth_sparse_crc32(header, 52U));

    if (!write(context, header, sizeof(header))) {
        return GOTH_SPARSE_NO_MEMORY;
    }

    /* Pass 2: emit the chunks in the same order the counting pass saw them. */
    encoder.emit = true;
    for (index = 0U; index < segment_count; index++) {
        encoder_segment(&encoder, &segments[index]);
    }
    if (encoder.failed) {
        return GOTH_SPARSE_NO_MEMORY;
    }

    if (output_size != NULL) {
        *output_size = out.total_size;
    }
    return GOTH_SPARSE_OK;
}
