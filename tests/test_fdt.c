#include "gothrtos/fdt.h"

#include <assert.h>
#include <string.h>

enum {
    TEST_BLOB_SIZE = 1024,
    TEST_STRUCTURE_OFFSET = 72,
    TEST_STRINGS_OFFSET = 512,
    TEST_RESERVE_OFFSET = 40
};

typedef struct {
    uint8_t bytes[TEST_BLOB_SIZE];
    uint32_t structure_size;
    uint32_t strings_size;
} test_blob_t;

static void put_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void put_be64(uint8_t *bytes, uint64_t value)
{
    put_be32(bytes, (uint32_t)(value >> 32));
    put_be32(bytes + 4, (uint32_t)value);
}

static uint32_t get_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static uint32_t add_string(test_blob_t *blob, const char *string)
{
    const uint32_t offset = blob->strings_size;
    size_t size = strlen(string) + 1;
    memcpy(blob->bytes + TEST_STRINGS_OFFSET + offset, string, size);
    blob->strings_size += (uint32_t)size;
    return offset;
}

static void add_token(test_blob_t *blob, uint32_t token)
{
    put_be32(blob->bytes + TEST_STRUCTURE_OFFSET +
                 blob->structure_size,
             token);
    blob->structure_size += 4;
}

static void add_node(test_blob_t *blob, const char *name)
{
    size_t size = strlen(name) + 1;
    uint32_t padded = ((uint32_t)size + 3u) & ~3u;
    add_token(blob, 1);
    memcpy(blob->bytes + TEST_STRUCTURE_OFFSET +
               blob->structure_size,
           name, size);
    memset(blob->bytes + TEST_STRUCTURE_OFFSET +
               blob->structure_size + size,
           0, padded - size);
    blob->structure_size += padded;
}

static void add_property(test_blob_t *blob,
                         uint32_t name_offset,
                         const void *value,
                         uint32_t size)
{
    uint32_t padded = (size + 3u) & ~3u;
    add_token(blob, 3);
    put_be32(blob->bytes + TEST_STRUCTURE_OFFSET +
                 blob->structure_size,
             size);
    put_be32(blob->bytes + TEST_STRUCTURE_OFFSET +
                 blob->structure_size + 4,
             name_offset);
    blob->structure_size += 8;
    if (size != 0) {
        memcpy(blob->bytes + TEST_STRUCTURE_OFFSET +
                   blob->structure_size,
               value, size);
    }
    memset(blob->bytes + TEST_STRUCTURE_OFFSET +
               blob->structure_size + size,
           0, padded - size);
    blob->structure_size += padded;
}

static void finish_blob(test_blob_t *blob)
{
    uint32_t total_size = TEST_STRINGS_OFFSET + blob->strings_size;
    uint8_t *header = blob->bytes;

    put_be32(header, GOTH_FDT_MAGIC);
    put_be32(header + 4, total_size);
    put_be32(header + 8, TEST_STRUCTURE_OFFSET);
    put_be32(header + 12, TEST_STRINGS_OFFSET);
    put_be32(header + 16, TEST_RESERVE_OFFSET);
    put_be32(header + 20, 17);
    put_be32(header + 24, 16);
    put_be32(header + 28, 0);
    put_be32(header + 32, blob->strings_size);
    put_be32(header + 36, blob->structure_size);

    put_be64(blob->bytes + TEST_RESERVE_OFFSET, 0x80000000u);
    put_be64(blob->bytes + TEST_RESERVE_OFFSET + 8, 0x1000);
    put_be64(blob->bytes + TEST_RESERVE_OFFSET + 16, 0);
    put_be64(blob->bytes + TEST_RESERVE_OFFSET + 24, 0);
}

static void make_test_blob(test_blob_t *blob)
{
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t reg[4];
    uint32_t reserved_cells[4];
    uint32_t name_address;
    uint32_t name_size;
    uint32_t name_compatible;
    uint32_t name_reg;
    uint32_t name_status;
    uint32_t name_empty;
    static const char compatible[] = "goth,device-v1\0simple-bus";

    memset(blob, 0, sizeof(*blob));
    name_address = add_string(blob, "#address-cells");
    name_size = add_string(blob, "#size-cells");
    name_compatible = add_string(blob, "compatible");
    name_reg = add_string(blob, "reg");
    name_status = add_string(blob, "status");
    name_empty = add_string(blob, "empty");

    address_cells = 2;
    size_cells = 2;
    add_node(blob, "");
    {
        uint8_t cell[4];
        put_be32(cell, address_cells);
        add_property(blob, name_address, cell, 4);
        put_be32(cell, size_cells);
        add_property(blob, name_size, cell, 4);
    }

    add_node(blob, "memory@80000000");
    add_property(blob, name_compatible, compatible,
                 sizeof(compatible));
    reg[0] = 0;
    reg[1] = 0x80000000u;
    reg[2] = 0;
    reg[3] = 0x00100000u;
    {
        uint8_t encoded[16];
        size_t index;
        for (index = 0; index < 4; index++) {
            put_be32(encoded + index * 4, reg[index]);
        }
        add_property(blob, name_reg, encoded, sizeof(encoded));
    }
    add_token(blob, 2);

    add_node(blob, "reserved-memory");
    add_property(blob, name_address, "\0\0\0\2", 4);
    add_property(blob, name_size, "\0\0\0\2", 4);
    add_node(blob, "firmware@80000000");
    reserved_cells[0] = 0;
    reserved_cells[1] = 0x80000000u;
    reserved_cells[2] = 0;
    reserved_cells[3] = 0x1000;
    {
        uint8_t encoded[16];
        size_t index;
        for (index = 0; index < 4; index++) {
            put_be32(encoded + index * 4, reserved_cells[index]);
        }
        add_property(blob, name_reg, encoded, sizeof(encoded));
    }
    add_token(blob, 2);
    add_token(blob, 2);

    add_node(blob, "chosen");
    add_property(blob, name_status, "okay", 5);
    add_property(blob, name_empty, NULL, 0);
    add_token(blob, 2);

    add_token(blob, 2);
    add_token(blob, 9);
    finish_blob(blob);
}

static void test_open_and_tree(void)
{
    test_blob_t blob;
    goth_fdt_t fdt;
    goth_fdt_node_t root;
    goth_fdt_node_t memory;
    goth_fdt_node_t parent;
    goth_fdt_node_t found;
    goth_fdt_node_t iterated;
    goth_fdt_node_t invalid_node;
    goth_fdt_property_t property;
    const char *name;
    size_t name_size;

    make_test_blob(&blob);
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_OK);
    assert(goth_fdt_root(&fdt, &root) == GOTH_FDT_OK);
    iterated = root;
    assert(goth_fdt_next_node(&fdt, &iterated) == GOTH_FDT_OK);
    assert(goth_fdt_get_name(&fdt, &iterated, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == strlen("memory@80000000"));
    assert(goth_fdt_next_node(&fdt, &iterated) == GOTH_FDT_OK);
    assert(goth_fdt_get_name(&fdt, &iterated, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == strlen("reserved-memory"));
    assert(goth_fdt_next_node(&fdt, &iterated) == GOTH_FDT_OK);
    assert(goth_fdt_get_name(&fdt, &iterated, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == strlen("firmware@80000000"));
    assert(goth_fdt_next_node(&fdt, &iterated) == GOTH_FDT_OK);
    assert(goth_fdt_get_name(&fdt, &iterated, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == strlen("chosen"));
    assert(goth_fdt_next_node(&fdt, &iterated) == GOTH_FDT_NOT_FOUND);
    assert(goth_fdt_get_name(&fdt, &root, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == 0);
    assert(goth_fdt_find_path(&fdt, "/memory@80000000", &memory) ==
           GOTH_FDT_OK);
    invalid_node = memory;
    invalid_node.depth = 0;
    assert(goth_fdt_next_node(&fdt, &invalid_node) ==
           GOTH_FDT_INVALID_ARGUMENT);
    assert(goth_fdt_get_property(&fdt, &invalid_node, "reg", &property) ==
           GOTH_FDT_INVALID_ARGUMENT);
    assert(goth_fdt_get_name(&fdt, &memory, &name, &name_size) ==
           GOTH_FDT_OK);
    assert(name_size == strlen("memory@80000000"));
    assert(goth_fdt_node_is_compatible(&fdt, &memory,
                                       "goth,device-v1"));
    assert(goth_fdt_node_is_compatible(&fdt, &memory, "simple-bus"));
    assert(!goth_fdt_node_is_compatible(&fdt, &memory, "other,bus"));
    assert(goth_fdt_get_property(&fdt, &memory, "reg", &property) ==
           GOTH_FDT_OK);
    assert(property.size == 16);
    assert(goth_fdt_parent(&fdt, &memory, &parent) == GOTH_FDT_OK);
    assert(parent.offset == root.offset);
    assert(goth_fdt_find_path(&fdt, "/chosen", &found) == GOTH_FDT_OK);
    assert(goth_fdt_get_property(&fdt, &found, "empty", &property) ==
           GOTH_FDT_OK);
    assert(property.size == 0);
    assert(goth_fdt_find_path(&fdt, "/missing", &found) ==
           GOTH_FDT_NOT_FOUND);
}

static void test_registers_and_reservations(void)
{
    test_blob_t blob;
    goth_fdt_t fdt;
    goth_fdt_node_t memory;
    goth_fdt_node_t firmware;
    goth_fdt_range_t range;
    goth_fdt_reservation_iterator_t iterator;

    make_test_blob(&blob);
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_OK);
    assert(goth_fdt_find_path(&fdt, "/memory@80000000", &memory) ==
           GOTH_FDT_OK);
    assert(goth_fdt_get_reg(&fdt, &memory, 0, &range) == GOTH_FDT_OK);
    assert(range.address == 0x80000000u);
    assert(range.size == 0x00100000u);
    assert(goth_fdt_get_reg(&fdt, &memory, 1, &range) ==
           GOTH_FDT_NOT_FOUND);
    assert(goth_fdt_find_path(&fdt,
                              "/reserved-memory/firmware@80000000",
                              &firmware) == GOTH_FDT_OK);
    assert(goth_fdt_get_reg(&fdt, &firmware, 0, &range) == GOTH_FDT_OK);
    assert(range.address == 0x80000000u && range.size == 0x1000);

    goth_fdt_reservation_begin(&fdt, &iterator);
    assert(goth_fdt_reservation_next(&fdt, &iterator, &range) ==
           GOTH_FDT_OK);
    assert(range.address == 0x80000000u && range.size == 0x1000);
    assert(goth_fdt_reservation_next(&fdt, &iterator, &range) ==
           GOTH_FDT_NOT_FOUND);
    goth_fdt_reservation_begin(&fdt, &iterator);
    iterator.offset++;
    assert(goth_fdt_reservation_next(&fdt, &iterator, &range) ==
           GOTH_FDT_BAD_FORMAT);
}

static void test_malformed_blobs(void)
{
    test_blob_t blob;
    goth_fdt_t fdt;
    uint32_t structure_offset;

    make_test_blob(&blob);
    assert(goth_fdt_open(blob.bytes, 16, &fdt) ==
           GOTH_FDT_INVALID_ARGUMENT);
    blob.bytes[0] = 0;
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_BAD_FORMAT);

    make_test_blob(&blob);
    put_be32(blob.bytes + 4, TEST_BLOB_SIZE + 1);
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_BAD_FORMAT);

    make_test_blob(&blob);
    put_be32(blob.bytes + 20, 15);
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_BAD_FORMAT);

    make_test_blob(&blob);
    structure_offset = get_be32(blob.bytes + 8);
    put_be32(blob.bytes + structure_offset, 99);
    assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
           GOTH_FDT_BAD_FORMAT);

    make_test_blob(&blob);
    {
        uint32_t old_strings_offset = TEST_STRINGS_OFFSET;
        uint32_t new_strings_offset =
            TEST_STRUCTURE_OFFSET + blob.structure_size;
        memmove(blob.bytes + new_strings_offset,
                blob.bytes + old_strings_offset, blob.strings_size);
        put_be32(blob.bytes + 12, new_strings_offset);
        put_be32(blob.bytes + 4, new_strings_offset + blob.strings_size);
        put_be32(blob.bytes + 20, 16);
        assert(goth_fdt_open(blob.bytes, sizeof(blob.bytes), &fdt) ==
               GOTH_FDT_OK);
        assert(fdt.version == 16);
    }
}

int main(void)
{
    test_open_and_tree();
    test_registers_and_reservations();
    test_malformed_blobs();
    return 0;
}
