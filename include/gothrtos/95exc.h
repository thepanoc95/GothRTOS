#ifndef GOTHRTOS_95EXC_H
#define GOTHRTOS_95EXC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GOTH95_MAGIC_SIZE 8u
#define GOTH95_HEADER_SIZE 64u
#define GOTH95_SEGMENT_SIZE 40u
#define GOTH95_DYNAMIC_SIZE 16u
#define GOTH95_SYMBOL_SIZE 20u
#define GOTH95_RELOCATION_SIZE 12u
#define GOTH95_MAX_SEGMENTS 64u
#define GOTH95_MAX_DYNAMIC_ENTRIES 256u
#define GOTH95_MAX_RELOCATIONS 1048576u
#define GOTH95_MAX_SYMBOLS 1048576u
#define GOTH95_MAX_NAME_SIZE 1024u

typedef enum {
    GOTH95_OK = 0,
    GOTH95_INVALID_ARGUMENT,
    GOTH95_IO_ERROR,
    GOTH95_BAD_FORMAT,
    GOTH95_UNSUPPORTED,
    GOTH95_NO_MEMORY,
    GOTH95_LINK_ERROR,
    GOTH95_MEMORY_ERROR
} goth95_status_t;

typedef enum {
    GOTH95_TYPE_EXECUTABLE = 2,
    GOTH95_TYPE_SHARED = 3,
    GOTH95_TYPE_MODULE = 4
} goth95_file_type_t;

typedef enum {
    GOTH95_MACHINE_I386 = 3
} goth95_machine_t;

typedef enum {
    GOTH95_I386_R_NONE = 0,
    GOTH95_I386_R_32 = 1,
    GOTH95_I386_R_PC32 = 2,
    GOTH95_I386_R_GLOB_DAT = 6,
    GOTH95_I386_R_JMP_SLOT = 7,
    GOTH95_I386_R_RELATIVE = 8
} goth95_i386_relocation_t;

typedef enum {
    GOTH95_SEGMENT_LOAD = 1
} goth95_segment_type_t;

enum {
    GOTH95_PERM_READ = 1u,
    GOTH95_PERM_WRITE = 2u,
    GOTH95_PERM_EXECUTE = 4u,
    GOTH95_DYN_OPTIONAL = 1u
};

typedef enum {
    GOTH95_COMPRESSION_NONE = 0
} goth95_compression_t;

typedef enum {
    GOTH95_DYN_NULL = 0,
    GOTH95_DYN_STRTAB = 1,
    GOTH95_DYN_STRSZ = 2,
    GOTH95_DYN_SYMTAB = 3,
    GOTH95_DYN_SYMENT = 4,
    GOTH95_DYN_RELA = 5,
    GOTH95_DYN_RELASZ = 6,
    GOTH95_DYN_RELAENT = 7,
    GOTH95_DYN_NEEDED = 8,
    GOTH95_DYN_SONAME = 9,
    GOTH95_DYN_SYMBOL_COUNT = 10
} goth95_dynamic_tag_t;

typedef struct {
    void *context;
    uint64_t size;
    /* The backing bytes must remain unchanged from open through unload. */
    bool (*read_at)(void *context,
                    uint64_t offset,
                    void *buffer,
                    size_t size);
} goth95_source_t;

typedef struct {
    uint32_t type;
    uint32_t permissions;
    uint64_t file_offset;
    uint32_t virtual_address;
    uint32_t file_size;
    uint32_t memory_size;
    uint32_t alignment;
    uint32_t compression;
} goth95_segment_t;

typedef struct {
    uint16_t version;
    uint16_t header_size;
    uint16_t type;
    uint16_t machine;
    uint32_t flags;
    uint32_t entry;
    uint64_t segment_table_offset;
    uint16_t segment_entry_size;
    uint16_t segment_count;
    uint64_t dynamic_table_offset;
    uint64_t dynamic_table_size;
    uint64_t metadata_offset;
    uint32_t metadata_size;
} goth95_header_t;

typedef struct {
    goth95_source_t source;
    goth95_header_t header;
    goth95_segment_t segments[GOTH95_MAX_SEGMENTS];
} goth95_image_t;

typedef struct {
    void *context;
    /* Address range is [minimum_address, maximum_address). */
    bool (*reserve)(void *context,
                    uint32_t minimum_address,
                    uint32_t maximum_address,
                    uint32_t alignment,
                    /* UINT32_MAX requests any available load bias. */
                    uint32_t preferred_bias,
                    uint32_t *load_bias);
    bool (*write)(void *context,
                  uint32_t address,
                  const void *data,
                  size_t size);
    bool (*read)(void *context,
                 uint32_t address,
                 void *data,
                 size_t size);
    bool (*zero)(void *context, uint32_t address, size_t size);
    bool (*protect)(void *context,
                    uint32_t address,
                    size_t size,
                    uint32_t permissions);
    void (*release)(void *context,
                    uint32_t minimum_address,
                    uint32_t maximum_address,
                    uint32_t load_bias);
    void *(*allocate_temporary)(void *context, size_t size);
    void (*free_temporary)(void *context, void *memory);
    bool (*decompress)(void *context,
                       uint32_t algorithm,
                       const void *compressed,
                       size_t compressed_size,
                       void *uncompressed,
                       size_t uncompressed_capacity,
                       size_t *uncompressed_size);
    bool (*load_dependency)(void *context, const char *name);
    void (*unload_dependency)(void *context, const char *name);
    bool (*resolve_symbol)(void *context,
                           const char *name,
                           uint16_t version,
                           uint32_t *address);
    bool (*publish_symbol)(void *context,
                           const char *module_name,
                           const char *name,
                           uint16_t version,
                           uint32_t address,
                           uint32_t size);
    void (*unpublish_module)(void *context, const char *module_name);
} goth95_loader_ops_t;

typedef struct {
    void *context;
    uint64_t capacity;
    bool (*write_at)(void *context,
                     uint64_t offset,
                     const void *buffer,
                     size_t size);
} goth95_sink_t;

typedef struct {
    uint32_t load_bias;
    uint32_t minimum_address;
    uint32_t maximum_address;
    uint32_t entry;
    uint16_t type;
    bool loaded;
    bool dependencies_loaded;
    bool symbols_published;
} goth95_module_t;

goth95_status_t goth95_image_open(const goth95_source_t *source,
                                  goth95_image_t *image);
/* Loads and relocates, but deliberately does not execute, the entry point. */
goth95_status_t goth95_load(const goth95_image_t *image,
                            const goth95_loader_ops_t *operations,
                            goth95_module_t *module);
goth95_status_t goth95_unload(const goth95_image_t *image,
                              const goth95_loader_ops_t *operations,
                              goth95_module_t *module);
/* codec_operations supplies temporary memory and optional decompression. */
goth95_status_t goth95_export_elf32(
    const goth95_image_t *image,
    const goth95_sink_t *sink,
    const goth95_loader_ops_t *codec_operations,
    uint64_t *output_size);

#endif
