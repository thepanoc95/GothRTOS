#ifndef GOTHRTOS_FDT_H
#define GOTHRTOS_FDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GOTH_FDT_MAGIC 0xd00dfeedu
#define GOTH_FDT_MAX_DEPTH 64u
#define GOTH_FDT_MAX_CELLS 2u

typedef enum {
    GOTH_FDT_OK = 0,
    GOTH_FDT_INVALID_ARGUMENT,
    GOTH_FDT_BAD_FORMAT,
    GOTH_FDT_NOT_FOUND,
    GOTH_FDT_UNSUPPORTED,
    GOTH_FDT_BUFFER_TOO_SMALL
} goth_fdt_status_t;

typedef struct {
    const uint8_t *blob;
    size_t total_size;
    uint32_t structure_offset;
    uint32_t structure_size;
    uint32_t strings_offset;
    uint32_t strings_size;
    uint32_t reserve_offset;
    uint32_t reserve_size;
    uint32_t version;
} goth_fdt_t;

typedef struct {
    uint32_t offset;
    uint16_t depth;
} goth_fdt_node_t;

typedef struct {
    const char *name;
    const uint8_t *data;
    uint32_t size;
} goth_fdt_property_t;

typedef struct {
    uint64_t address;
    uint64_t size;
} goth_fdt_range_t;

typedef struct {
    uint32_t offset;
} goth_fdt_reservation_iterator_t;

goth_fdt_status_t goth_fdt_open(const void *blob,
                                size_t available_size,
                                goth_fdt_t *fdt);
goth_fdt_status_t goth_fdt_root(const goth_fdt_t *fdt,
                                goth_fdt_node_t *node);
goth_fdt_status_t goth_fdt_next_node(const goth_fdt_t *fdt,
                                     goth_fdt_node_t *node);
goth_fdt_status_t goth_fdt_parent(const goth_fdt_t *fdt,
                                  const goth_fdt_node_t *node,
                                  goth_fdt_node_t *parent);
goth_fdt_status_t goth_fdt_first_child(const goth_fdt_t *fdt,
                                       const goth_fdt_node_t *node,
                                       goth_fdt_node_t *child);
goth_fdt_status_t goth_fdt_find_path(const goth_fdt_t *fdt,
                                     const char *path,
                                     goth_fdt_node_t *node);
goth_fdt_status_t goth_fdt_get_name(const goth_fdt_t *fdt,
                                    const goth_fdt_node_t *node,
                                    const char **name,
                                    size_t *name_size);
goth_fdt_status_t goth_fdt_get_property(const goth_fdt_t *fdt,
                                        const goth_fdt_node_t *node,
                                        const char *name,
                                        goth_fdt_property_t *property);
bool goth_fdt_node_is_compatible(const goth_fdt_t *fdt,
                                 const goth_fdt_node_t *node,
                                 const char *compatible);
goth_fdt_status_t goth_fdt_get_reg(const goth_fdt_t *fdt,
                                   const goth_fdt_node_t *node,
                                   size_t index,
                                   goth_fdt_range_t *range);
void goth_fdt_reservation_begin(const goth_fdt_t *fdt,
                               goth_fdt_reservation_iterator_t *iterator);
goth_fdt_status_t goth_fdt_reservation_next(
    const goth_fdt_t *fdt,
    goth_fdt_reservation_iterator_t *iterator,
    goth_fdt_range_t *range);

#endif
