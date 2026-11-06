#include "gothrtos/sparsekernel.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Round-trips the SparseKernel image format: encode a set of segments,
 * parse the result back, verify the CRCs and apply the chunks into a memory
 * buffer that must match the original. Also checks the sparse encoding really
 * shrinks an image dominated by zeros. */

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t capacity;
} buffer_t;

static bool buffer_write(void *context, const void *data, size_t size)
{
    buffer_t *buffer = (buffer_t *)context;

    if (buffer->size + size > buffer->capacity) {
        return false;
    }
    memcpy(buffer->bytes + buffer->size, data, size);
    buffer->size += size;
    return true;
}

typedef struct {
    uint8_t memory[64 * 1024];
    size_t writes;
} destination_t;

static bool memory_store(void *context,
                         uint64_t address,
                         const void *data,
                         size_t length,
                         bool zero)
{
    destination_t *destination = (destination_t *)context;

    if (address + length > sizeof(destination->memory)) {
        return false;
    }
    if (zero) {
        memset(destination->memory + address, 0, length);
    } else {
        memcpy(destination->memory + address, data, length);
    }
    destination->writes++;
    return true;
}

static void test_crc32(void)
{
    /* CRC32 of "123456789" is the standard check value 0xcbf43926. */
    assert(goth_sparse_crc32("123456789", 9) == UINT32_C(0xcbf43926));
    assert(goth_sparse_crc32("", 0) == 0U);
    assert(goth_sparse_crc32_update(goth_sparse_crc32("1234", 4), "56789", 5) ==
           UINT32_C(0xcbf43926));
}

static void test_round_trip(void)
{
    /* Two segments: a code segment with an embedded zero gap and a data
     * segment with a .bss tail. */
    uint8_t code[64];
    uint8_t data[32];
    goth_sparse_segment_t segments[2];
    buffer_t image = {NULL, 0U, 0U};
    goth_sparse_image_t parsed;
    goth_sparse_iterator_t iterator;
    goth_sparse_chunk_t chunk;
    destination_t destination;
    uint64_t output_size = 0U;
    uint32_t data_chunks = 0U;
    uint32_t zero_chunks = 0U;

    memset(code, 0xAA, sizeof(code));
    memset(code + 16, 0x00, 32); /* a 32-byte zero gap */
    memset(data, 0x55, sizeof(data));

    segments[0].load_address = 0x1000U;
    segments[0].data = code;
    segments[0].file_size = sizeof(code);
    segments[0].memory_size = sizeof(code);
    segments[0].flags = GOTH_SPARSE_CHUNK_DATA;

    segments[1].load_address = 0x8000U;
    segments[1].data = data;
    segments[1].file_size = sizeof(data);
    segments[1].memory_size = sizeof(data) + 1024U; /* 1 KiB of bss */
    segments[1].flags = 0U;

    image.capacity = 8192U;
    image.bytes = malloc(image.capacity);
    assert(image.bytes != NULL);

    assert(goth_sparse_encode(segments, 2U, 0x1000U, GOTH_SPARSE_ARCH_ARM32,
                              0U, 0U, buffer_write, &image, &output_size) ==
           GOTH_SPARSE_OK);
    assert(image.size == output_size);
    assert(image.size < 400U); /* the 1 KiB bss must not be stored */

    assert(goth_sparse_open(image.bytes, image.size, &parsed) ==
           GOTH_SPARSE_OK);
    assert(parsed.header.arch == GOTH_SPARSE_ARCH_ARM32);
    assert(parsed.header.entry == 0x1000U);
    assert(parsed.header.memory_size ==
           (uint64_t)sizeof(code) + sizeof(data) + 1024U);
    assert(strcmp(goth_sparse_arch_name(parsed.header.arch), "arm32") == 0);
    assert(goth_sparse_verify(&parsed) == GOTH_SPARSE_OK);

    memset(&destination, 0, sizeof(destination));
    assert(goth_sparse_apply(&parsed, memory_store, &destination) ==
           GOTH_SPARSE_OK);
    assert(memcmp(destination.memory + 0x1000U, code, sizeof(code)) == 0);
    assert(memcmp(destination.memory + 0x8000U, data, sizeof(data)) == 0);
    for (size_t index = 0U; index < 1024U; index++) {
        assert(destination.memory[0x8000U + sizeof(data) + index] == 0U);
    }

    goth_sparse_iterator_begin(&parsed, &iterator);
    while (goth_sparse_next_chunk(&iterator, &chunk)) {
        if (chunk.type == GOTH_SPARSE_CHUNK_DATA) {
            data_chunks++;
        } else {
            zero_chunks++;
        }
    }
    assert(data_chunks >= 2U); /* at least the two data segments */
    assert(zero_chunks >= 2U); /* the 32-byte gap and the 1 KiB bss */

    free(image.bytes);
}

static void test_bad_input(void)
{
    goth_sparse_image_t image;
    uint8_t truncated[8] = {'S', 'P', 'R', 'S', 'K', '3', 0, 0};

    assert(goth_sparse_open(NULL, 0U, &image) == GOTH_SPARSE_INVALID_ARGUMENT);
    assert(goth_sparse_open(truncated, sizeof(truncated), &image) ==
           GOTH_SPARSE_TRUNCATED);

    {
        uint8_t wrong_magic[GOTH_SPARSE_HEADER_SIZE] = {0};
        assert(goth_sparse_open(wrong_magic, sizeof(wrong_magic), &image) ==
               GOTH_SPARSE_BAD_MAGIC);
    }
    {
        uint8_t good[GOTH_SPARSE_HEADER_SIZE] = {'S', 'P', 'R', 'S', 'K', '3'};
        /* Right magic, but version 0 is not the current format. */
        assert(goth_sparse_open(good, sizeof(good), &image) ==
               GOTH_SPARSE_BAD_VERSION);
    }
}

static void test_corruption_detected(void)
{
    uint8_t payload[16];
    goth_sparse_segment_t segment;
    buffer_t image = {NULL, 0U, 0U};
    goth_sparse_image_t parsed;

    memset(payload, 0x11, sizeof(payload));
    segment.load_address = 0x2000U;
    segment.data = payload;
    segment.file_size = sizeof(payload);
    segment.memory_size = sizeof(payload);
    segment.flags = 0U;

    image.capacity = 4096U;
    image.bytes = malloc(image.capacity);
    assert(image.bytes != NULL);
    assert(goth_sparse_encode(&segment, 1U, 0x2000U, GOTH_SPARSE_ARCH_AMD64,
                              0U, 0U, buffer_write, &image, NULL) ==
           GOTH_SPARSE_OK);
    assert(goth_sparse_open(image.bytes, image.size, &parsed) ==
           GOTH_SPARSE_OK);
    assert(goth_sparse_verify(&parsed) == GOTH_SPARSE_OK);

    /* Flip one payload byte and the CRC must catch it. */
    image.bytes[image.size - 1U] ^= 0xffU;
    assert(goth_sparse_verify(&parsed) == GOTH_SPARSE_BAD_PAYLOAD_CRC);

    /* Corrupt the header and open must reject it. */
    image.bytes[12] ^= 0xffU;
    assert(goth_sparse_open(image.bytes, image.size, &parsed) ==
           GOTH_SPARSE_BAD_HEADER_CRC);

    free(image.bytes);
}

static void test_chunk_limit(void)
{
    uint8_t payload[100];
    goth_sparse_segment_t segment;
    buffer_t image = {NULL, 0U, 0U};
    goth_sparse_image_t parsed;
    goth_sparse_iterator_t iterator;
    goth_sparse_chunk_t chunk;
    uint32_t chunks = 0U;

    memset(payload, 0x7f, sizeof(payload));
    segment.load_address = 0x3000U;
    segment.data = payload;
    segment.file_size = sizeof(payload);
    segment.memory_size = sizeof(payload);
    segment.flags = 0U;

    image.capacity = 8192U;
    image.bytes = malloc(image.capacity);
    assert(image.bytes != NULL);
    /* A 32-byte chunk limit splits the 100 bytes into 4 DATA chunks. */
    assert(goth_sparse_encode(&segment, 1U, 0x3000U, GOTH_SPARSE_ARCH_ARM32,
                              0U, 32U, buffer_write, &image, NULL) ==
           GOTH_SPARSE_OK);
    assert(goth_sparse_open(image.bytes, image.size, &parsed) ==
           GOTH_SPARSE_OK);
    assert(parsed.header.chunk_count == 4U);
    goth_sparse_iterator_begin(&parsed, &iterator);
    while (goth_sparse_next_chunk(&iterator, &chunk)) {
        assert(chunk.type == GOTH_SPARSE_CHUNK_DATA);
        assert(chunk.length <= 32U);
        chunks++;
    }
    assert(chunks == 4U);
    free(image.bytes);
}

int main(void)
{
    test_crc32();
    test_round_trip();
    test_bad_input();
    test_corruption_detected();
    test_chunk_limit();
    return 0;
}
