#ifndef GOTHRTOS_VFS_H
#define GOTHRTOS_VFS_H

#include "gothrtos/kernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RAM-backed virtual filesystem.
 *
 * The VFS is a small but real in-memory filesystem: a fixed node table, a
 * fixed block pool, directories with sibling lists, file handles with a
 * position, and create/read/write/seek/truncate/unlink/readdir operations.
 * It uses no dynamic allocation, so it works identically in user mode and on
 * bare metal.
 *
 * The filesystem is mounted at "/" as the `ramfs` backend. Operating-system
 * policy (which directories a distribution creates, mount tables in Lua, and
 * so on) stays in userland. */

#define GOTH_VFS_MAX_PATH 128U
#define GOTH_VFS_MAX_NAME 32U
#define GOTH_VFS_MAX_NODES 64U
#define GOTH_VFS_MAX_OPEN_FILES 16U
#define GOTH_VFS_BLOCK_SIZE 256U
#define GOTH_VFS_BLOCK_COUNT 256U
#define GOTH_VFS_MAX_FILE_SIZE \
    ((uint64_t)GOTH_VFS_BLOCK_SIZE * GOTH_VFS_BLOCK_COUNT)

typedef enum {
    GOTH_VFS_NODE_UNUSED = 0,
    GOTH_VFS_NODE_DIRECTORY,
    GOTH_VFS_NODE_FILE
} goth_vfs_node_type_t;

typedef enum {
    GOTH_VFS_SEEK_SET = 0,
    GOTH_VFS_SEEK_CUR,
    GOTH_VFS_SEEK_END
} goth_vfs_whence_t;

#define GOTH_VFS_OPEN_READ      (1U << 0)
#define GOTH_VFS_OPEN_WRITE     (1U << 1)
#define GOTH_VFS_OPEN_CREATE    (1U << 2)
#define GOTH_VFS_OPEN_TRUNCATE  (1U << 3)
#define GOTH_VFS_OPEN_APPEND    (1U << 4)

typedef struct {
    uint32_t node;
    goth_vfs_node_type_t type;
    uint64_t size;
} goth_vfs_stat_t;

typedef struct {
    char name[GOTH_VFS_MAX_NAME];
    goth_vfs_node_type_t type;
    uint64_t size;
} goth_vfs_dirent_t;

void goth_vfs_init(void);
bool goth_vfs_is_initialized(void);
const char *goth_vfs_backend_name(void);
const char *goth_vfs_mount_point(void);
void goth_vfs_register_memory(void);

goth_status_t goth_vfs_open(const char *path, uint32_t flags, uint32_t *fd);
goth_status_t goth_vfs_close(uint32_t fd);
goth_status_t goth_vfs_read(uint32_t fd,
                            void *buffer,
                            size_t capacity,
                            size_t *read_length);
goth_status_t goth_vfs_write(uint32_t fd,
                             const void *data,
                             size_t length,
                             size_t *written);
goth_status_t goth_vfs_seek(uint32_t fd,
                            int64_t offset,
                            goth_vfs_whence_t whence,
                            uint64_t *position);
goth_status_t goth_vfs_truncate(uint32_t fd, uint64_t size);
goth_status_t goth_vfs_stat(const char *path, goth_vfs_stat_t *stat);
goth_status_t goth_vfs_mkdir(const char *path);
goth_status_t goth_vfs_unlink(const char *path);
goth_status_t goth_vfs_readdir(const char *path,
                               uint32_t index,
                               goth_vfs_dirent_t *entry);
goth_status_t goth_vfs_usage(uint32_t *used_blocks, uint32_t *total_blocks);

#endif
