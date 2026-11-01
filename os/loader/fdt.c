#include "gothrtos/fdt.h"

#include <string.h>

enum {
    FDT_HEADER_SIZE = 40,
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE = 2,
    FDT_PROP = 3,
    FDT_NOP = 4,
    FDT_END = 9,
    FDT_VERSION_MIN = 16,
    FDT_VERSION_MAX = 17
};

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    return ((uint64_t)read_be32(bytes) << 32) |
           read_be32(bytes + 4);
}

static bool range_valid(uint32_t offset,
                        uint32_t size,
                        uint32_t total_size)
{
    return offset <= total_size && size <= total_size - offset;
}

static bool next_string(const uint8_t *bytes,
                        uint32_t limit,
                        uint32_t *offset,
                        const char **string,
                        size_t *length)
{
    uint32_t start = *offset;
    uint32_t end = start;

    while (end < limit && bytes[end] != 0) {
        end++;
    }
    if (end == limit) {
        return false;
    }
    if (string != NULL) {
        *string = (const char *)(bytes + start);
    }
    if (length != NULL) {
        *length = end - start;
    }
    {
        uint64_t aligned = ((uint64_t)end + 4u) & ~UINT64_C(3);
        if (aligned > limit) {
            return false;
        }
        *offset = (uint32_t)aligned;
    }
    return true;
}

static bool node_offset_valid(const goth_fdt_t *fdt, uint32_t offset)
{
    return fdt != NULL && fdt->blob != NULL &&
           offset < fdt->structure_size && (offset & 3u) == 0 &&
           read_be32(fdt->blob + fdt->structure_offset + offset) ==
               FDT_BEGIN_NODE;
}

static bool node_name_at(const goth_fdt_t *fdt,
                         uint32_t offset,
                         const char **name,
                         size_t *name_size,
                         uint32_t *after_name)
{
    uint32_t cursor;
    const uint8_t *structure;

    if (!node_offset_valid(fdt, offset)) {
        return false;
    }
    structure = fdt->blob + fdt->structure_offset;
    cursor = offset + 4;
    if (!next_string(structure, fdt->structure_size, &cursor,
                     name, name_size)) {
        return false;
    }
    if (after_name != NULL) {
        *after_name = cursor;
    }
    return true;
}

static bool next_token(const goth_fdt_t *fdt,
                       uint32_t *offset,
                       uint32_t *token,
                       uint32_t *property_size,
                       uint32_t *name_offset,
                       const uint8_t **property_data)
{
    const uint8_t *structure;
    uint32_t cursor;

    if (*offset > fdt->structure_size ||
        fdt->structure_size - *offset < 4) {
        return false;
    }
    structure = fdt->blob + fdt->structure_offset;
    cursor = *offset;
    *token = read_be32(structure + cursor);
    cursor += 4;
    if (*token == FDT_BEGIN_NODE) {
        if (!next_string(structure, fdt->structure_size, &cursor,
                         NULL, NULL)) {
            return false;
        }
    } else if (*token == FDT_PROP) {
        if (fdt->structure_size - cursor < 8) {
            return false;
        }
        *property_size = read_be32(structure + cursor);
        *name_offset = read_be32(structure + cursor + 4);
        cursor += 8;
        if (*property_size > fdt->structure_size - cursor) {
            return false;
        }
        if (property_data != NULL) {
            *property_data = structure + cursor;
        }
        cursor += *property_size;
        {
            uint64_t aligned = ((uint64_t)cursor + 3u) & ~UINT64_C(3);
            if (aligned > fdt->structure_size) {
                return false;
            }
            cursor = (uint32_t)aligned;
        }
    }
    if (cursor > fdt->structure_size) {
        return false;
    }
    *offset = cursor;
    return true;
}

static bool node_handle_valid(const goth_fdt_t *fdt,
                              const goth_fdt_node_t *node)
{
    uint32_t offset = 0;
    uint32_t depth = 0;

    while (offset < fdt->structure_size) {
        uint32_t token_offset = offset;
        uint32_t token;
        uint32_t property_size = 0;
        uint32_t name_offset = 0;
        const uint8_t *property_data = NULL;

        if (!next_token(fdt, &offset, &token, &property_size,
                        &name_offset, &property_data)) {
            return false;
        }
        if (token == FDT_BEGIN_NODE) {
            if (token_offset == node->offset) {
                return depth == node->depth;
            }
            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
        } else if (token == FDT_END) {
            break;
        }
    }
    return false;
}

static bool structure_is_well_formed(const goth_fdt_t *fdt)
{
    uint32_t offset = 0;
    uint32_t depth = 0;
    uint32_t roots = 0;
    bool ended = false;

    while (offset < fdt->structure_size) {
        uint32_t token;
        uint32_t property_size = 0;
        uint32_t name_offset = 0;
        const uint8_t *property_data = NULL;
        uint32_t previous = offset;

        if (!next_token(fdt, &offset, &token, &property_size,
                        &name_offset, &property_data) ||
            offset <= previous) {
            return false;
        }
        (void)property_size;
        (void)property_data;

        if (token == FDT_BEGIN_NODE) {
            uint32_t name_cursor = previous + 4;
            size_t node_name_size;
            if (ended || depth == GOTH_FDT_MAX_DEPTH) {
                return false;
            }
            if (depth == 0 && ++roots != 1) {
                return false;
            }
            if (!next_string(fdt->blob + fdt->structure_offset,
                             fdt->structure_size, &name_cursor,
                             NULL, &node_name_size) ||
                (depth == 0 && node_name_size != 0) ||
                (depth != 0 && node_name_size == 0) ||
                memchr(fdt->blob + fdt->structure_offset + previous + 4,
                       '/', node_name_size) != NULL) {
                return false;
            }
            depth++;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return false;
            }
            depth--;
        } else if (token == FDT_PROP) {
            if (depth == 0 || name_offset >= fdt->strings_size ||
                fdt->blob[fdt->strings_offset + name_offset] == 0 ||
                memchr(fdt->blob + fdt->strings_offset + name_offset, 0,
                       fdt->strings_size - name_offset) == NULL) {
                return false;
            }
        } else if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_END) {
            if (depth != 0 || roots != 1 || offset != fdt->structure_size) {
                return false;
            }
            ended = true;
            break;
        } else {
            return false;
        }
    }
    return ended;
}

goth_fdt_status_t goth_fdt_open(const void *blob,
                                size_t available_size,
                                goth_fdt_t *fdt)
{
    const uint8_t *bytes = blob;
    uint32_t total_size;
    uint32_t structure_offset;
    uint32_t strings_offset;
    uint32_t reserve_offset;
    uint32_t version;
    uint32_t last_compatible;
    uint32_t structure_size;
    uint32_t strings_size;
    uint32_t cursor;
    bool reservation_terminator = false;

    if (blob == NULL || fdt == NULL || available_size < 36) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    if (read_be32(bytes) != GOTH_FDT_MAGIC) {
        return GOTH_FDT_BAD_FORMAT;
    }
    total_size = read_be32(bytes + 4);
    structure_offset = read_be32(bytes + 8);
    strings_offset = read_be32(bytes + 12);
    reserve_offset = read_be32(bytes + 16);
    version = read_be32(bytes + 20);
    last_compatible = read_be32(bytes + 24);
    strings_size = read_be32(bytes + 32);
    structure_size = version >= 17
                         ? (available_size >= FDT_HEADER_SIZE
                                ? read_be32(bytes + 36)
                                : 0)
                         : (strings_offset >= structure_offset
                                ? strings_offset - structure_offset
                                : 0);

    if (total_size < (version >= 17 ? FDT_HEADER_SIZE : 36) ||
        total_size > available_size ||
        version < FDT_VERSION_MIN || version > FDT_VERSION_MAX ||
        last_compatible > version || last_compatible > FDT_VERSION_MAX ||
        (structure_offset & 3u) != 0 ||
        (reserve_offset & 7u) != 0 ||
        structure_offset < (version >= 17 ? FDT_HEADER_SIZE : 36) ||
        strings_offset < (version >= 17 ? FDT_HEADER_SIZE : 36) ||
        reserve_offset < (version >= 17 ? FDT_HEADER_SIZE : 36) ||
        !range_valid(structure_offset, structure_size, total_size) ||
        !range_valid(strings_offset, strings_size, total_size) ||
        structure_size < 4) {
        return GOTH_FDT_BAD_FORMAT;
    }
    if ((uint64_t)structure_offset + structure_size > strings_offset &&
        (uint64_t)strings_offset + strings_size > structure_offset) {
        return GOTH_FDT_BAD_FORMAT;
    }

    cursor = reserve_offset;
    while (range_valid(cursor, 16, total_size)) {
        uint64_t address = read_be64(bytes + cursor);
        uint64_t size = read_be64(bytes + cursor + 8);
        cursor += 16;
        if (address == 0 && size == 0) {
            reservation_terminator = true;
            break;
        }
    }
    if (!reservation_terminator ||
        ((uint64_t)reserve_offset < (uint64_t)structure_offset +
                                       structure_size &&
         (uint64_t)structure_offset <
             (uint64_t)reserve_offset + (cursor - reserve_offset)) ||
        ((uint64_t)reserve_offset < (uint64_t)strings_offset + strings_size &&
         (uint64_t)strings_offset <
             (uint64_t)reserve_offset + (cursor - reserve_offset))) {
        return GOTH_FDT_BAD_FORMAT;
    }

    fdt->blob = bytes;
    fdt->total_size = total_size;
    fdt->structure_offset = structure_offset;
    fdt->structure_size = structure_size;
    fdt->strings_offset = strings_offset;
    fdt->strings_size = strings_size;
    fdt->reserve_offset = reserve_offset;
    fdt->reserve_size = cursor - reserve_offset;
    fdt->version = version;
    if (!structure_is_well_formed(fdt)) {
        memset(fdt, 0, sizeof(*fdt));
        return GOTH_FDT_BAD_FORMAT;
    }
    return GOTH_FDT_OK;
}

goth_fdt_status_t goth_fdt_root(const goth_fdt_t *fdt,
                                goth_fdt_node_t *node)
{
    if (fdt == NULL || node == NULL || fdt->blob == NULL) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    if (!node_offset_valid(fdt, 0)) {
        return GOTH_FDT_BAD_FORMAT;
    }
    node->offset = 0;
    node->depth = 0;
    return GOTH_FDT_OK;
}

goth_fdt_status_t goth_fdt_next_node(const goth_fdt_t *fdt,
                                     goth_fdt_node_t *node)
{
    uint32_t offset;
    uint32_t depth;
    bool found_current = false;

    if (fdt == NULL || node == NULL || fdt->blob == NULL ||
        !node_offset_valid(fdt, node->offset)) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    offset = 0;
    depth = 0;
    while (offset < fdt->structure_size) {
        uint32_t token_offset = offset;
        uint32_t token;
        uint32_t property_size = 0;
        uint32_t name_offset = 0;
        const uint8_t *property_data = NULL;

        if (!next_token(fdt, &offset, &token, &property_size,
                        &name_offset, &property_data)) {
            return GOTH_FDT_BAD_FORMAT;
        }
        if (token == FDT_BEGIN_NODE) {
            if (found_current) {
                node->offset = token_offset;
                node->depth = (uint16_t)depth;
                return GOTH_FDT_OK;
            }
            if (token_offset == node->offset) {
                if (depth != node->depth) {
                    return GOTH_FDT_INVALID_ARGUMENT;
                }
                found_current = true;
            }
            depth++;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return GOTH_FDT_BAD_FORMAT;
            }
            depth--;
        } else if (token == FDT_END) {
            break;
        }
    }
    return GOTH_FDT_NOT_FOUND;
}

goth_fdt_status_t goth_fdt_parent(const goth_fdt_t *fdt,
                                  const goth_fdt_node_t *node,
                                  goth_fdt_node_t *parent)
{
    goth_fdt_node_t current;
    goth_fdt_node_t stack[GOTH_FDT_MAX_DEPTH];
    goth_fdt_status_t status;

    if (fdt == NULL || node == NULL || parent == NULL ||
        !node_offset_valid(fdt, node->offset)) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    status = goth_fdt_root(fdt, &current);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    stack[0] = current;
    while ((status = goth_fdt_next_node(fdt, &current)) == GOTH_FDT_OK) {
        if (current.offset == node->offset) {
            if (current.depth != node->depth) {
                return GOTH_FDT_INVALID_ARGUMENT;
            }
            if (current.depth == 0) {
                return GOTH_FDT_NOT_FOUND;
            }
            *parent = stack[current.depth - 1];
            return GOTH_FDT_OK;
        }
        stack[current.depth] = current;
    }
    return GOTH_FDT_NOT_FOUND;
}

goth_fdt_status_t goth_fdt_first_child(const goth_fdt_t *fdt,
                                       const goth_fdt_node_t *node,
                                       goth_fdt_node_t *child)
{
    goth_fdt_node_t current;
    goth_fdt_status_t status;

    if (fdt == NULL || node == NULL || child == NULL ||
        !node_offset_valid(fdt, node->offset)) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    current = *node;
    while ((status = goth_fdt_next_node(fdt, &current)) == GOTH_FDT_OK) {
        if (current.depth <= node->depth) {
            return GOTH_FDT_NOT_FOUND;
        }
        if (current.depth == node->depth + 1) {
            *child = current;
            return GOTH_FDT_OK;
        }
    }
    return status;
}

goth_fdt_status_t goth_fdt_get_name(const goth_fdt_t *fdt,
                                    const goth_fdt_node_t *node,
                                    const char **name,
                                    size_t *name_size)
{
    if (fdt == NULL || node == NULL || name == NULL ||
        name_size == NULL || !node_offset_valid(fdt, node->offset)) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    if (!node_name_at(fdt, node->offset, name, name_size, NULL)) {
        return GOTH_FDT_BAD_FORMAT;
    }
    return GOTH_FDT_OK;
}

goth_fdt_status_t goth_fdt_get_property(const goth_fdt_t *fdt,
                                        const goth_fdt_node_t *node,
                                        const char *name,
                                        goth_fdt_property_t *property)
{
    uint32_t offset;
    uint32_t depth;
    uint32_t after_name;
    size_t requested_name_size;

    if (fdt == NULL || node == NULL || name == NULL || property == NULL ||
        !node_handle_valid(fdt, node) ||
        !node_name_at(fdt, node->offset, NULL, NULL, &after_name)) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    requested_name_size = strlen(name);
    offset = after_name;
    depth = (uint32_t)node->depth + 1;
    while (offset < fdt->structure_size) {
        uint32_t token_offset = offset;
        uint32_t token;
        uint32_t property_size = 0;
        uint32_t name_offset = 0;
        const uint8_t *property_data = NULL;

        if (!next_token(fdt, &offset, &token, &property_size,
                        &name_offset, &property_data)) {
            return GOTH_FDT_BAD_FORMAT;
        }
        if (token == FDT_PROP) {
            const char *property_name;
            size_t property_name_size;
            if (name_offset >= fdt->strings_size) {
                return GOTH_FDT_BAD_FORMAT;
            }
            property_name = (const char *)(fdt->blob +
                                            fdt->strings_offset +
                                            name_offset);
            property_name_size = strlen(property_name);
            if (property_name_size == requested_name_size &&
                memcmp(property_name, name, requested_name_size) == 0) {
                property->name = property_name;
                property->data = property_data;
                property->size = property_size;
                return GOTH_FDT_OK;
            }
        } else if (token == FDT_BEGIN_NODE) {
            depth++;
        } else if (token == FDT_END_NODE) {
            if (depth == (uint32_t)node->depth + 1) {
                return GOTH_FDT_NOT_FOUND;
            }
            depth--;
        } else if (token == FDT_END) {
            break;
        }
        if (token_offset == node->offset) {
            return GOTH_FDT_BAD_FORMAT;
        }
    }
    return GOTH_FDT_NOT_FOUND;
}

goth_fdt_status_t goth_fdt_find_path(const goth_fdt_t *fdt,
                                     const char *path,
                                     goth_fdt_node_t *node)
{
    goth_fdt_node_t current;
    const char *component;
    goth_fdt_status_t status;

    if (fdt == NULL || path == NULL || node == NULL ||
        path[0] != '/') {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    status = goth_fdt_root(fdt, &current);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    component = path + 1;
    while (*component != '\0') {
        const char *end = strchr(component, '/');
        size_t component_size = end == NULL
                                   ? strlen(component)
                                   : (size_t)(end - component);
        goth_fdt_node_t child;
        bool matched = false;

        if (component_size == 0) {
            return GOTH_FDT_INVALID_ARGUMENT;
        }
        status = goth_fdt_first_child(fdt, &current, &child);
        while (status == GOTH_FDT_OK) {
            const char *child_name;
            size_t child_name_size;
            if (goth_fdt_get_name(fdt, &child, &child_name,
                                  &child_name_size) == GOTH_FDT_OK &&
                child_name_size == component_size &&
                memcmp(child_name, component, component_size) == 0) {
                current = child;
                matched = true;
                break;
            }
            status = goth_fdt_next_node(fdt, &child);
            if (status == GOTH_FDT_OK && child.depth <= current.depth) {
                status = GOTH_FDT_NOT_FOUND;
            }
        }
        if (!matched) {
            return GOTH_FDT_NOT_FOUND;
        }
        if (end == NULL) {
            break;
        }
        component = end + 1;
        if (*component == '\0') {
            return GOTH_FDT_INVALID_ARGUMENT;
        }
    }
    *node = current;
    return GOTH_FDT_OK;
}

bool goth_fdt_node_is_compatible(const goth_fdt_t *fdt,
                                 const goth_fdt_node_t *node,
                                 const char *compatible)
{
    goth_fdt_property_t property;
    size_t target_size;
    uint32_t offset = 0;

    if (compatible == NULL ||
        goth_fdt_get_property(fdt, node, "compatible", &property) !=
            GOTH_FDT_OK) {
        return false;
    }
    target_size = strlen(compatible);
    while (offset < property.size) {
        const uint8_t *end = memchr(property.data + offset, 0,
                                    property.size - offset);
        size_t item_size;
        if (end == NULL) {
            return false;
        }
        item_size = (size_t)(end - (property.data + offset));
        if (item_size == target_size &&
            memcmp(property.data + offset, compatible, target_size) == 0) {
            return true;
        }
        offset += (uint32_t)item_size + 1;
    }
    return false;
}

static goth_fdt_status_t get_cell_count(const goth_fdt_t *fdt,
                                        const goth_fdt_node_t *node,
                                        const char *property_name,
                                        uint32_t default_value,
                                        uint32_t *value)
{
    goth_fdt_property_t property;
    goth_fdt_status_t status = goth_fdt_get_property(
        fdt, node, property_name, &property);

    if (status == GOTH_FDT_NOT_FOUND) {
        *value = default_value;
        return GOTH_FDT_OK;
    }
    if (status != GOTH_FDT_OK) {
        return status;
    }
    if (property.size != 4) {
        return GOTH_FDT_BAD_FORMAT;
    }
    *value = read_be32(property.data);
    return GOTH_FDT_OK;
}

goth_fdt_status_t goth_fdt_get_reg(const goth_fdt_t *fdt,
                                   const goth_fdt_node_t *node,
                                   size_t index,
                                   goth_fdt_range_t *range)
{
    goth_fdt_node_t parent;
    goth_fdt_property_t property;
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t tuple_cells;
    uint32_t tuple_bytes;
    size_t offset;
    goth_fdt_status_t status;

    if (fdt == NULL || node == NULL || range == NULL) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    status = goth_fdt_parent(fdt, node, &parent);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    status = get_cell_count(fdt, &parent, "#address-cells", 2,
                            &address_cells);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    status = get_cell_count(fdt, &parent, "#size-cells", 1, &size_cells);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    if (address_cells == 0 || address_cells > GOTH_FDT_MAX_CELLS ||
        size_cells > GOTH_FDT_MAX_CELLS) {
        return GOTH_FDT_UNSUPPORTED;
    }
    tuple_cells = address_cells + size_cells;
    tuple_bytes = tuple_cells * 4;
    status = goth_fdt_get_property(fdt, node, "reg", &property);
    if (status != GOTH_FDT_OK) {
        return status;
    }
    if (tuple_bytes == 0 || property.size % tuple_bytes != 0 ||
        index >= property.size / tuple_bytes) {
        return GOTH_FDT_NOT_FOUND;
    }
    offset = index * tuple_bytes;
    range->address = address_cells == 1
                         ? read_be32(property.data + offset)
                         : read_be64(property.data + offset);
    offset += address_cells * 4;
    range->size = size_cells == 0
                      ? 0
                      : size_cells == 1
                            ? read_be32(property.data + offset)
                            : read_be64(property.data + offset);
    return GOTH_FDT_OK;
}

void goth_fdt_reservation_begin(const goth_fdt_t *fdt,
                               goth_fdt_reservation_iterator_t *iterator)
{
    if (iterator != NULL) {
        iterator->offset = fdt != NULL ? fdt->reserve_offset : 0;
    }
}

goth_fdt_status_t goth_fdt_reservation_next(
    const goth_fdt_t *fdt,
    goth_fdt_reservation_iterator_t *iterator,
    goth_fdt_range_t *range)
{
    const uint8_t *entry;

    if (fdt == NULL || iterator == NULL || range == NULL ||
        fdt->blob == NULL) {
        return GOTH_FDT_INVALID_ARGUMENT;
    }
    if (iterator->offset < fdt->reserve_offset ||
        iterator->offset - fdt->reserve_offset >= fdt->reserve_size ||
        (iterator->offset - fdt->reserve_offset) % 16 != 0 ||
        !range_valid(iterator->offset, 16, (uint32_t)fdt->total_size)) {
        return GOTH_FDT_BAD_FORMAT;
    }
    entry = fdt->blob + iterator->offset;
    range->address = read_be64(entry);
    range->size = read_be64(entry + 8);
    iterator->offset += 16;
    if (range->address == 0 && range->size == 0) {
        return GOTH_FDT_NOT_FOUND;
    }
    if (range->size > UINT64_MAX - range->address) {
        return GOTH_FDT_BAD_FORMAT;
    }
    return GOTH_FDT_OK;
}
