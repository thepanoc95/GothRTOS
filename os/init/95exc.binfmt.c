#include "gothrtos/95exc.h"

#include <limits.h>
#include <string.h>

static const uint8_t goth95_magic[GOTH95_MAGIC_SIZE] = {
    '9', '5', 'E', 'X', 'C', 0, '\r', '\n'
};

enum {
    GOTH95_LZ_SYMBOL_UNDEFINED = 0,
    GOTH95_LZ_SYMBOL_ABSOLUTE = 0xfff1,
    GOTH95_BIND_GLOBAL = 1,
    GOTH95_BIND_WEAK = 2
};

typedef struct {
    uint32_t string_table;
    uint32_t string_size;
    uint32_t symbol_table;
    uint32_t symbol_entry_size;
    uint32_t relocation_table;
    uint32_t relocation_size;
    uint32_t relocation_entry_size;
    uint32_t symbol_count;
    uint32_t soname_offset;
    bool has_string_table;
    bool has_string_size;
    bool has_symbol_table;
    bool has_symbol_entry_size;
    bool has_symbol_count;
    bool has_relocation_table;
    bool has_relocation_size;
    bool has_relocation_entry_size;
    bool has_soname;
} goth95_dynamic_info_t;

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

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool range_within_file(uint64_t offset,
                              uint64_t size,
                              uint64_t file_size)
{
    return offset <= file_size && size <= file_size - offset;
}

static bool ranges_overlap(uint64_t first_offset,
                           uint64_t first_size,
                           uint64_t second_offset,
                           uint64_t second_size)
{
    return first_size != 0 && second_size != 0 &&
           first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

static bool source_read(const goth95_source_t *source,
                        uint64_t offset,
                        void *buffer,
                        size_t size)
{
    return range_within_file(offset, size, source->size) &&
           source->read_at(source->context, offset, buffer, size);
}

static bool is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static goth95_status_t read_segment(const goth95_source_t *source,
                                    uint64_t offset,
                                    goth95_segment_t *segment)
{
    uint8_t bytes[GOTH95_SEGMENT_SIZE];

    if (!source_read(source, offset, bytes, sizeof(bytes))) {
        return GOTH95_IO_ERROR;
    }
    segment->type = read_u32(bytes);
    segment->permissions = read_u32(bytes + 4);
    segment->file_offset = read_u64(bytes + 8);
    segment->virtual_address = read_u32(bytes + 16);
    segment->file_size = read_u32(bytes + 20);
    segment->memory_size = read_u32(bytes + 24);
    segment->alignment = read_u32(bytes + 28);
    segment->compression = read_u32(bytes + 32);

    if (read_u32(bytes + 36) != 0 ||
        segment->type != GOTH95_SEGMENT_LOAD ||
        (segment->permissions & ~(GOTH95_PERM_READ |
                                  GOTH95_PERM_WRITE |
                                  GOTH95_PERM_EXECUTE)) != 0 ||
        (segment->permissions & (GOTH95_PERM_WRITE |
                                 GOTH95_PERM_EXECUTE)) ==
            (GOTH95_PERM_WRITE | GOTH95_PERM_EXECUTE) ||
        segment->memory_size == 0 ||
        (segment->compression == GOTH95_COMPRESSION_NONE &&
         segment->file_size > segment->memory_size) ||
        (segment->compression != GOTH95_COMPRESSION_NONE &&
         segment->file_size == 0) ||
        (segment->alignment != 0 &&
         (!is_power_of_two(segment->alignment) ||
          ((segment->file_offset -
            (uint64_t)segment->virtual_address) &
           (segment->alignment - 1u)) != 0)) ||
        !range_within_file(segment->file_offset,
                           segment->file_size,
                           source->size) ||
        (uint64_t)segment->virtual_address + segment->memory_size >
            UINT32_MAX) {
        return GOTH95_BAD_FORMAT;
    }
    return GOTH95_OK;
}

goth95_status_t goth95_image_open(const goth95_source_t *source,
                                  goth95_image_t *image)
{
    uint8_t bytes[GOTH95_HEADER_SIZE];
    uint64_t table_size;
    uint64_t minimum_address = UINT64_MAX;
    uint64_t maximum_address = 0;
    size_t index;
    bool executable_entry = false;

    if (source == NULL || image == NULL || source->read_at == NULL ||
        source->size < GOTH95_HEADER_SIZE) {
        return GOTH95_INVALID_ARGUMENT;
    }
    if (!source_read(source, 0, bytes, sizeof(bytes))) {
        return GOTH95_IO_ERROR;
    }
    if (memcmp(bytes, goth95_magic, sizeof(goth95_magic)) != 0) {
        return GOTH95_BAD_FORMAT;
    }

    memset(image, 0, sizeof(*image));
    image->source = *source;
    image->header.version = read_u16(bytes + 8);
    image->header.header_size = read_u16(bytes + 10);
    image->header.type = read_u16(bytes + 12);
    image->header.machine = read_u16(bytes + 14);
    image->header.flags = read_u32(bytes + 16);
    image->header.entry = read_u32(bytes + 20);
    image->header.segment_table_offset = read_u64(bytes + 24);
    image->header.segment_entry_size = read_u16(bytes + 32);
    image->header.segment_count = read_u16(bytes + 34);
    image->header.dynamic_table_offset = read_u64(bytes + 36);
    image->header.dynamic_table_size = read_u64(bytes + 44);
    image->header.metadata_offset = read_u64(bytes + 52);
    image->header.metadata_size = read_u32(bytes + 60);

    if (image->header.version != 1 ||
        image->header.header_size < GOTH95_HEADER_SIZE ||
        image->header.header_size > source->size ||
        image->header.flags != 0 ||
        (image->header.type != GOTH95_TYPE_EXECUTABLE &&
         image->header.type != GOTH95_TYPE_SHARED &&
         image->header.type != GOTH95_TYPE_MODULE) ||
        image->header.segment_count == 0 ||
        image->header.segment_count > GOTH95_MAX_SEGMENTS ||
        image->header.segment_entry_size < GOTH95_SEGMENT_SIZE ||
        image->header.segment_table_offset < image->header.header_size) {
        return GOTH95_BAD_FORMAT;
    }
    if (image->header.machine != GOTH95_MACHINE_I386) {
        return GOTH95_UNSUPPORTED;
    }
    if (image->header.segment_entry_size >
        UINT64_MAX / image->header.segment_count) {
        return GOTH95_BAD_FORMAT;
    }
    table_size = (uint64_t)image->header.segment_entry_size *
                 image->header.segment_count;
    if (!range_within_file(image->header.segment_table_offset,
                           table_size,
                           source->size) ||
        (image->header.dynamic_table_size != 0 &&
         (image->header.dynamic_table_size % GOTH95_DYNAMIC_SIZE != 0 ||
          image->header.dynamic_table_size / GOTH95_DYNAMIC_SIZE >
              GOTH95_MAX_DYNAMIC_ENTRIES ||
          image->header.dynamic_table_offset < image->header.header_size ||
          !range_within_file(image->header.dynamic_table_offset,
                             image->header.dynamic_table_size,
                             source->size))) ||
        (image->header.dynamic_table_size == 0 &&
         image->header.dynamic_table_offset != 0) ||
        !range_within_file(image->header.metadata_offset,
                           image->header.metadata_size,
                           source->size) ||
        (image->header.metadata_size != 0 &&
         image->header.metadata_offset < image->header.header_size) ||
        (image->header.metadata_size == 0 &&
         image->header.metadata_offset != 0) ||
        ranges_overlap(image->header.segment_table_offset, table_size,
                       image->header.dynamic_table_offset,
                       image->header.dynamic_table_size) ||
        ranges_overlap(image->header.segment_table_offset, table_size,
                       image->header.metadata_offset,
                       image->header.metadata_size) ||
        ranges_overlap(image->header.dynamic_table_offset,
                       image->header.dynamic_table_size,
                       image->header.metadata_offset,
                       image->header.metadata_size)) {
        return GOTH95_BAD_FORMAT;
    }

    for (index = 0; index < image->header.segment_count; index++) {
        uint64_t offset = image->header.segment_table_offset +
                          (uint64_t)index *
                              image->header.segment_entry_size;
        uint64_t segment_start;
        uint64_t segment_end;
        size_t other;
        goth95_status_t status = read_segment(source, offset,
                                              &image->segments[index]);
        if (status != GOTH95_OK) {
            return status;
        }
        segment_start = image->segments[index].virtual_address;
        segment_end = segment_start + image->segments[index].memory_size;
        if (segment_start < minimum_address) {
            minimum_address = segment_start;
        }
        if (segment_end > maximum_address) {
            maximum_address = segment_end;
        }
        if ((image->segments[index].permissions & GOTH95_PERM_EXECUTE) != 0 &&
            image->header.entry >= segment_start &&
            image->header.entry < segment_end) {
            executable_entry = true;
        }
        for (other = 0; other < index; other++) {
            uint64_t other_start = image->segments[other].virtual_address;
            uint64_t other_end = other_start +
                                 image->segments[other].memory_size;
            if (segment_start < other_end && other_start < segment_end) {
                return GOTH95_BAD_FORMAT;
            }
        }
    }
    if (!executable_entry &&
        (image->header.type == GOTH95_TYPE_EXECUTABLE ||
         image->header.entry != 0)) {
        return GOTH95_BAD_FORMAT;
    }
    return GOTH95_OK;
}

static bool read_dynamic(const goth95_image_t *image,
                         uint64_t index,
                         uint32_t *tag,
                         uint32_t *flags,
                         uint64_t *value)
{
    uint8_t bytes[GOTH95_DYNAMIC_SIZE];
    uint64_t offset = image->header.dynamic_table_offset +
                      index * GOTH95_DYNAMIC_SIZE;

    if (!source_read(&image->source, offset, bytes, sizeof(bytes))) {
        return false;
    }
    *tag = read_u32(bytes);
    *flags = read_u32(bytes + 4);
    *value = read_u64(bytes + 8);
    return true;
}

static goth95_status_t parse_dynamic(const goth95_image_t *image,
                                     goth95_dynamic_info_t *info)
{
    uint64_t count = image->header.dynamic_table_size /
                     GOTH95_DYNAMIC_SIZE;
    uint64_t index;
    bool terminated = false;

    memset(info, 0, sizeof(*info));
    if (count == 0) {
        return GOTH95_OK;
    }
    for (index = 0; index < count; index++) {
        uint32_t tag;
        uint32_t flags;
        uint64_t value;
        uint32_t *destination = NULL;
        bool *present = NULL;

        if (!read_dynamic(image, index, &tag, &flags, &value)) {
            return GOTH95_IO_ERROR;
        }
        if (tag == GOTH95_DYN_NULL) {
            if (flags != 0 || value != 0 || index + 1 != count) {
                return GOTH95_BAD_FORMAT;
            }
            terminated = true;
            break;
        }
        if (tag == GOTH95_DYN_NEEDED) {
            if (flags != 0 || value > UINT32_MAX) {
                return GOTH95_BAD_FORMAT;
            }
            continue;
        }
        if (tag == GOTH95_DYN_STRTAB) {
            destination = &info->string_table;
            present = &info->has_string_table;
        } else if (tag == GOTH95_DYN_STRSZ) {
            destination = &info->string_size;
            present = &info->has_string_size;
        } else if (tag == GOTH95_DYN_SYMTAB) {
            destination = &info->symbol_table;
            present = &info->has_symbol_table;
        } else if (tag == GOTH95_DYN_SYMENT) {
            destination = &info->symbol_entry_size;
            present = &info->has_symbol_entry_size;
        } else if (tag == GOTH95_DYN_RELA) {
            destination = &info->relocation_table;
            present = &info->has_relocation_table;
        } else if (tag == GOTH95_DYN_RELASZ) {
            destination = &info->relocation_size;
            present = &info->has_relocation_size;
        } else if (tag == GOTH95_DYN_RELAENT) {
            destination = &info->relocation_entry_size;
            present = &info->has_relocation_entry_size;
        } else if (tag == GOTH95_DYN_SONAME) {
            destination = &info->soname_offset;
            present = &info->has_soname;
        } else if (tag == GOTH95_DYN_SYMBOL_COUNT) {
            destination = &info->symbol_count;
            present = &info->has_symbol_count;
        } else if ((flags & GOTH95_DYN_OPTIONAL) == 0 ||
                   (flags & ~GOTH95_DYN_OPTIONAL) != 0) {
            return GOTH95_UNSUPPORTED;
        }
        if (destination != NULL) {
            if ((flags & ~GOTH95_DYN_OPTIONAL) != 0 ||
                value > UINT32_MAX || (present != NULL && *present)) {
                return GOTH95_BAD_FORMAT;
            }
            *destination = (uint32_t)value;
            if (present != NULL) {
                *present = true;
            }
        }
    }
    if (!terminated) {
        return GOTH95_BAD_FORMAT;
    }
    if (info->has_string_table != info->has_string_size ||
        (info->has_string_table && info->string_size == 0) ||
        info->has_symbol_table != info->has_symbol_entry_size ||
        info->has_symbol_table != info->has_symbol_count ||
        (info->has_symbol_table &&
         (!info->has_string_table ||
          info->symbol_entry_size != GOTH95_SYMBOL_SIZE ||
          info->symbol_count == 0 ||
          info->symbol_count > GOTH95_MAX_SYMBOLS)) ||
        info->has_relocation_table != info->has_relocation_size ||
        info->has_relocation_size != info->has_relocation_entry_size ||
        (info->has_relocation_size &&
         (info->relocation_entry_size != GOTH95_RELOCATION_SIZE ||
          info->relocation_size % GOTH95_RELOCATION_SIZE != 0 ||
          info->relocation_size / GOTH95_RELOCATION_SIZE >
              GOTH95_MAX_RELOCATIONS))) {
        return GOTH95_BAD_FORMAT;
    }
    return GOTH95_OK;
}

static bool read_guest(const goth95_loader_ops_t *ops,
                       uint32_t address,
                       void *data,
                       size_t size)
{
    return ops->read(ops->context, address, data, size);
}

static bool guest_string(const goth95_loader_ops_t *ops,
                         const goth95_dynamic_info_t *dynamic,
                         uint32_t offset,
                         char name[GOTH95_MAX_NAME_SIZE])
{
    size_t index;

    if (offset >= dynamic->string_size) {
        return false;
    }
    for (index = 0; index < GOTH95_MAX_NAME_SIZE; index++) {
        uint64_t string_offset = (uint64_t)offset + index;
        uint64_t address = (uint64_t)dynamic->string_table + string_offset;
        if (string_offset >= dynamic->string_size ||
            address > UINT32_MAX ||
            !read_guest(ops, (uint32_t)address, name + index, 1)) {
            return false;
        }
        if (name[index] == '\0') {
            return true;
        }
    }
    return false;
}

static goth95_status_t load_dependencies(
    const goth95_image_t *image,
    const goth95_loader_ops_t *ops,
    const goth95_dynamic_info_t *dynamic,
    size_t *loaded_count)
{
    uint64_t count = image->header.dynamic_table_size /
                     GOTH95_DYNAMIC_SIZE;
    uint64_t index;

    *loaded_count = 0;
    for (index = 0; index < count; index++) {
        uint32_t tag;
        uint32_t flags;
        uint64_t value;
        char name[GOTH95_MAX_NAME_SIZE];

        if (!read_dynamic(image, index, &tag, &flags, &value)) {
            return GOTH95_IO_ERROR;
        }
        if (tag == GOTH95_DYN_NULL) {
            break;
        }
        if (tag != GOTH95_DYN_NEEDED) {
            continue;
        }
        if (!dynamic->has_string_table ||
            !guest_string(ops, dynamic, (uint32_t)value, name) ||
            name[0] == '\0') {
            return GOTH95_BAD_FORMAT;
        }
        if (ops->load_dependency == NULL ||
            ops->unload_dependency == NULL ||
            !ops->load_dependency(ops->context, name)) {
            return GOTH95_LINK_ERROR;
        }
        (*loaded_count)++;
    }
    return GOTH95_OK;
}

static void unload_dependencies(const goth95_image_t *image,
                                const goth95_loader_ops_t *ops,
                                const goth95_dynamic_info_t *dynamic,
                                size_t loaded_count)
{
    uint64_t count = image->header.dynamic_table_size /
                     GOTH95_DYNAMIC_SIZE;
    uint64_t index;
    size_t unloaded = 0;

    if (ops->unload_dependency == NULL || dynamic == NULL) {
        return;
    }
    for (index = 0; index < count && unloaded < loaded_count; index++) {
        uint32_t tag;
        uint32_t flags;
        uint64_t value;
        char name[GOTH95_MAX_NAME_SIZE];

        if (!read_dynamic(image, index, &tag, &flags, &value) ||
            tag == GOTH95_DYN_NULL) {
            break;
        }
        if (tag != GOTH95_DYN_NEEDED ||
            !guest_string(ops, dynamic, (uint32_t)value, name)) {
            continue;
        }
        ops->unload_dependency(ops->context, name);
        unloaded++;
    }
}

static bool read_symbol(const goth95_loader_ops_t *ops,
                        const goth95_dynamic_info_t *dynamic,
                        uint32_t index,
                        uint32_t *name_offset,
                        uint32_t *value,
                        uint32_t *size,
                        uint8_t *info,
                        uint16_t *section_index,
                        uint16_t *version)
{
    uint8_t bytes[GOTH95_SYMBOL_SIZE];
    uint64_t address = (uint64_t)dynamic->symbol_table +
                       (uint64_t)index * GOTH95_SYMBOL_SIZE;

    if (index >= dynamic->symbol_count ||
        address + sizeof(bytes) > (uint64_t)UINT32_MAX + 1 ||
        !read_guest(ops, (uint32_t)address, bytes, sizeof(bytes))) {
        return false;
    }
    *name_offset = read_u32(bytes);
    *value = read_u32(bytes + 4);
    *size = read_u32(bytes + 8);
    *info = bytes[12];
    if (bytes[13] != 0) {
        return false;
    }
    *section_index = read_u16(bytes + 14);
    *version = read_u16(bytes + 16);
    return read_u16(bytes + 18) == 0;
}

static bool symbol_address(const goth95_loader_ops_t *ops,
                           const goth95_dynamic_info_t *dynamic,
                           uint32_t symbol_index,
                           uint32_t load_bias,
                           uint32_t *address)
{
    uint32_t name_offset;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint16_t section_index;
    uint16_t version;
    uint8_t binding;
    char name[GOTH95_MAX_NAME_SIZE];
    uint64_t relocated;

    if (!read_symbol(ops, dynamic, symbol_index, &name_offset, &value,
                     &size, &info, &section_index, &version)) {
        return false;
    }
    if (section_index != GOTH95_LZ_SYMBOL_UNDEFINED) {
        relocated = value;
        if (section_index != GOTH95_LZ_SYMBOL_ABSOLUTE) {
            relocated += load_bias;
        }
        if (relocated > UINT32_MAX) {
            return false;
        }
        *address = (uint32_t)relocated;
        return true;
    }
    if (name_offset == 0 ||
        !guest_string(ops, dynamic, name_offset, name) ||
        name[0] == '\0') {
        return false;
    }
    if (ops->resolve_symbol != NULL &&
        ops->resolve_symbol(ops->context, name, version, address)) {
        return true;
    }
    binding = (uint8_t)(info >> 4);
    if (binding == GOTH95_BIND_WEAK) {
        *address = 0;
        return true;
    }
    return false;
}

static bool address_in_writable_segment(const goth95_image_t *image,
                                        const goth95_module_t *module,
                                        uint32_t address,
                                        size_t size)
{
    size_t index;
    uint64_t end = (uint64_t)address + size;

    if (end > UINT32_MAX) {
        return false;
    }
    for (index = 0; index < image->header.segment_count; index++) {
        const goth95_segment_t *segment = &image->segments[index];
        uint64_t start = (uint64_t)segment->virtual_address +
                         module->load_bias;
        uint64_t segment_end = start + segment->memory_size;
        if ((segment->permissions & GOTH95_PERM_WRITE) != 0 &&
            address >= start && end <= segment_end) {
            return true;
        }
    }
    return false;
}

static goth95_status_t apply_relocations(
    const goth95_loader_ops_t *ops,
    const goth95_dynamic_info_t *dynamic,
    uint32_t load_bias,
    const goth95_image_t *image,
    const goth95_module_t *module)
{
    uint32_t offset;

    if (!dynamic->has_relocation_size || dynamic->relocation_size == 0) {
        return GOTH95_OK;
    }
    for (offset = 0; offset < dynamic->relocation_size;
         offset += GOTH95_RELOCATION_SIZE) {
        uint8_t bytes[GOTH95_RELOCATION_SIZE];
        uint64_t relocation_address =
            (uint64_t)dynamic->relocation_table + offset;
        uint32_t target;
        uint32_t relocation_info;
        uint32_t symbol_index;
        uint32_t relocation_type;
        uint32_t addend;
        uint32_t symbol = 0;
        uint32_t result;
        uint64_t relocated_target;

        if (relocation_address + sizeof(bytes) >
                (uint64_t)UINT32_MAX + 1 ||
            !read_guest(ops, (uint32_t)relocation_address,
                        bytes, sizeof(bytes))) {
            return GOTH95_MEMORY_ERROR;
        }
        target = read_u32(bytes);
        relocation_info = read_u32(bytes + 4);
        symbol_index = relocation_info >> 8;
        relocation_type = relocation_info & 0xffu;
        addend = read_u32(bytes + 8);
        if (relocation_type == GOTH95_I386_R_NONE) {
            continue;
        }
        relocated_target = (uint64_t)target + load_bias;
        if (relocated_target > UINT32_MAX ||
            !address_in_writable_segment(image, module,
                                         (uint32_t)relocated_target, 4)) {
            return GOTH95_MEMORY_ERROR;
        }
        if (relocation_type == GOTH95_I386_R_RELATIVE &&
            symbol_index != 0) {
            return GOTH95_BAD_FORMAT;
        }
        if (relocation_type != GOTH95_I386_R_RELATIVE &&
            symbol_index != 0 &&
            !symbol_address(ops, dynamic, symbol_index,
                            load_bias, &symbol)) {
            return GOTH95_LINK_ERROR;
        }
        switch (relocation_type) {
        case GOTH95_I386_R_32:
            result = symbol + addend;
            break;
        case GOTH95_I386_R_PC32:
            result = symbol + addend - (uint32_t)relocated_target;
            break;
        case GOTH95_I386_R_GLOB_DAT:
        case GOTH95_I386_R_JMP_SLOT:
            result = symbol;
            break;
        case GOTH95_I386_R_RELATIVE:
            result = load_bias + addend;
            break;
        default:
            return GOTH95_UNSUPPORTED;
        }
        {
            uint8_t word[4];
            write_u32(word, result);
            if (!ops->write(ops->context, (uint32_t)relocated_target,
                            word, sizeof(word))) {
                return GOTH95_MEMORY_ERROR;
            }
        }
    }
    return GOTH95_OK;
}

static bool relocate_dynamic_addresses(goth95_dynamic_info_t *dynamic,
                                      uint32_t load_bias)
{
    uint64_t address;

    if (dynamic->has_string_table) {
        address = (uint64_t)dynamic->string_table + load_bias;
        if (address > UINT32_MAX) {
            return false;
        }
        dynamic->string_table = (uint32_t)address;
    }
    if (dynamic->has_symbol_table) {
        address = (uint64_t)dynamic->symbol_table + load_bias;
        if (address > UINT32_MAX) {
            return false;
        }
        dynamic->symbol_table = (uint32_t)address;
    }
    if (dynamic->has_relocation_table) {
        address = (uint64_t)dynamic->relocation_table + load_bias;
        if (address > UINT32_MAX) {
            return false;
        }
        dynamic->relocation_table = (uint32_t)address;
    }
    return true;
}

static goth95_status_t publish_exports(
    const goth95_loader_ops_t *ops,
    const goth95_dynamic_info_t *dynamic,
    const goth95_module_t *module,
    const char *module_name)
{
    uint32_t index;

    if (ops->publish_symbol == NULL || dynamic->symbol_count == 0) {
        return GOTH95_OK;
    }
    for (index = 0; index < dynamic->symbol_count; index++) {
        uint32_t name_offset;
        uint32_t value;
        uint32_t size;
        uint8_t info;
        uint16_t section_index;
        uint16_t version;
        uint8_t binding;
        uint64_t address;
        char name[GOTH95_MAX_NAME_SIZE];

        if (!read_symbol(ops, dynamic, index, &name_offset, &value, &size,
                         &info, &section_index, &version)) {
            return GOTH95_MEMORY_ERROR;
        }
        binding = (uint8_t)(info >> 4);
        if (section_index == GOTH95_LZ_SYMBOL_UNDEFINED ||
            (binding != GOTH95_BIND_GLOBAL && binding != GOTH95_BIND_WEAK) ||
            name_offset == 0) {
            continue;
        }
        address = value;
        if (section_index != GOTH95_LZ_SYMBOL_ABSOLUTE) {
            address += module->load_bias;
        }
        if (address > UINT32_MAX ||
            !guest_string(ops, dynamic, name_offset, name) ||
            name[0] == '\0' ||
            !ops->publish_symbol(ops->context, module_name, name, version,
                                 (uint32_t)address, size)) {
            return GOTH95_LINK_ERROR;
        }
    }
    return GOTH95_OK;
}

static goth95_status_t load_segment(const goth95_image_t *image,
                                    const goth95_loader_ops_t *ops,
                                    const goth95_segment_t *segment,
                                    uint32_t load_bias)
{
    uint64_t address = (uint64_t)segment->virtual_address + load_bias;

    if (address + segment->memory_size > UINT32_MAX) {
        return GOTH95_BAD_FORMAT;
    }
    if (segment->compression == GOTH95_COMPRESSION_NONE) {
        uint8_t buffer[512];
        uint32_t offset = 0;
        while (offset < segment->file_size) {
            size_t amount = segment->file_size - offset;
            if (amount > sizeof(buffer)) {
                amount = sizeof(buffer);
            }
            if (!source_read(&image->source,
                             segment->file_offset + offset,
                             buffer, amount)) {
                return GOTH95_IO_ERROR;
            }
            if (!ops->write(ops->context, (uint32_t)address + offset,
                            buffer, amount)) {
                return GOTH95_MEMORY_ERROR;
            }
            offset += (uint32_t)amount;
        }
        if (segment->memory_size > segment->file_size &&
            !ops->zero(ops->context,
                       (uint32_t)address + segment->file_size,
                       segment->memory_size - segment->file_size)) {
            return GOTH95_MEMORY_ERROR;
        }
        return GOTH95_OK;
    }
    if (ops->allocate_temporary == NULL ||
        ops->free_temporary == NULL || ops->decompress == NULL) {
        return GOTH95_UNSUPPORTED;
    }
    {
        void *compressed = ops->allocate_temporary(
            ops->context, segment->file_size);
        void *uncompressed;
        size_t output_size = 0;
        bool success;

        if (compressed == NULL) {
            return GOTH95_NO_MEMORY;
        }
        uncompressed = ops->allocate_temporary(
            ops->context, segment->memory_size);
        if (uncompressed == NULL) {
            ops->free_temporary(ops->context, compressed);
            return GOTH95_NO_MEMORY;
        }
        success = source_read(&image->source, segment->file_offset,
                              compressed, segment->file_size);
        if (!success) {
            ops->free_temporary(ops->context, uncompressed);
            ops->free_temporary(ops->context, compressed);
            return GOTH95_IO_ERROR;
        }
        success = ops->decompress(ops->context, segment->compression,
                                  compressed, segment->file_size,
                                  uncompressed, segment->memory_size,
                                  &output_size);
        if (!success || output_size != segment->memory_size) {
            ops->free_temporary(ops->context, uncompressed);
            ops->free_temporary(ops->context, compressed);
            return GOTH95_BAD_FORMAT;
        }
        success = ops->write(ops->context, (uint32_t)address,
                             uncompressed, output_size);
        ops->free_temporary(ops->context, uncompressed);
        ops->free_temporary(ops->context, compressed);
        return success ? GOTH95_OK : GOTH95_MEMORY_ERROR;
    }
}

goth95_status_t goth95_load(const goth95_image_t *image,
                            const goth95_loader_ops_t *operations,
                            goth95_module_t *module)
{
    goth95_dynamic_info_t dynamic;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0;
    uint32_t alignment = 1;
    uint32_t load_bias = 0;
    size_t loaded_dependencies = 0;
    size_t index;
    goth95_status_t status;
    bool reserved = false;
    bool exports_attempted = false;
    char module_name[GOTH95_MAX_NAME_SIZE] = "";

    if (image == NULL || operations == NULL || module == NULL ||
        operations->reserve == NULL || operations->write == NULL ||
        operations->read == NULL || operations->zero == NULL ||
        operations->protect == NULL || operations->release == NULL) {
        return GOTH95_INVALID_ARGUMENT;
    }
    memset(module, 0, sizeof(*module));
    status = parse_dynamic(image, &dynamic);
    if (status != GOTH95_OK) {
        return status;
    }
    if (dynamic.symbol_count != 0 &&
        operations->publish_symbol != NULL &&
        operations->unpublish_module == NULL) {
        return GOTH95_INVALID_ARGUMENT;
    }
    if (dynamic.symbol_count != 0 &&
        operations->publish_symbol != NULL &&
        !dynamic.has_soname) {
        return GOTH95_BAD_FORMAT;
    }
    for (index = 0; index < image->header.segment_count; index++) {
        const goth95_segment_t *segment = &image->segments[index];
        uint64_t end = (uint64_t)segment->virtual_address +
                       segment->memory_size;
        if (segment->virtual_address < minimum) {
            minimum = segment->virtual_address;
        }
        if (end > maximum) {
            maximum = (uint32_t)end;
        }
        if (segment->alignment > alignment) {
            alignment = segment->alignment;
        }
    }
    if (minimum >= maximum ||
        !operations->reserve(operations->context, minimum, maximum,
                             alignment,
                             image->header.type == GOTH95_TYPE_EXECUTABLE
                                 ? 0
                                 : UINT32_MAX,
                             &load_bias)) {
        return GOTH95_MEMORY_ERROR;
    }
    reserved = true;
    if ((uint64_t)maximum + load_bias > UINT32_MAX ||
        (load_bias & (alignment - 1u)) != 0 ||
        (image->header.type == GOTH95_TYPE_EXECUTABLE && load_bias != 0)) {
        status = GOTH95_MEMORY_ERROR;
        goto failed;
    }
    module->load_bias = load_bias;
    module->minimum_address = minimum;
    module->maximum_address = maximum;
    module->type = image->header.type;
    if (image->header.entry == 0 &&
        image->header.type != GOTH95_TYPE_EXECUTABLE) {
        module->entry = 0;
    } else if ((uint64_t)image->header.entry + load_bias > UINT32_MAX) {
        status = GOTH95_BAD_FORMAT;
        goto failed;
    } else {
        module->entry = image->header.entry + load_bias;
    }

    for (index = 0; index < image->header.segment_count; index++) {
        status = load_segment(image, operations,
                              &image->segments[index], load_bias);
        if (status != GOTH95_OK) {
            goto failed;
        }
    }
    if (!relocate_dynamic_addresses(&dynamic, load_bias)) {
        status = GOTH95_BAD_FORMAT;
        goto failed;
    }
    if (image->header.dynamic_table_size != 0) {
        uint64_t count = image->header.dynamic_table_size /
                         GOTH95_DYNAMIC_SIZE;
        uint64_t dynamic_index;
        for (dynamic_index = 0; dynamic_index < count; dynamic_index++) {
            uint32_t tag;
            uint32_t flags;
            uint64_t value;
            if (!read_dynamic(image, dynamic_index, &tag, &flags, &value)) {
                status = GOTH95_IO_ERROR;
                goto failed;
            }
            if (tag == GOTH95_DYN_NULL) {
                break;
            }
            if (tag == GOTH95_DYN_SONAME) {
                if (!guest_string(operations, &dynamic, (uint32_t)value,
                                  module_name) ||
                    module_name[0] == '\0') {
                    status = GOTH95_BAD_FORMAT;
                    goto failed;
                }
                break;
            }
        }
        status = load_dependencies(image, operations, &dynamic,
                                   &loaded_dependencies);
        if (status != GOTH95_OK) {
            goto failed;
        }
    }
    status = apply_relocations(operations, &dynamic, load_bias, image, module);
    if (status != GOTH95_OK) {
        goto failed;
    }
    exports_attempted = operations->publish_symbol != NULL &&
                        dynamic.symbol_count != 0;
    status = publish_exports(operations, &dynamic, module, module_name);
    if (status != GOTH95_OK) {
        goto failed;
    }
    module->dependencies_loaded = loaded_dependencies != 0;
    module->symbols_published = exports_attempted;
    for (index = 0; index < image->header.segment_count; index++) {
        const goth95_segment_t *segment = &image->segments[index];
        uint64_t address = (uint64_t)segment->virtual_address + load_bias;
        if (!operations->protect(operations->context, (uint32_t)address,
                                 segment->memory_size,
                                 segment->permissions)) {
            status = GOTH95_MEMORY_ERROR;
            goto failed;
        }
    }
    module->loaded = true;
    return GOTH95_OK;

failed:
    if (exports_attempted && operations->unpublish_module != NULL) {
        operations->unpublish_module(operations->context, module_name);
    }
    if (loaded_dependencies != 0) {
        unload_dependencies(image, operations, &dynamic,
                            loaded_dependencies);
    }
    if (reserved) {
        operations->release(operations->context, minimum, maximum,
                            load_bias);
    }
    memset(module, 0, sizeof(*module));
    return status;
}

goth95_status_t goth95_unload(const goth95_image_t *image,
                              const goth95_loader_ops_t *operations,
                              goth95_module_t *module)
{
    goth95_dynamic_info_t dynamic;
    goth95_status_t status;
    char module_name[GOTH95_MAX_NAME_SIZE] = "";
    uint64_t count;
    uint64_t index;

    if (image == NULL || operations == NULL || module == NULL ||
        !module->loaded || operations->release == NULL ||
        operations->read == NULL || module->type != image->header.type ||
        (module->dependencies_loaded &&
         operations->unload_dependency == NULL) ||
        (module->symbols_published &&
         operations->unpublish_module == NULL)) {
        return GOTH95_INVALID_ARGUMENT;
    }
    status = parse_dynamic(image, &dynamic);
    if (status != GOTH95_OK) {
        return status;
    }
    if (!relocate_dynamic_addresses(&dynamic, module->load_bias)) {
        return GOTH95_BAD_FORMAT;
    }
    count = image->header.dynamic_table_size / GOTH95_DYNAMIC_SIZE;
    for (index = 0; index < count; index++) {
        uint32_t tag;
        uint32_t flags;
        uint64_t value;
        if (!read_dynamic(image, index, &tag, &flags, &value)) {
            return GOTH95_IO_ERROR;
        }
        if (tag == GOTH95_DYN_NULL) {
            break;
        }
        if (tag == GOTH95_DYN_SONAME) {
            if (!guest_string(operations, &dynamic, (uint32_t)value,
                              module_name) ||
                module_name[0] == '\0') {
                return GOTH95_MEMORY_ERROR;
            }
            break;
        }
    }
    if (module->dependencies_loaded) {
        unload_dependencies(image, operations, &dynamic, SIZE_MAX);
    }
    if (module->symbols_published) {
        operations->unpublish_module(operations->context, module_name);
    }
    operations->release(operations->context, module->minimum_address,
                        module->maximum_address, module->load_bias);
    memset(module, 0, sizeof(*module));
    return GOTH95_OK;
}

typedef struct {
    uint32_t file_offset;
    uint32_t file_size;
} goth95_elf32_segment_layout_t;

typedef struct {
    uint32_t target;
    uint32_t addend;
    uint32_t segment_index;
} goth95_elf32_patch_t;

enum {
    GOTH95_ELF32_HEADER_SIZE = 52,
    GOTH95_ELF32_PROGRAM_HEADER_SIZE = 32,
    GOTH95_ELF32_PT_LOAD = 1,
    GOTH95_ELF32_PT_DYNAMIC = 2,
    GOTH95_ELF32_ET_EXEC = 2,
    GOTH95_ELF32_ET_DYN = 3,
    GOTH95_ELF32_PF_X = 1,
    GOTH95_ELF32_PF_W = 2,
    GOTH95_ELF32_PF_R = 4,
    GOTH95_ELF32_PAGE_SIZE = 4096,
    GOTH95_ELF32_DT_NULL = 0,
    GOTH95_ELF32_DT_NEEDED = 1,
    GOTH95_ELF32_DT_HASH = 4,
    GOTH95_ELF32_DT_STRTAB = 5,
    GOTH95_ELF32_DT_SYMTAB = 6,
    GOTH95_ELF32_DT_STRSZ = 10,
    GOTH95_ELF32_DT_SYMENT = 11,
    GOTH95_ELF32_DT_SONAME = 14,
    GOTH95_ELF32_DT_REL = 17,
    GOTH95_ELF32_DT_RELSZ = 18,
    GOTH95_ELF32_DT_RELENT = 19
};

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint64_t align_congruent(uint64_t value,
                                uint32_t alignment,
                                uint32_t residue)
{
    uint32_t current_residue =
        (uint32_t)(value & (alignment - 1u));
    uint32_t adjustment = (residue - current_residue) &
                          (alignment - 1u);
    return value + adjustment;
}

static void *export_allocate(const goth95_loader_ops_t *ops, size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return ops->allocate_temporary(ops->context, size);
}

static goth95_status_t read_image_virtual(const goth95_image_t *image,
                                          const goth95_loader_ops_t *ops,
                                          uint32_t address,
                                          void *buffer,
                                          size_t size)
{
    size_t index;
    uint64_t end = (uint64_t)address + size;

    if (end > (uint64_t)UINT32_MAX + 1) {
        return GOTH95_BAD_FORMAT;
    }
    for (index = 0; index < image->header.segment_count; index++) {
        const goth95_segment_t *segment = &image->segments[index];
        uint64_t segment_end = (uint64_t)segment->virtual_address +
                               segment->memory_size;
        uint64_t offset;

        if (address < segment->virtual_address || end > segment_end) {
            continue;
        }
        offset = (uint64_t)address - segment->virtual_address;
        if (segment->compression == GOTH95_COMPRESSION_NONE) {
            uint8_t *output = buffer;
            size_t available = 0;
            if (offset < segment->file_size) {
                available = segment->file_size - (size_t)offset;
                if (available > size) {
                    available = size;
                }
                if (!source_read(&image->source,
                                 segment->file_offset + offset,
                                 output, available)) {
                    return GOTH95_IO_ERROR;
                }
            }
            if (available < size) {
                memset(output + available, 0, size - available);
            }
            return GOTH95_OK;
        }
        if (ops->decompress == NULL) {
            return GOTH95_UNSUPPORTED;
        }
#if SIZE_MAX < UINT32_MAX
        if (segment->memory_size > SIZE_MAX) {
            return GOTH95_UNSUPPORTED;
        }
#endif
        {
            void *compressed = export_allocate(ops, segment->file_size);
            void *uncompressed;
            size_t output_size = 0;
            bool success;

            if (compressed == NULL) {
                return GOTH95_NO_MEMORY;
            }
            uncompressed = export_allocate(ops, segment->memory_size);
            if (uncompressed == NULL) {
                ops->free_temporary(ops->context, compressed);
                return GOTH95_NO_MEMORY;
            }
            if (!source_read(&image->source, segment->file_offset,
                             compressed, segment->file_size)) {
                ops->free_temporary(ops->context, uncompressed);
                ops->free_temporary(ops->context, compressed);
                return GOTH95_IO_ERROR;
            }
            success = ops->decompress(ops->context, segment->compression,
                                      compressed, segment->file_size,
                                      uncompressed, segment->memory_size,
                                      &output_size);
            if (!success || output_size != segment->memory_size) {
                ops->free_temporary(ops->context, uncompressed);
                ops->free_temporary(ops->context, compressed);
                return GOTH95_BAD_FORMAT;
            }
            memcpy(buffer, (const uint8_t *)uncompressed + (size_t)offset,
                   size);
            ops->free_temporary(ops->context, uncompressed);
            ops->free_temporary(ops->context, compressed);
            return GOTH95_OK;
        }
    }
    return GOTH95_BAD_FORMAT;
}

static bool find_writable_segment(const goth95_image_t *image,
                                  uint32_t address,
                                  uint32_t size,
                                  uint32_t *segment_index)
{
    size_t index;
    uint64_t end = (uint64_t)address + size;

    for (index = 0; index < image->header.segment_count; index++) {
        const goth95_segment_t *segment = &image->segments[index];
        uint64_t segment_end = (uint64_t)segment->virtual_address +
                               segment->memory_size;
        if ((segment->permissions & GOTH95_PERM_WRITE) != 0 &&
            address >= segment->virtual_address && end <= segment_end) {
            *segment_index = (uint32_t)index;
            return true;
        }
    }
    return false;
}

static bool string_at(const uint8_t *strings,
                      uint32_t string_size,
                      uint32_t offset,
                      const char **name)
{
    uint32_t index;

    if (offset >= string_size) {
        return false;
    }
    for (index = offset; index < string_size; index++) {
        if (strings[index] == '\0') {
            *name = (const char *)(strings + offset);
            return true;
        }
        if (index - offset + 1 >= GOTH95_MAX_NAME_SIZE) {
            return false;
        }
    }
    return false;
}

static bool sink_write(const goth95_sink_t *sink,
                       uint64_t offset,
                       const void *buffer,
                       size_t size)
{
    return offset <= sink->capacity &&
           size <= sink->capacity - offset &&
           sink->write_at(sink->context, offset, buffer, size);
}

static bool sink_zero(const goth95_sink_t *sink,
                      uint64_t start,
                      uint64_t end)
{
    uint8_t zeros[4096] = {0};

    while (start < end) {
        size_t amount = (size_t)(end - start);
        if (amount > sizeof(zeros)) {
            amount = sizeof(zeros);
        }
        if (!sink_write(sink, start, zeros, amount)) {
            return false;
        }
        start += amount;
    }
    return true;
}

static void write_elf32_dynamic(uint8_t *buffer,
                                size_t *offset,
                                uint32_t tag,
                                uint32_t value)
{
    write_u32(buffer + *offset, tag);
    write_u32(buffer + *offset + 4, value);
    *offset += 8;
}

static void write_elf32_program_header(uint8_t *buffer,
                                      uint32_t type,
                                      uint32_t file_offset,
                                      uint32_t virtual_address,
                                      uint32_t file_size,
                                      uint32_t memory_size,
                                      uint32_t flags,
                                      uint32_t alignment)
{
    write_u32(buffer, type);
    write_u32(buffer + 4, file_offset);
    write_u32(buffer + 8, virtual_address);
    write_u32(buffer + 12, virtual_address);
    write_u32(buffer + 16, file_size);
    write_u32(buffer + 20, memory_size);
    write_u32(buffer + 24, flags);
    write_u32(buffer + 28, alignment);
}

static goth95_status_t write_elf32_load_segment(
    const goth95_image_t *image,
    const goth95_loader_ops_t *ops,
    const goth95_sink_t *sink,
    uint32_t segment_index,
    const goth95_elf32_segment_layout_t *layout,
    const goth95_elf32_patch_t *patches,
    size_t patch_count)
{
    const goth95_segment_t *segment = &image->segments[segment_index];
    uint8_t *contents;
    size_t index;
    goth95_status_t status = GOTH95_OK;

    if (layout->file_size == 0) {
        return GOTH95_OK;
    }
    contents = export_allocate(ops, layout->file_size);
    if (contents == NULL) {
        return GOTH95_NO_MEMORY;
    }
    if (segment->compression == GOTH95_COMPRESSION_NONE) {
        memset(contents, 0, layout->file_size);
        if (segment->file_size != 0 &&
            !source_read(&image->source, segment->file_offset,
                         contents, segment->file_size)) {
            status = GOTH95_IO_ERROR;
            goto done;
        }
    } else {
        void *compressed = export_allocate(ops, segment->file_size);
        size_t output_size = 0;
        bool success;

        if (compressed == NULL) {
            status = GOTH95_NO_MEMORY;
            goto done;
        }
        if (!source_read(&image->source, segment->file_offset,
                         compressed, segment->file_size)) {
            ops->free_temporary(ops->context, compressed);
            status = GOTH95_IO_ERROR;
            goto done;
        }
        success = ops->decompress(ops->context, segment->compression,
                                  compressed, segment->file_size,
                                  contents, layout->file_size,
                                  &output_size);
        ops->free_temporary(ops->context, compressed);
        if (!success || output_size != segment->memory_size ||
            layout->file_size != segment->memory_size) {
            status = GOTH95_BAD_FORMAT;
            goto done;
        }
    }

    for (index = 0; index < patch_count; index++) {
        uint32_t patch_offset;
        if (patches[index].segment_index != segment_index) {
            continue;
        }
        patch_offset = patches[index].target - segment->virtual_address;
        if ((uint64_t)patch_offset + 4 > layout->file_size) {
            status = GOTH95_BAD_FORMAT;
            goto done;
        }
        write_u32(contents + patch_offset, patches[index].addend);
    }
    if (!sink_write(sink, layout->file_offset,
                    contents, layout->file_size)) {
        status = GOTH95_IO_ERROR;
    }

done:
    ops->free_temporary(ops->context, contents);
    return status;
}

goth95_status_t goth95_export_elf32(
    const goth95_image_t *image,
    const goth95_sink_t *sink,
    const goth95_loader_ops_t *codec_operations,
    uint64_t *output_size)
{
    goth95_dynamic_info_t dynamic;
    goth95_elf32_segment_layout_t layouts[GOTH95_MAX_SEGMENTS] = {{0}};
    goth95_elf32_patch_t *patches = NULL;
    uint8_t *source_symbols = NULL;
    uint8_t *strings = NULL;
    uint8_t *source_relocations = NULL;
    uint8_t *extension = NULL;
    uint8_t elf_header_and_program_headers[
        GOTH95_ELF32_HEADER_SIZE +
        (GOTH95_MAX_SEGMENTS + 2) * GOTH95_ELF32_PROGRAM_HEADER_SIZE] = {0};
    uint32_t output_file_sizes[GOTH95_MAX_SEGMENTS] = {0};
    uint32_t segment_order[GOTH95_MAX_SEGMENTS] = {0};
    uint32_t symbol_table_offset = 0;
    uint32_t hash_table_offset = 0;
    uint32_t relocation_table_offset = 0;
    uint32_t dynamic_table_offset = 0;
    uint32_t extension_size = 0;
    uint32_t extension_address = 0;
    uint32_t extension_file_offset = 0;
    uint32_t dynamic_entry_count = 0;
    uint32_t needed_count = 0;
    uint32_t relocation_count = 0;
    size_t patch_count = 0;
    size_t index;
    uint64_t working_size;
    uint64_t output_cursor;
    uint64_t output_end;
    size_t program_header_count;
    size_t program_header_offset;
    size_t dynamic_output_offset;
    goth95_status_t status;
    bool has_dynamic;

    if (output_size != NULL) {
        *output_size = 0;
    }
    if (image == NULL || sink == NULL || sink->write_at == NULL ||
        codec_operations == NULL ||
        codec_operations->allocate_temporary == NULL ||
        codec_operations->free_temporary == NULL ||
        output_size == NULL) {
        return GOTH95_INVALID_ARGUMENT;
    }
    for (index = 0; index < image->header.segment_count; index++) {
        if (image->segments[index].compression != GOTH95_COMPRESSION_NONE &&
            codec_operations->decompress == NULL) {
            return GOTH95_UNSUPPORTED;
        }
        segment_order[index] = (uint32_t)index;
    }
    for (index = 1; index < image->header.segment_count; index++) {
        uint32_t current = segment_order[index];
        size_t position = index;
        while (position > 0 &&
               image->segments[segment_order[position - 1]].virtual_address >
                   image->segments[current].virtual_address) {
            segment_order[position] = segment_order[position - 1];
            position--;
        }
        segment_order[position] = current;
    }

    status = parse_dynamic(image, &dynamic);
    if (status != GOTH95_OK) {
        return status;
    }
    has_dynamic = image->header.dynamic_table_size != 0;
    if (dynamic.has_string_table) {
        strings = export_allocate(codec_operations, dynamic.string_size);
        if (strings == NULL) {
            return GOTH95_NO_MEMORY;
        }
        status = read_image_virtual(image, codec_operations,
                                    dynamic.string_table, strings,
                                    dynamic.string_size);
        if (status != GOTH95_OK) {
            goto done;
        }
    }

    for (index = 0;
         index < image->header.dynamic_table_size / GOTH95_DYNAMIC_SIZE;
         index++) {
        uint32_t tag;
        uint32_t flags;
        uint64_t value;
        const char *name;

        if (!read_dynamic(image, index, &tag, &flags, &value)) {
            status = GOTH95_IO_ERROR;
            goto done;
        }
        if (tag == GOTH95_DYN_NULL) {
            break;
        }
        if (tag == GOTH95_DYN_NEEDED || tag == GOTH95_DYN_SONAME) {
            if (!dynamic.has_string_table ||
                !string_at(strings, dynamic.string_size,
                           (uint32_t)value, &name) ||
                name[0] == '\0') {
                status = GOTH95_BAD_FORMAT;
                goto done;
            }
            if (tag == GOTH95_DYN_NEEDED) {
                needed_count++;
            }
        }
    }

    if (dynamic.has_symbol_table) {
        uint64_t bytes = (uint64_t)dynamic.symbol_count *
                         GOTH95_SYMBOL_SIZE;
        uint32_t symbol_index;

        if (bytes > SIZE_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        source_symbols = export_allocate(codec_operations, (size_t)bytes);
        if (source_symbols == NULL) {
            status = GOTH95_NO_MEMORY;
            goto done;
        }
        status = read_image_virtual(image, codec_operations,
                                    dynamic.symbol_table,
                                    source_symbols, (size_t)bytes);
        if (status != GOTH95_OK) {
            goto done;
        }
        for (symbol_index = 0; symbol_index < dynamic.symbol_count;
             symbol_index++) {
            const uint8_t *symbol =
                source_symbols + (size_t)symbol_index * GOTH95_SYMBOL_SIZE;
            uint32_t name_offset = read_u32(symbol);
            const char *name;

            if (symbol[13] != 0 || read_u16(symbol + 16) != 0 ||
                read_u16(symbol + 18) != 0 ||
                !string_at(strings, dynamic.string_size,
                           name_offset, &name) ||
                (symbol_index != 0 && name[0] == '\0')) {
                status = read_u16(symbol + 16) != 0
                             ? GOTH95_UNSUPPORTED
                             : GOTH95_BAD_FORMAT;
                goto done;
            }
        }
    }

    if (dynamic.has_relocation_size) {
        uint64_t bytes = dynamic.relocation_size;
        uint32_t relocation_index;

        relocation_count = dynamic.relocation_size /
                           GOTH95_RELOCATION_SIZE;
        if (bytes > SIZE_MAX ||
            (uint64_t)relocation_count * sizeof(*patches) > SIZE_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        if (bytes != 0) {
            source_relocations =
                export_allocate(codec_operations, (size_t)bytes);
            patches = export_allocate(
                codec_operations,
                (size_t)relocation_count * sizeof(*patches));
            if (source_relocations == NULL || patches == NULL) {
                status = GOTH95_NO_MEMORY;
                goto done;
            }
            status = read_image_virtual(image, codec_operations,
                                        dynamic.relocation_table,
                                        source_relocations, (size_t)bytes);
            if (status != GOTH95_OK) {
                goto done;
            }
            for (relocation_index = 0;
                 relocation_index < relocation_count;
                 relocation_index++) {
                const uint8_t *relocation =
                    source_relocations +
                    (size_t)relocation_index * GOTH95_RELOCATION_SIZE;
                uint32_t target = read_u32(relocation);
                uint32_t info = read_u32(relocation + 4);
                uint32_t symbol_index = info >> 8;
                uint32_t type = info & 0xffu;
                uint32_t segment_index;

                if (type == GOTH95_I386_R_NONE) {
                    continue;
                }
                if (type != GOTH95_I386_R_32 &&
                    type != GOTH95_I386_R_PC32 &&
                    type != GOTH95_I386_R_GLOB_DAT &&
                    type != GOTH95_I386_R_JMP_SLOT &&
                    type != GOTH95_I386_R_RELATIVE) {
                    status = GOTH95_UNSUPPORTED;
                    goto done;
                }
                if ((type == GOTH95_I386_R_RELATIVE && symbol_index != 0) ||
                    (symbol_index != 0 &&
                     (!dynamic.has_symbol_table ||
                      symbol_index >= dynamic.symbol_count)) ||
                    !find_writable_segment(image, target, 4,
                                           &segment_index)) {
                    status = GOTH95_BAD_FORMAT;
                    goto done;
                }
                patches[patch_count].target = target;
                patches[patch_count].addend =
                    type == GOTH95_I386_R_GLOB_DAT ||
                            type == GOTH95_I386_R_JMP_SLOT
                        ? 0
                        : read_u32(relocation + 8);
                patches[patch_count].segment_index = segment_index;
                patch_count++;
                if (image->segments[segment_index].compression ==
                    GOTH95_COMPRESSION_NONE) {
                    uint32_t required_file_size =
                        target - image->segments[segment_index].virtual_address +
                        4;
                    if (required_file_size >
                        image->segments[segment_index].memory_size) {
                        status = GOTH95_BAD_FORMAT;
                        goto done;
                    }
                    if (required_file_size >
                        output_file_sizes[segment_index]) {
                        output_file_sizes[segment_index] =
                            required_file_size;
                    }
                }
            }
        }
    }

    if (has_dynamic) {
        uint64_t extension_cursor = 0;
        uint64_t hash_size = 0;
        uint64_t symbol_bytes = 0;
        uint64_t relocation_bytes = (uint64_t)relocation_count * 8;
        uint32_t dynamic_index;
        uint64_t maximum_address = 0;

        dynamic_entry_count = 1 + needed_count +
                              (dynamic.has_soname ? 1u : 0u) +
                              (dynamic.has_string_table ? 2u : 0u) +
                              (dynamic.has_symbol_table ? 3u : 0u) +
                              (dynamic.has_relocation_table ? 3u : 0u);
        if (dynamic.has_symbol_table) {
            symbol_bytes = (uint64_t)dynamic.symbol_count * 16;
            extension_cursor = (extension_cursor + 3u) & ~UINT64_C(3);
            symbol_table_offset = (uint32_t)extension_cursor;
            extension_cursor += symbol_bytes;
            hash_size = 8 + (uint64_t)(dynamic.symbol_count + 1u) * 4;
            extension_cursor = (extension_cursor + 3u) & ~UINT64_C(3);
            hash_table_offset = (uint32_t)extension_cursor;
            extension_cursor += hash_size;
        }
        if (dynamic.has_relocation_table) {
            extension_cursor = (extension_cursor + 3u) & ~UINT64_C(3);
            relocation_table_offset = (uint32_t)extension_cursor;
            extension_cursor += relocation_bytes;
        }
        extension_cursor = (extension_cursor + 3u) & ~UINT64_C(3);
        dynamic_table_offset = (uint32_t)extension_cursor;
        extension_cursor += (uint64_t)dynamic_entry_count * 8;
        if (extension_cursor == 0 || extension_cursor > UINT32_MAX ||
            extension_cursor > SIZE_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        extension_size = (uint32_t)extension_cursor;
        extension = export_allocate(codec_operations, extension_size);
        if (extension == NULL) {
            status = GOTH95_NO_MEMORY;
            goto done;
        }
        memset(extension, 0, extension_size);

        if (dynamic.has_symbol_table) {
            uint32_t symbol_index;
            uint32_t chain_offset;

            for (symbol_index = 0; symbol_index < dynamic.symbol_count;
                 symbol_index++) {
                const uint8_t *source_symbol =
                    source_symbols +
                    (size_t)symbol_index * GOTH95_SYMBOL_SIZE;
                uint8_t *output_symbol =
                    extension + symbol_table_offset +
                    (size_t)symbol_index * 16;
                write_u32(output_symbol, read_u32(source_symbol));
                write_u32(output_symbol + 4, read_u32(source_symbol + 4));
                write_u32(output_symbol + 8, read_u32(source_symbol + 8));
                output_symbol[12] = source_symbol[12];
                output_symbol[13] = 0;
                write_u16(output_symbol + 14,
                          read_u16(source_symbol + 14));
            }

            write_u32(extension + hash_table_offset, 1);
            write_u32(extension + hash_table_offset + 4,
                      dynamic.symbol_count);
            write_u32(extension + hash_table_offset + 8,
                      dynamic.symbol_count > 1 ? 1 : 0);
            chain_offset = hash_table_offset + 12;
            for (symbol_index = 0; symbol_index < dynamic.symbol_count;
                 symbol_index++) {
                uint32_t next = symbol_index + 1;
                write_u32(extension + chain_offset +
                              (size_t)symbol_index * 4,
                          next < dynamic.symbol_count ? next : 0);
            }
        }
        if (dynamic.has_relocation_table) {
            uint32_t relocation_index;
            for (relocation_index = 0;
                 relocation_index < relocation_count;
                 relocation_index++) {
                const uint8_t *source_relocation =
                    source_relocations +
                    (size_t)relocation_index * GOTH95_RELOCATION_SIZE;
                uint8_t *output_relocation =
                    extension + relocation_table_offset +
                    (size_t)relocation_index * 8;
                write_u32(output_relocation, read_u32(source_relocation));
                write_u32(output_relocation + 4,
                          read_u32(source_relocation + 4));
            }
        }

        for (index = 0; index < image->header.segment_count; index++) {
            uint64_t end = (uint64_t)image->segments[index].virtual_address +
                           image->segments[index].memory_size;
            if (end > maximum_address) {
                maximum_address = end;
            }
        }
        working_size = (maximum_address +
                        GOTH95_ELF32_PAGE_SIZE - 1) &
                       ~(uint64_t)(GOTH95_ELF32_PAGE_SIZE - 1);
        if (working_size > UINT32_MAX ||
            working_size + extension_size > UINT32_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        extension_address = (uint32_t)working_size;

        dynamic_output_offset = dynamic_table_offset;
        if (dynamic.has_string_table) {
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_STRTAB,
                                dynamic.string_table);
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_STRSZ,
                                dynamic.string_size);
        }
        if (dynamic.has_symbol_table) {
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_SYMTAB,
                                extension_address + symbol_table_offset);
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_SYMENT, 16);
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_HASH,
                                extension_address + hash_table_offset);
        }
        if (dynamic.has_relocation_table) {
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_REL,
                                extension_address +
                                    relocation_table_offset);
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_RELSZ,
                                relocation_count * 8);
            write_elf32_dynamic(extension, &dynamic_output_offset,
                                GOTH95_ELF32_DT_RELENT, 8);
        }
        for (dynamic_index = 0;
             dynamic_index <
                 image->header.dynamic_table_size / GOTH95_DYNAMIC_SIZE;
             dynamic_index++) {
            uint32_t tag;
            uint32_t flags;
            uint64_t value;

            if (!read_dynamic(image, dynamic_index, &tag, &flags, &value)) {
                status = GOTH95_IO_ERROR;
                goto done;
            }
            if (tag == GOTH95_DYN_NULL) {
                break;
            }
            if (tag == GOTH95_DYN_NEEDED) {
                write_elf32_dynamic(extension, &dynamic_output_offset,
                                    GOTH95_ELF32_DT_NEEDED,
                                    (uint32_t)value);
            } else if (tag == GOTH95_DYN_SONAME) {
                write_elf32_dynamic(extension, &dynamic_output_offset,
                                    GOTH95_ELF32_DT_SONAME,
                                    (uint32_t)value);
            }
        }
        write_elf32_dynamic(extension, &dynamic_output_offset,
                            GOTH95_ELF32_DT_NULL, 0);
        if (dynamic_output_offset !=
            (size_t)dynamic_table_offset +
                (size_t)dynamic_entry_count * 8) {
            status = GOTH95_BAD_FORMAT;
            goto done;
        }
    }

    program_header_count = image->header.segment_count +
                           (has_dynamic ? 2u : 0u);
    if (program_header_count > GOTH95_MAX_SEGMENTS + 2u) {
        status = GOTH95_UNSUPPORTED;
        goto done;
    }
    output_cursor = GOTH95_ELF32_HEADER_SIZE +
                    program_header_count *
                        GOTH95_ELF32_PROGRAM_HEADER_SIZE;
    for (index = 0; index < image->header.segment_count; index++) {
        uint32_t segment_index = segment_order[index];
        const goth95_segment_t *segment =
            &image->segments[segment_index];
        uint32_t alignment = segment->alignment == 0
                                 ? 1
                                 : segment->alignment;
        uint32_t permissions = 0;
        uint64_t file_offset = align_congruent(
            output_cursor, alignment,
            segment->virtual_address & (alignment - 1u));

        if (segment->compression != GOTH95_COMPRESSION_NONE) {
            output_file_sizes[segment_index] = segment->memory_size;
        } else if (output_file_sizes[segment_index] < segment->file_size) {
            output_file_sizes[segment_index] = segment->file_size;
        }
        if (file_offset > UINT32_MAX ||
            file_offset + output_file_sizes[segment_index] > UINT32_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        layouts[segment_index].file_offset = (uint32_t)file_offset;
        layouts[segment_index].file_size =
            output_file_sizes[segment_index];
        output_cursor = file_offset + output_file_sizes[segment_index];
        if ((segment->permissions & GOTH95_PERM_READ) != 0) {
            permissions |= GOTH95_ELF32_PF_R;
        }
        if ((segment->permissions & GOTH95_PERM_WRITE) != 0) {
            permissions |= GOTH95_ELF32_PF_W;
        }
        if ((segment->permissions & GOTH95_PERM_EXECUTE) != 0) {
            permissions |= GOTH95_ELF32_PF_X;
        }
        write_elf32_program_header(
            elf_header_and_program_headers +
                GOTH95_ELF32_HEADER_SIZE +
                index * GOTH95_ELF32_PROGRAM_HEADER_SIZE,
            GOTH95_ELF32_PT_LOAD, layouts[segment_index].file_offset,
            segment->virtual_address, layouts[segment_index].file_size,
            segment->memory_size, permissions, alignment);
    }

    if (has_dynamic) {
        uint64_t maximum_address = 0;
        uint64_t file_offset;
        size_t program_index = image->header.segment_count;

        for (index = 0; index < image->header.segment_count; index++) {
            uint64_t end = (uint64_t)image->segments[index].virtual_address +
                           image->segments[index].memory_size;
            if (end > maximum_address) {
                maximum_address = end;
            }
        }
        working_size = (maximum_address +
                        GOTH95_ELF32_PAGE_SIZE - 1) &
                       ~(uint64_t)(GOTH95_ELF32_PAGE_SIZE - 1);
        extension_address = (uint32_t)working_size;
        file_offset = align_congruent(output_cursor,
                                      GOTH95_ELF32_PAGE_SIZE, 0);
        if (file_offset > UINT32_MAX ||
            file_offset + extension_size > UINT32_MAX ||
            working_size + extension_size > UINT32_MAX) {
            status = GOTH95_UNSUPPORTED;
            goto done;
        }
        extension_file_offset = (uint32_t)file_offset;
        output_cursor = file_offset + extension_size;
        write_elf32_program_header(
            elf_header_and_program_headers +
                GOTH95_ELF32_HEADER_SIZE +
                program_index * GOTH95_ELF32_PROGRAM_HEADER_SIZE,
            GOTH95_ELF32_PT_LOAD, extension_file_offset,
            extension_address, extension_size, extension_size,
            GOTH95_ELF32_PF_R, GOTH95_ELF32_PAGE_SIZE);
        program_index++;
        write_elf32_program_header(
            elf_header_and_program_headers +
                GOTH95_ELF32_HEADER_SIZE +
                program_index * GOTH95_ELF32_PROGRAM_HEADER_SIZE,
            GOTH95_ELF32_PT_DYNAMIC,
            extension_file_offset + dynamic_table_offset,
            extension_address + dynamic_table_offset,
            dynamic_entry_count * 8, dynamic_entry_count * 8,
            GOTH95_ELF32_PF_R, 4);
    }

    output_end = output_cursor;
    if (output_end > sink->capacity ||
        output_end > (uint64_t)UINT32_MAX + 1) {
        status = GOTH95_MEMORY_ERROR;
        goto done;
    }

    elf_header_and_program_headers[0] = 0x7f;
    elf_header_and_program_headers[1] = 'E';
    elf_header_and_program_headers[2] = 'L';
    elf_header_and_program_headers[3] = 'F';
    elf_header_and_program_headers[4] = 1;
    elf_header_and_program_headers[5] = 1;
    elf_header_and_program_headers[6] = 1;
    write_u16(elf_header_and_program_headers + 16,
              image->header.type == GOTH95_TYPE_EXECUTABLE
                  ? GOTH95_ELF32_ET_EXEC
                  : GOTH95_ELF32_ET_DYN);
    write_u16(elf_header_and_program_headers + 18,
              GOTH95_MACHINE_I386);
    write_u32(elf_header_and_program_headers + 20, 1);
    write_u32(elf_header_and_program_headers + 24, image->header.entry);
    write_u32(elf_header_and_program_headers + 28,
              GOTH95_ELF32_HEADER_SIZE);
    write_u32(elf_header_and_program_headers + 32, 0);
    write_u32(elf_header_and_program_headers + 36, 0);
    write_u16(elf_header_and_program_headers + 40,
              GOTH95_ELF32_HEADER_SIZE);
    write_u16(elf_header_and_program_headers + 42,
              GOTH95_ELF32_PROGRAM_HEADER_SIZE);
    write_u16(elf_header_and_program_headers + 44,
              (uint16_t)program_header_count);

    program_header_offset = GOTH95_ELF32_HEADER_SIZE +
                            program_header_count *
                                GOTH95_ELF32_PROGRAM_HEADER_SIZE;
    if (!sink_write(sink, 0, elf_header_and_program_headers,
                    program_header_offset)) {
        status = GOTH95_IO_ERROR;
        goto done;
    }
    output_cursor = program_header_offset;
    for (index = 0; index < image->header.segment_count; index++) {
        uint32_t segment_index = segment_order[index];
        const goth95_elf32_segment_layout_t *layout =
            &layouts[segment_index];
        if (!sink_zero(sink, output_cursor, layout->file_offset)) {
            status = GOTH95_IO_ERROR;
            goto done;
        }
        status = write_elf32_load_segment(
            image, codec_operations, sink, segment_index,
            layout, patches, patch_count);
        if (status != GOTH95_OK) {
            goto done;
        }
        output_cursor = (uint64_t)layout->file_offset + layout->file_size;
    }
    if (has_dynamic) {
        if (!sink_zero(sink, output_cursor, extension_file_offset) ||
            !sink_write(sink, extension_file_offset, extension,
                        extension_size)) {
            status = GOTH95_IO_ERROR;
            goto done;
        }
    }
    *output_size = output_end;
    status = GOTH95_OK;

done:
    if (extension != NULL) {
        codec_operations->free_temporary(codec_operations->context,
                                         extension);
    }
    if (source_relocations != NULL) {
        codec_operations->free_temporary(codec_operations->context,
                                         source_relocations);
    }
    if (patches != NULL) {
        codec_operations->free_temporary(codec_operations->context, patches);
    }
    if (source_symbols != NULL) {
        codec_operations->free_temporary(codec_operations->context,
                                         source_symbols);
    }
    if (strings != NULL) {
        codec_operations->free_temporary(codec_operations->context, strings);
    }
    return status;
}