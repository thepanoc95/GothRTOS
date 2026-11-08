#include "gothrtos/95exc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bytes[2048];
    size_t size;
} test_file_t;

typedef struct {
    uint8_t bytes[8192];
    size_t size;
} test_output_t;

typedef struct {
    uint8_t memory[0x2000];
    uint32_t bias;
    bool reserved;
    bool released;
    bool dependency_loaded;
    bool dependency_unloaded;
    bool symbol_published;
    bool symbol_unpublished;
    bool decompressed;
    uint32_t permissions[2];
} test_loader_t;

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] |
                      ((uint16_t)bytes[1] << 8));
}

static void put_u64(uint8_t *bytes, uint64_t value)
{
    put_u32(bytes, (uint32_t)value);
    put_u32(bytes + 4, (uint32_t)(value >> 32));
}

static bool read_file(void *context,
                      uint64_t offset,
                      void *buffer,
                      size_t size)
{
    test_file_t *file = context;
    if (offset > file->size || size > file->size - (size_t)offset) {
        return false;
    }
    memcpy(buffer, file->bytes + (size_t)offset, size);
    return true;
}

static bool write_output(void *context,
                         uint64_t offset,
                         const void *buffer,
                         size_t size)
{
    test_output_t *output = context;
    if (offset > sizeof(output->bytes) ||
        size > sizeof(output->bytes) - (size_t)offset) {
        return false;
    }
    memcpy(output->bytes + (size_t)offset, buffer, size);
    if (offset + size > output->size) {
        output->size = (size_t)(offset + size);
    }
    return true;
}

static void setup_header(test_file_t *file,
                         uint16_t type,
                         uint16_t machine,
                         uint16_t segment_count,
                         uint64_t dynamic_offset,
                         uint64_t dynamic_size)
{
    static const uint8_t magic[GOTH95_MAGIC_SIZE] = {
        '9', '5', 'E', 'X', 'C', 0, '\r', '\n'
    };
    uint8_t *header = file->bytes;

    memcpy(header, magic, sizeof(magic));
    put_u16(header + 8, GOTH95_VERSION);
    put_u16(header + 10, GOTH95_HEADER_SIZE);
    put_u16(header + 12, type);
    put_u16(header + 14, machine);
    put_u32(header + 20, 0x1000);
    put_u64(header + 24, GOTH95_HEADER_SIZE);
    put_u16(header + 32, GOTH95_SEGMENT_SIZE);
    put_u16(header + 34, segment_count);
    put_u64(header + 36, dynamic_offset);
    put_u64(header + 44, dynamic_size);
    file->size = 1024;
}

static void put_segment(uint8_t *bytes,
                       uint64_t file_offset,
                       uint32_t virtual_address,
                       uint32_t file_size,
                       uint32_t memory_size,
                       uint32_t permissions)
{
    put_u32(bytes, 1);
    put_u32(bytes + 4, permissions);
    put_u64(bytes + 8, file_offset);
    put_u32(bytes + 16, virtual_address);
    put_u32(bytes + 20, file_size);
    put_u32(bytes + 24, memory_size);
    put_u32(bytes + 28, 1);
}

static void put_dynamic(uint8_t *bytes, uint32_t tag, uint64_t value)
{
    put_u32(bytes, tag);
    put_u64(bytes + 8, value);
}

static void make_executable(test_file_t *file)
{
    setup_header(file, GOTH95_TYPE_EXECUTABLE, GOTH95_MACHINE_I386,
                 1, 0, 0);
    put_segment(file->bytes + 64, 104, 0x1000, 4, 4,
                GOTH95_PERM_READ | GOTH95_PERM_EXECUTE);
    memcpy(file->bytes + 104, "\x90\x90\x90\xc3", 4);
    file->size = 108;
}

static void make_dynamic_module(test_file_t *file)
{
    const uint64_t dynamic_offset = 404;
    uint8_t *data;
    uint8_t *symbol;
    uint8_t *relocation;
    uint8_t *exported_symbol;
    uint8_t *dynamic;
    static const char strings[] = "\0dep95.so\0foo\0mod95\0";

    memset(file, 0, sizeof(*file));
    setup_header(file, GOTH95_TYPE_SHARED, GOTH95_MACHINE_I386,
                 2, dynamic_offset, 11 * GOTH95_DYNAMIC_SIZE);
    put_u32(file->bytes + 20, 0);
    put_segment(file->bytes + 64, 144, 0x1000, 4, 4,
                GOTH95_PERM_READ | GOTH95_PERM_EXECUTE);
    put_segment(file->bytes + 104, 148, 0x2000, 256, 256,
                GOTH95_PERM_READ | GOTH95_PERM_WRITE);
    memcpy(file->bytes + 144, "\x90\x90\x90\xc3", 4);

    data = file->bytes + 148;
    memcpy(data + 0x40, strings, sizeof(strings));
    put_u32(data + 0x20, 5);
    symbol = data + 0x80 + GOTH95_SYMBOL_SIZE;
    put_u32(symbol, 10);
    put_u32(symbol + 8, 4);
    symbol[12] = 0x10;
    put_u16(symbol + 16, 1);
    exported_symbol = symbol + GOTH95_SYMBOL_SIZE;
    put_u32(exported_symbol, 10);
    put_u32(exported_symbol + 4, 0x1001);
    put_u32(exported_symbol + 8, 3);
    exported_symbol[12] = 0x10;
    put_u16(exported_symbol + 14, 1);
    put_u16(exported_symbol + 16, 1);
    relocation = data + 0xc0;
    put_u32(relocation, 0x2020);
    put_u32(relocation + 4, (1u << 8) | 1u);
    put_u32(relocation + 8, 5);

    dynamic = file->bytes + dynamic_offset;
    put_dynamic(dynamic, GOTH95_DYN_STRTAB, 0x2040);
    put_dynamic(dynamic + 16, GOTH95_DYN_STRSZ, sizeof(strings));
    put_dynamic(dynamic + 32, GOTH95_DYN_SYMTAB, 0x2080);
    put_dynamic(dynamic + 48, GOTH95_DYN_SYMENT, GOTH95_SYMBOL_SIZE);
    put_dynamic(dynamic + 64, GOTH95_DYN_RELA, 0x20c0);
    put_dynamic(dynamic + 80, GOTH95_DYN_RELASZ, GOTH95_RELOCATION_SIZE);
    put_dynamic(dynamic + 96, GOTH95_DYN_RELAENT, GOTH95_RELOCATION_SIZE);
    put_dynamic(dynamic + 112, GOTH95_DYN_NEEDED, 1);
    put_dynamic(dynamic + 128, GOTH95_DYN_SONAME, 14);
    put_dynamic(dynamic + 144, GOTH95_DYN_SYMBOL_COUNT, 3);
    put_dynamic(dynamic + 160, GOTH95_DYN_NULL, 0);
    file->size = (size_t)dynamic_offset + 11 * GOTH95_DYNAMIC_SIZE;
}

static bool reserve_memory(void *context,
                          uint32_t minimum,
                          uint32_t maximum,
                          uint32_t alignment,
                          uint32_t preferred_bias,
                          uint32_t *load_bias)
{
    test_loader_t *loader = context;
    assert(minimum == 0x1000);
    assert(maximum > minimum);
    assert(alignment == 1);
    loader->bias = preferred_bias == UINT32_MAX ? 0x10000 : 0;
    *load_bias = loader->bias;
    loader->reserved = true;
    return true;
}

static bool memory_range(test_loader_t *loader,
                         uint32_t address,
                         size_t size,
                         size_t *offset)
{
    uint64_t start = (uint64_t)0x1000 + loader->bias;
    uint64_t relative;

    if (address < start) {
        return false;
    }
    relative = (uint64_t)address - start;
    if (relative > sizeof(loader->memory) ||
        size > sizeof(loader->memory) - (size_t)relative) {
        return false;
    }
    *offset = (size_t)relative;
    return true;
}

static bool write_memory(void *context,
                         uint32_t address,
                         const void *data,
                         size_t size)
{
    test_loader_t *loader = context;
    size_t offset;
    if (!memory_range(loader, address, size, &offset)) {
        return false;
    }
    memcpy(loader->memory + offset, data, size);
    return true;
}

static bool read_memory(void *context,
                        uint32_t address,
                        void *data,
                        size_t size)
{
    test_loader_t *loader = context;
    size_t offset;
    if (!memory_range(loader, address, size, &offset)) {
        return false;
    }
    memcpy(data, loader->memory + offset, size);
    return true;
}

static bool zero_memory(void *context, uint32_t address, size_t size)
{
    test_loader_t *loader = context;
    size_t offset;
    if (!memory_range(loader, address, size, &offset)) {
        return false;
    }
    memset(loader->memory + offset, 0, size);
    return true;
}

static bool protect_memory(void *context,
                           uint32_t address,
                           size_t size,
                           uint32_t permissions)
{
    test_loader_t *loader = context;
    size_t offset;
    assert(memory_range(loader, address, size, &offset));
    (void)offset;
    if (address == 0x1000 + loader->bias) {
        loader->permissions[0] = permissions;
    } else {
        loader->permissions[1] = permissions;
    }
    return true;
}

static void release_memory(void *context,
                           uint32_t minimum,
                           uint32_t maximum,
                           uint32_t load_bias)
{
    test_loader_t *loader = context;
    assert(minimum == 0x1000);
    assert(maximum > minimum);
    assert(load_bias == loader->bias);
    loader->released = true;
}

static bool load_dependency(void *context, const char *name)
{
    test_loader_t *loader = context;
    assert(strcmp(name, "dep95.so") == 0);
    loader->dependency_loaded = true;
    return true;
}

static void unload_dependency(void *context, const char *name)
{
    test_loader_t *loader = context;
    assert(strcmp(name, "dep95.so") == 0);
    loader->dependency_unloaded = true;
}

static bool resolve_symbol(void *context,
                           const char *name,
                           uint16_t version,
                           uint32_t *address)
{
    (void)context;
    if (strcmp(name, "foo") != 0 || version != 1) {
        return false;
    }
    *address = 0x1234;
    return true;
}

static void *allocate_temporary(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void free_temporary(void *context, void *memory)
{
    (void)context;
    free(memory);
}

static bool decompress_segment(void *context,
                               uint32_t algorithm,
                               const void *compressed,
                               size_t compressed_size,
                               void *uncompressed,
                               size_t uncompressed_capacity,
                               size_t *uncompressed_size)
{
    test_loader_t *loader = context;
    static const uint8_t expected[] = {0x90, 0x90, 0x90, 0xc3};

    if (algorithm != 42 || compressed_size != 1 ||
        *(const uint8_t *)compressed != 42 ||
        uncompressed_capacity < sizeof(expected)) {
        return false;
    }
    memcpy(uncompressed, expected, sizeof(expected));
    *uncompressed_size = sizeof(expected);
    loader->decompressed = true;
    return true;
}

static bool publish_symbol(void *context,
                           const char *module_name,
                           const char *name,
                           uint16_t version,
                           uint32_t address,
                           uint32_t size)
{
    test_loader_t *loader = context;
    assert(strcmp(module_name, "mod95") == 0);
    assert(strcmp(name, "foo") == 0);
    assert(version == 1);
    assert(address == 0x11001);
    assert(size == 3);
    loader->symbol_published = true;
    return true;
}

static void unpublish_module(void *context, const char *module_name)
{
    test_loader_t *loader = context;
    assert(strcmp(module_name, "mod95") == 0);
    loader->symbol_unpublished = true;
}

static void test_static_load(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;
    goth95_module_t module;
    test_loader_t loader = {0};
    goth95_loader_ops_t operations = {
        .context = &loader,
        .reserve = reserve_memory,
        .write = write_memory,
        .read = read_memory,
        .zero = zero_memory,
        .protect = protect_memory,
        .release = release_memory
    };

    make_executable(&file);
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_load(&image, &operations, &module) == GOTH95_OK);
    assert(module.loaded && module.entry == 0x1000);
    assert(memcmp(loader.memory, "\x90\x90\x90\xc3", 4) == 0);
    assert(loader.permissions[0] ==
           (GOTH95_PERM_READ | GOTH95_PERM_EXECUTE));
    assert(goth95_unload(&image, &operations, &module) == GOTH95_OK);
    assert(loader.released && !module.loaded);
}

static void test_dynamic_load(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;
    goth95_module_t module;
    test_loader_t loader = {0};
    uint32_t relocated;
    goth95_loader_ops_t operations = {
        .context = &loader,
        .reserve = reserve_memory,
        .write = write_memory,
        .read = read_memory,
        .zero = zero_memory,
        .protect = protect_memory,
        .release = release_memory,
        .allocate_temporary = allocate_temporary,
        .free_temporary = free_temporary,
        .load_dependency = load_dependency,
        .unload_dependency = unload_dependency,
        .resolve_symbol = resolve_symbol,
        .publish_symbol = publish_symbol,
        .unpublish_module = unpublish_module
    };

    make_dynamic_module(&file);
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_load(&image, &operations, &module) == GOTH95_OK);
    assert(module.loaded && module.entry == 0);
    assert(loader.dependency_loaded);
    relocated = get_u32(loader.memory + 0x1020);
    assert(relocated == 0x1239);
    assert(loader.permissions[0] ==
           (GOTH95_PERM_READ | GOTH95_PERM_EXECUTE));
    assert(loader.permissions[1] ==
           (GOTH95_PERM_READ | GOTH95_PERM_WRITE));
    assert(loader.symbol_published);
    assert(goth95_unload(&image, &operations, &module) == GOTH95_OK);
    assert(loader.dependency_unloaded);
    assert(loader.symbol_unpublished);
    assert(loader.released);
}

static void test_compressed_segment(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;
    goth95_module_t module;
    test_loader_t loader = {0};
    goth95_loader_ops_t operations = {
        .context = &loader,
        .reserve = reserve_memory,
        .write = write_memory,
        .read = read_memory,
        .zero = zero_memory,
        .protect = protect_memory,
        .release = release_memory,
        .allocate_temporary = allocate_temporary,
        .free_temporary = free_temporary,
        .decompress = decompress_segment
    };

    make_executable(&file);
    file.bytes[64 + 32] = 42;
    file.bytes[104] = 42;
    put_u32(file.bytes + 64 + 20, 1);
    file.size = 105;
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_load(&image, &operations, &module) == GOTH95_OK);
    assert(loader.decompressed);
    assert(memcmp(loader.memory, "\x90\x90\x90\xc3", 4) == 0);
    assert(goth95_unload(&image, &operations, &module) == GOTH95_OK);
}

static void test_elf32_export(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;
    test_output_t output = {0};
    test_loader_t loader = {0};
    goth95_sink_t sink = {
        .context = &output,
        .capacity = sizeof(output.bytes),
        .write_at = write_output
    };
    goth95_loader_ops_t codec_operations = {
        .context = &loader,
        .allocate_temporary = allocate_temporary,
        .free_temporary = free_temporary
    };
    uint64_t output_size;

    make_executable(&file);
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_export_elf32(&image, &sink, &codec_operations,
                               &output_size) == GOTH95_OK);
    assert(output_size == output.size);
    assert(output.bytes[0] == 0x7f &&
           memcmp(output.bytes + 1, "ELF", 3) == 0);
    assert(output.bytes[4] == 1 && output.bytes[5] == 1);
    assert(get_u32(output.bytes + 28) == 52);
    assert(get_u32(output.bytes + 52) == 1);

    make_executable(&file);
    file.bytes[64 + 32] = 42;
    file.bytes[104] = 42;
    put_u32(file.bytes + 64 + 20, 1);
    file.size = 105;
    source.size = file.size;
    codec_operations.decompress = decompress_segment;
    memset(&output, 0, sizeof(output));
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_export_elf32(&image, &sink, &codec_operations,
                               &output_size) == GOTH95_OK);
    assert(loader.decompressed);
    assert(get_u32(output.bytes + 52 + 16) == 4);
    assert(memcmp(output.bytes + 84, "\x90\x90\x90\xc3", 4) == 0);
}

static void test_dynamic_elf32_export(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;
    test_output_t output = {0};
    test_loader_t loader = {0};
    goth95_sink_t sink = {
        .context = &output,
        .capacity = sizeof(output.bytes),
        .write_at = write_output
    };
    goth95_loader_ops_t codec_operations = {
        .context = &loader,
        .allocate_temporary = allocate_temporary,
        .free_temporary = free_temporary
    };
    uint64_t output_size;
    uint32_t dynamic_offset;
    uint32_t dynamic_size;
    uint32_t extension_file_offset;
    uint32_t extension_address;
    uint32_t relocation_address = 0;
    uint32_t relocation_size = 0;
    uint32_t relocation_entry_size = 0;
    uint32_t index;
    uint8_t segment_entry[GOTH95_SEGMENT_SIZE];
    bool found_rel = false;
    bool found_hash = false;

    make_dynamic_module(&file);
    memcpy(segment_entry, file.bytes + 64, sizeof(segment_entry));
    memcpy(file.bytes + 64, file.bytes + 104, sizeof(segment_entry));
    memcpy(file.bytes + 104, segment_entry, sizeof(segment_entry));
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    assert(goth95_image_open(&source, &image) == GOTH95_OK);
    assert(goth95_export_elf32(&image, &sink, &codec_operations,
                               &output_size) == GOTH95_UNSUPPORTED);
    assert(output_size == 0);

    put_u16(file.bytes + 148 + 0x80 + GOTH95_SYMBOL_SIZE + 16, 0);
    put_u16(file.bytes + 148 + 0x80 + 2 * GOTH95_SYMBOL_SIZE + 16, 0);
    memset(&output, 0, sizeof(output));
    assert(goth95_export_elf32(&image, &sink, &codec_operations,
                               &output_size) == GOTH95_OK);
    assert(output_size == output.size);
    assert(get_u16(output.bytes + 16) == 3);
    assert(get_u16(output.bytes + 44) == 4);
    assert(get_u32(output.bytes + 52 + 4) == 180);
    assert(get_u32(output.bytes + 52 + 16) == 4);
    assert(get_u32(output.bytes + 52 + 32 + 4) == 184);
    assert(get_u32(output.bytes + 52 + 32 + 16) == 256);
    assert(get_u32(output.bytes + 184 + 0x20) == 5);

    extension_file_offset = get_u32(output.bytes + 52 + 2 * 32 + 4);
    extension_address = get_u32(output.bytes + 52 + 2 * 32 + 8);
    dynamic_offset = get_u32(output.bytes + 52 + 3 * 32 + 4);
    dynamic_size = get_u32(output.bytes + 52 + 3 * 32 + 16);
    for (index = 0; index < dynamic_size; index += 8) {
        uint32_t tag = get_u32(output.bytes + dynamic_offset + index);
        uint32_t value = get_u32(output.bytes + dynamic_offset + index + 4);
        if (tag == 17) {
            found_rel = true;
            relocation_address = value;
        }
        if (tag == 18) {
            relocation_size = value;
        }
        if (tag == 19) {
            relocation_entry_size = value;
        }
        if (tag == 4) {
            found_hash = true;
        }
    }
    assert(found_rel && found_hash);
    assert(relocation_size == 8 && relocation_entry_size == 8);
    assert(get_u32(output.bytes + extension_file_offset +
                    relocation_address - extension_address) == 0x2020);
    assert(get_u32(output.bytes + extension_file_offset +
                    relocation_address - extension_address + 4) ==
           ((1u << 8) | 1u));
}

static void test_rejects_bad_images(void)
{
    test_file_t file = {0};
    goth95_source_t source;
    goth95_image_t image;

    make_executable(&file);
    source.context = &file;
    source.size = file.size;
    source.read_at = read_file;
    file.bytes[0] = 'X';
    assert(goth95_image_open(&source, &image) == GOTH95_BAD_FORMAT);

    make_executable(&file);
    put_u16(file.bytes + 14, 62);
    assert(goth95_image_open(&source, &image) == GOTH95_UNSUPPORTED);

    make_executable(&file);
    put_u16(file.bytes + 8, GOTH95_VERSION + 1U);
    assert(goth95_image_open(&source, &image) == GOTH95_BAD_FORMAT);

    make_executable(&file);
    put_u32(file.bytes + 64 + 4,
            GOTH95_PERM_WRITE | GOTH95_PERM_EXECUTE);
    assert(goth95_image_open(&source, &image) == GOTH95_BAD_FORMAT);
}

int main(void)
{
    test_static_load();
    test_dynamic_load();
    test_compressed_segment();
    test_elf32_export();
    test_dynamic_elf32_export();
    test_rejects_bad_images();
    return 0;
}
