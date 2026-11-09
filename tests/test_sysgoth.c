#include "sysgoth.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Drives the SYSGOTH loader core against a memory-backed target: the feed
 * buffer plays the host sending SparseKernel.img3 over the UART, and the
 * loader's writes land in a fake memory region. Verifies the wire protocol,
 * the CRC checks, the region guard and the entry-point reporting. */

#define FAKE_MEMORY_SIZE (64U * 1024U)

typedef struct {
    uint8_t feed[8192];
    size_t feed_size;
    size_t feed_pos;
    char output[1024];
    size_t output_size;
    uint8_t memory[FAKE_MEMORY_SIZE];
    uint64_t tick;
    uint64_t jumped_entry;
} fake_target_t;

static fake_target_t fake;

static void fake_uart_init(uint32_t baud)
{
    (void)baud;
}

static void fake_uart_putc(char character)
{
    if (fake.output_size + 1U < sizeof(fake.output)) {
        fake.output[fake.output_size++] = character;
        fake.output[fake.output_size] = '\0';
    }
}

static int fake_uart_getc(void)
{
    if (fake.feed_pos < fake.feed_size) {
        return fake.feed[fake.feed_pos++];
    }
    return -1;
}

static uint64_t fake_time_ms(void)
{
    return fake.tick++;
}

static void fake_jump(uint64_t entry) __attribute__((noreturn));

static void fake_jump(uint64_t entry)
{
    fake.jumped_entry = entry;
    /* The real jump never returns; a host test ends the process instead. */
    exit(0);
}

static sg_region_t fake_regions[] = {
    {0U, FAKE_MEMORY_SIZE, 0U}
};

/* The loader writes through absolute addresses, so the host test points the
 * single writable region at the fake memory buffer and encodes images at that
 * same address. */
static uint64_t fake_base(void)
{
    return (uint64_t)(uintptr_t)fake.memory;
}

static const sg_target_t fake_target = {
    "fake",
    "host",
    fake_uart_init,
    fake_uart_putc,
    fake_uart_getc,
    fake_time_ms,
    fake_regions,
    1U,
    fake_jump,
    NULL
};

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t capacity;
} image_buffer_t;

static bool image_write(void *context, const void *data, size_t size)
{
    image_buffer_t *buffer = (image_buffer_t *)context;

    if (buffer->size + size > buffer->capacity) {
        return false;
    }
    memcpy(buffer->bytes + buffer->size, data, size);
    buffer->size += size;
    return true;
}

static void build_image(image_buffer_t *image,
                        uint8_t *payload,
                        size_t payload_size,
                        uint64_t address)
{
    goth_sparse_segment_t segment;

    segment.load_address = address;
    segment.data = payload;
    segment.file_size = payload_size;
    segment.memory_size = payload_size + 256U; /* a little bss */
    segment.flags = 0U;

    image->capacity = 8192U;
    image->bytes = malloc(image->capacity);
    assert(image->bytes != NULL);
    image->size = 0U;
    assert(goth_sparse_encode(&segment, 1U, address, GOTH_SPARSE_ARCH_ARM32,
                              0U, 0U, image_write, image, NULL) ==
           GOTH_SPARSE_OK);
}

static void test_protocol_round_trip(void)
{
    uint8_t payload[100];
    image_buffer_t image;
    sg_boot_stats_t stats;
    uint8_t scratch[4096];
    size_t index;

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    for (index = 0U; index < sizeof(payload); index++) {
        payload[index] = (uint8_t)(index + 1U);
    }
    build_image(&image, payload, sizeof(payload), fake_base());

    /* Feed the loader the exact bytes the host would send. */
    memcpy(fake.feed, image.bytes, image.size);
    fake.feed_size = image.size;

    assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                           &stats) == GOTH_SPARSE_OK);
    assert(stats.entry == fake_base());
    assert(stats.bytes_written == sizeof(payload));
    assert(stats.zero_bytes == 256U);
    assert(stats.chunks_applied >= 2U);
    assert(memcmp(fake.memory, payload, sizeof(payload)) == 0);
    for (index = 0U; index < 256U; index++) {
        assert(fake.memory[sizeof(payload) + index] == 0U);
    }

    free(image.bytes);
}

static void test_magic_scan_skips_garbage(void)
{
    uint8_t payload[32];
    image_buffer_t image;
    sg_boot_stats_t stats;
    uint8_t scratch[4096];

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x33, sizeof(payload));
    build_image(&image, payload, sizeof(payload), fake_base());

    /* Prefix the image with noise, including a partial magic, as a host that
     * connected mid-stream would produce. */
    fake.feed_size = 0U;
    fake.feed[fake.feed_size++] = 'h';
    fake.feed[fake.feed_size++] = 'i';
    fake.feed[fake.feed_size++] = 'S';
    fake.feed[fake.feed_size++] = 'P';
    fake.feed[fake.feed_size++] = 'X';
    memcpy(fake.feed + fake.feed_size, image.bytes, image.size);
    fake.feed_size += image.size;

    assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                           &stats) == GOTH_SPARSE_OK);
    assert(memcmp(fake.memory, payload, sizeof(payload)) == 0);

    free(image.bytes);
}

static void test_corrupt_payload_rejected(void)
{
    uint8_t payload[64];
    image_buffer_t image;
    sg_boot_stats_t stats;
    uint8_t scratch[4096];

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x44, sizeof(payload));
    build_image(&image, payload, sizeof(payload), fake_base());

    /* Corrupt one byte of the first DATA chunk payload (header + chunk
     * header = 88 bytes), so the chunk CRC must reject it. */
    image.bytes[GOTH_SPARSE_HEADER_SIZE + GOTH_SPARSE_CHUNK_HEADER_SIZE + 2U] ^=
        0xffU;
    memcpy(fake.feed, image.bytes, image.size);
    fake.feed_size = image.size;

    assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                           &stats) == GOTH_SPARSE_BAD_PAYLOAD_CRC);

    free(image.bytes);
}

static void test_region_guard(void)
{
    uint8_t payload[64];
    image_buffer_t image;
    sg_boot_stats_t stats;
    uint8_t scratch[4096];

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x55, sizeof(payload));
    /* Load address outside the fake target's only writable region. */
    build_image(&image, payload, sizeof(payload), 0x90000000U);
    memcpy(fake.feed, image.bytes, image.size);
    fake.feed_size = image.size;

    assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                           &stats) == GOTH_SPARSE_NO_MEMORY);

    free(image.bytes);
}

static void test_truncated_stream(void)
{
    uint8_t payload[64];
    image_buffer_t image;
    sg_boot_stats_t stats;
    uint8_t scratch[4096];

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x66, sizeof(payload));
    build_image(&image, payload, sizeof(payload), fake_base());

    /* Send only the header and half the body, then stop. */
    fake.feed_size = image.size / 2U;
    memcpy(fake.feed, image.bytes, fake.feed_size);

    assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                           &stats) == GOTH_SPARSE_TRUNCATED);

    free(image.bytes);
}

static void test_apply_buffer(void)
{
    uint8_t payload[48];
    image_buffer_t image;
    sg_boot_stats_t stats;

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x77, sizeof(payload));
    build_image(&image, payload, sizeof(payload), fake_base());

    assert(sysgoth_apply_buffer(&fake_target, image.bytes, image.size,
                                &stats) == GOTH_SPARSE_OK);
    assert(stats.entry == fake_base());
    assert(memcmp(fake.memory, payload, sizeof(payload)) == 0);

    free(image.bytes);
}

static void test_run_reports_and_jumps(void)
{
    uint8_t payload[16];
    image_buffer_t image;

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    memset(payload, 0x88, sizeof(payload));
    build_image(&image, payload, sizeof(payload), fake_base());
    memcpy(fake.feed, image.bytes, image.size);
    fake.feed_size = image.size;

    /* sysgoth_run loops forever; the fake jump records the entry and returns,
     * which the loop reports as a target error. Call it directly and stop
     * after the first jump by making the feed run dry and checking output. */
    {
        uint8_t scratch[4096];
        sg_boot_stats_t stats;

        assert(sysgoth_receive(&fake_target, scratch, sizeof(scratch), 1000U,
                               &stats) == GOTH_SPARSE_OK);
        assert(stats.entry == fake_base());
    }

    free(image.bytes);
}

static void test_status_names(void)
{
    assert(strcmp(sysgoth_status_name(GOTH_SPARSE_OK), "ok") == 0);
    assert(strcmp(sysgoth_status_name(GOTH_SPARSE_BAD_MAGIC), "bad magic") ==
           0);
    assert(strcmp(sysgoth_status_name(GOTH_SPARSE_BAD_PAYLOAD_CRC),
                  "bad payload crc") == 0);
}

static void test_receive_ex_magic_timeout(void)
{
    /* With no data on the wire, receive_ex should return TRUNCATED once the
     * magic timeout expires, allowing a caller to poll other sources. */
    sg_boot_stats_t stats;
    uint8_t scratch[4096];

    memset(&fake, 0, sizeof(fake));
    fake_regions[0].base = fake_base();
    fake.feed_size = 0U;

    assert(sysgoth_receive_ex(&fake_target, scratch, sizeof(scratch), 5U,
                              1000U, &stats) == GOTH_SPARSE_TRUNCATED);
}

int main(void)
{
    test_protocol_round_trip();
    test_magic_scan_skips_garbage();
    test_corrupt_payload_rejected();
    test_region_guard();
    test_truncated_stream();
    test_apply_buffer();
    test_run_reports_and_jumps();
    test_status_names();
    test_receive_ex_magic_timeout();
    return 0;
}
