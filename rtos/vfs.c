#include "gothrtos/vfs.h"

#include "gothrtos/klog.h"
#include "gothrtos/mem.h"

#define VFS_NO_NODE UINT16_MAX
#define VFS_NO_BLOCK UINT16_MAX
#define VFS_MAX_COMPONENTS 32U

typedef struct {
    bool used;
    goth_vfs_node_type_t type;
    char name[GOTH_VFS_MAX_NAME];
    uint16_t parent;
    uint16_t first_child;
    uint16_t next_sibling;
    uint16_t first_block;
    uint32_t size;
    uint32_t open_refs;
} vfs_node_t;

typedef struct {
    bool used;
    uint16_t next;
} vfs_block_t;

typedef struct {
    bool used;
    uint16_t node;
    uint32_t position;
    uint32_t flags;
} vfs_file_t;

typedef struct {
    uint16_t node;
    uint16_t parent;
    const char *name;
    size_t name_length;
    bool found;
} vfs_resolved_t;

static vfs_node_t nodes[GOTH_VFS_MAX_NODES];
static vfs_block_t blocks[GOTH_VFS_BLOCK_COUNT];
static vfs_file_t files[GOTH_VFS_MAX_OPEN_FILES];
static uint8_t pool[GOTH_VFS_BLOCK_COUNT * GOTH_VFS_BLOCK_SIZE];
static bool vfs_initialized;

static size_t vfs_strlen(const char *text)
{
    size_t length = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool vfs_equal(const char *first, const char *second, size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

static void vfs_copy(char *destination, const char *source, size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++) {
        destination[index] = source[index];
    }
}

static void vfs_zero(void *memory, size_t length)
{
    uint8_t *bytes = (uint8_t *)memory;
    size_t index;

    for (index = 0U; index < length; index++) {
        bytes[index] = 0U;
    }
}

static uint8_t *vfs_block_data(uint16_t block)
{
    return &pool[(size_t)block * GOTH_VFS_BLOCK_SIZE];
}

static uint16_t vfs_allocate_block(void)
{
    uint16_t index;

    for (index = 0U; index < GOTH_VFS_BLOCK_COUNT; index++) {
        if (!blocks[index].used) {
            blocks[index].used = true;
            blocks[index].next = VFS_NO_BLOCK;
            vfs_zero(vfs_block_data(index), GOTH_VFS_BLOCK_SIZE);
            return index;
        }
    }
    return VFS_NO_BLOCK;
}

static void vfs_free_blocks(uint16_t first_block)
{
    uint16_t block = first_block;

    while (block != VFS_NO_BLOCK && block < GOTH_VFS_BLOCK_COUNT) {
        uint16_t next = blocks[block].next;

        blocks[block].used = false;
        blocks[block].next = VFS_NO_BLOCK;
        block = next;
    }
}

static uint16_t vfs_node_blocks(const vfs_node_t *node)
{
    uint16_t count = 0U;
    uint16_t block = node->first_block;

    while (block != VFS_NO_BLOCK && count < GOTH_VFS_BLOCK_COUNT) {
        count++;
        block = blocks[block].next;
    }
    return count;
}

static uint8_t *vfs_block_at(const vfs_node_t *node, uint32_t index)
{
    uint16_t block = node->first_block;
    uint32_t position = 0U;

    while (block != VFS_NO_BLOCK && block < GOTH_VFS_BLOCK_COUNT) {
        if (position == index) {
            return vfs_block_data(block);
        }
        block = blocks[block].next;
        position++;
    }
    return NULL;
}

static goth_status_t vfs_ensure_blocks(vfs_node_t *node, uint32_t needed)
{
    uint16_t block;
    uint16_t count;

    if (needed > GOTH_VFS_BLOCK_COUNT) {
        return GOTH_STATUS_FULL;
    }

    count = vfs_node_blocks(node);
    while (count < needed) {
        uint16_t fresh = vfs_allocate_block();

        if (fresh == VFS_NO_BLOCK) {
            return GOTH_STATUS_FULL;
        }
        if (node->first_block == VFS_NO_BLOCK) {
            node->first_block = fresh;
        } else {
            block = node->first_block;
            while (blocks[block].next != VFS_NO_BLOCK) {
                block = blocks[block].next;
            }
            blocks[block].next = fresh;
        }
        count++;
    }
    return GOTH_STATUS_OK;
}

static uint16_t vfs_find_child(uint16_t parent, const char *name,
                               size_t length)
{
    uint16_t child = nodes[parent].first_child;

    while (child != VFS_NO_NODE && child < GOTH_VFS_MAX_NODES) {
        if (vfs_strlen(nodes[child].name) == length &&
            vfs_equal(nodes[child].name, name, length)) {
            return child;
        }
        child = nodes[child].next_sibling;
    }
    return VFS_NO_NODE;
}

static uint16_t vfs_create_node(uint16_t parent, const char *name,
                                size_t length, goth_vfs_node_type_t type)
{
    uint16_t index;

    if (length == 0U || length >= GOTH_VFS_MAX_NAME) {
        return VFS_NO_NODE;
    }

    for (index = 0U; index < GOTH_VFS_MAX_NODES; index++) {
        if (!nodes[index].used) {
            vfs_zero(&nodes[index], sizeof(nodes[index]));
            nodes[index].used = true;
            nodes[index].type = type;
            vfs_copy(nodes[index].name, name, length);
            nodes[index].name[length] = '\0';
            nodes[index].parent = parent;
            nodes[index].first_child = VFS_NO_NODE;
            nodes[index].next_sibling = nodes[parent].first_child;
            nodes[index].first_block = VFS_NO_BLOCK;
            nodes[parent].first_child = index;
            return index;
        }
    }
    return VFS_NO_NODE;
}

static uint16_t vfs_next_component(const char *path, size_t *offset,
                                   size_t *length)
{
    size_t start;
    size_t cursor = *offset;

    while (path[cursor] == '/') {
        cursor++;
    }
    if (path[cursor] == '\0') {
        *offset = cursor;
        return VFS_NO_NODE;
    }
    start = cursor;
    while (path[cursor] != '\0' && path[cursor] != '/') {
        cursor++;
    }
    *length = cursor - start;
    *offset = cursor;
    return start;
}

static bool vfs_component_is_last(const char *path, size_t offset)
{
    while (path[offset] == '/') {
        offset++;
    }
    return path[offset] == '\0';
}

static goth_status_t vfs_resolve(const char *path, vfs_resolved_t *resolved)
{
    uint16_t current = 0U;
    size_t offset = 0U;
    size_t length = 0U;
    uint16_t start;
    uint32_t components = 0U;

    if (path == NULL || path[0] != '/') {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (vfs_strlen(path) >= GOTH_VFS_MAX_PATH) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    resolved->node = 0U;
    resolved->parent = 0U;
    resolved->name = path;
    resolved->name_length = 0U;
    resolved->found = true;

    for (;;) {
        start = vfs_next_component(path, &offset, &length);
        if (start == VFS_NO_NODE) {
            break;
        }
        if (++components > VFS_MAX_COMPONENTS) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
        if (length >= GOTH_VFS_MAX_NAME) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }

        if (length == 1U && path[start] == '.') {
            if (vfs_component_is_last(path, offset)) {
                resolved->node = current;
                resolved->parent = nodes[current].parent;
                resolved->found = true;
            }
            continue;
        }
        if (length == 2U && path[start] == '.' && path[start + 1U] == '.') {
            current = nodes[current].parent;
            if (vfs_component_is_last(path, offset)) {
                resolved->node = current;
                resolved->parent = nodes[current].parent;
                resolved->found = true;
            }
            continue;
        }

        if (vfs_component_is_last(path, offset)) {
            uint16_t child = vfs_find_child(current, path + start, length);

            resolved->parent = current;
            resolved->name = path + start;
            resolved->name_length = length;
            resolved->node = child;
            resolved->found = child != VFS_NO_NODE;
            return GOTH_STATUS_OK;
        }

        {
            uint16_t child = vfs_find_child(current, path + start, length);

            if (child == VFS_NO_NODE ||
                nodes[child].type != GOTH_VFS_NODE_DIRECTORY) {
                return GOTH_STATUS_EMPTY;
            }
            current = child;
        }
    }

    resolved->node = current;
    resolved->parent = nodes[current].parent;
    resolved->found = true;
    return GOTH_STATUS_OK;
}

static vfs_file_t *vfs_file(uint32_t fd)
{
    if (fd >= GOTH_VFS_MAX_OPEN_FILES || !files[fd].used) {
        return NULL;
    }
    return &files[fd];
}

static vfs_node_t *vfs_open_node(vfs_file_t *file)
{
    if (file == NULL || file->node >= GOTH_VFS_MAX_NODES ||
        !nodes[file->node].used) {
        return NULL;
    }
    return &nodes[file->node];
}

void goth_vfs_init(void)
{
    size_t index;

    vfs_zero(nodes, sizeof(nodes));
    vfs_zero(blocks, sizeof(blocks));
    vfs_zero(files, sizeof(files));
    vfs_zero(pool, sizeof(pool));

    for (index = 0U; index < GOTH_VFS_MAX_NODES; index++) {
        nodes[index].used = false;
        nodes[index].parent = VFS_NO_NODE;
        nodes[index].first_child = VFS_NO_NODE;
        nodes[index].next_sibling = VFS_NO_NODE;
        nodes[index].first_block = VFS_NO_BLOCK;
    }
    for (index = 0U; index < GOTH_VFS_BLOCK_COUNT; index++) {
        blocks[index].used = false;
        blocks[index].next = VFS_NO_BLOCK;
    }

    nodes[0].used = true;
    nodes[0].type = GOTH_VFS_NODE_DIRECTORY;
    nodes[0].name[0] = '/';
    nodes[0].name[1] = '\0';
    nodes[0].parent = 0U;
    nodes[0].first_child = VFS_NO_NODE;
    nodes[0].next_sibling = VFS_NO_NODE;
    nodes[0].first_block = VFS_NO_BLOCK;
    nodes[0].size = 0U;
    nodes[0].open_refs = 0U;

    vfs_initialized = true;
    goth_vfs_register_memory();
}

bool goth_vfs_is_initialized(void)
{
    return vfs_initialized;
}

const char *goth_vfs_backend_name(void)
{
    return "ramfs";
}

const char *goth_vfs_mount_point(void)
{
    return "/";
}

void goth_vfs_register_memory(void)
{
    (void)goth_mem_register(nodes, sizeof(nodes),
                            GOTH_MEM_READ | GOTH_MEM_WRITE,
                            GOTH_MEM_KERNEL, 0U, "vfs nodes");
    (void)goth_mem_register(blocks, sizeof(blocks),
                            GOTH_MEM_READ | GOTH_MEM_WRITE,
                            GOTH_MEM_KERNEL, 0U, "vfs blocks");
    (void)goth_mem_register(files, sizeof(files),
                            GOTH_MEM_READ | GOTH_MEM_WRITE,
                            GOTH_MEM_KERNEL, 0U, "vfs handles");
    (void)goth_mem_register(pool, sizeof(pool),
                            GOTH_MEM_READ | GOTH_MEM_WRITE,
                            GOTH_MEM_KERNEL, 0U, "vfs data");
}

goth_status_t goth_vfs_open(const char *path, uint32_t flags, uint32_t *fd)
{
    vfs_resolved_t resolved;
    vfs_file_t *file = NULL;
    uint16_t node;
    uint32_t index;
    uint32_t slot = GOTH_VFS_MAX_OPEN_FILES;
    goth_status_t status;

    if (!vfs_initialized || fd == NULL) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if ((flags & (GOTH_VFS_OPEN_READ | GOTH_VFS_OPEN_WRITE)) == 0U ||
        (flags & ~(GOTH_VFS_OPEN_READ | GOTH_VFS_OPEN_WRITE |
                   GOTH_VFS_OPEN_CREATE | GOTH_VFS_OPEN_TRUNCATE |
                   GOTH_VFS_OPEN_APPEND)) != 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    status = vfs_resolve(path, &resolved);
    if (status != GOTH_STATUS_OK) {
        return status;
    }

    if (!resolved.found) {
        if ((flags & GOTH_VFS_OPEN_CREATE) == 0U ||
            (flags & GOTH_VFS_OPEN_WRITE) == 0U) {
            return GOTH_STATUS_EMPTY;
        }
        if (resolved.parent >= GOTH_VFS_MAX_NODES ||
            nodes[resolved.parent].type != GOTH_VFS_NODE_DIRECTORY) {
            return GOTH_STATUS_BAD_STATE;
        }
        node = vfs_create_node(resolved.parent, resolved.name,
                               resolved.name_length, GOTH_VFS_NODE_FILE);
        if (node == VFS_NO_NODE) {
            return GOTH_STATUS_FULL;
        }
    } else {
        node = resolved.node;
        if (nodes[node].type != GOTH_VFS_NODE_FILE) {
            return GOTH_STATUS_BAD_STATE;
        }
    }

    for (index = 0U; index < GOTH_VFS_MAX_OPEN_FILES; index++) {
        if (!files[index].used) {
            slot = index;
            break;
        }
    }
    if (slot == GOTH_VFS_MAX_OPEN_FILES) {
        return GOTH_STATUS_FULL;
    }
    file = &files[slot];
    *fd = slot;

    file->used = true;
    file->node = node;
    file->flags = flags;

    if ((flags & GOTH_VFS_OPEN_TRUNCATE) != 0U) {
        vfs_free_blocks(nodes[node].first_block);
        nodes[node].first_block = VFS_NO_BLOCK;
        nodes[node].size = 0U;
        file->position = 0U;
    } else if ((flags & GOTH_VFS_OPEN_APPEND) != 0U) {
        file->position = nodes[node].size;
    } else {
        file->position = 0U;
    }
    nodes[node].open_refs++;
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_close(uint32_t fd)
{
    vfs_file_t *file = vfs_file(fd);
    vfs_node_t *node;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (file == NULL) {
        return GOTH_STATUS_EMPTY;
    }
    node = vfs_open_node(file);
    if (node != NULL && node->open_refs > 0U) {
        node->open_refs--;
    }
    file->used = false;
    file->node = 0U;
    file->position = 0U;
    file->flags = 0U;
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_read(uint32_t fd,
                            void *buffer,
                            size_t capacity,
                            size_t *read_length)
{
    vfs_file_t *file = vfs_file(fd);
    vfs_node_t *node;
    uint8_t *destination = (uint8_t *)buffer;
    size_t total = 0U;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (file == NULL || read_length == NULL ||
        (buffer == NULL && capacity != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if ((file->flags & GOTH_VFS_OPEN_READ) == 0U) {
        return GOTH_STATUS_BAD_STATE;
    }

    node = vfs_open_node(file);
    if (node == NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    *read_length = 0U;

    while (total < capacity && file->position < node->size) {
        uint32_t block_index = file->position / GOTH_VFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % GOTH_VFS_BLOCK_SIZE;
        uint32_t available = GOTH_VFS_BLOCK_SIZE - block_offset;
        uint32_t remaining = node->size - file->position;
        uint8_t *data = vfs_block_at(node, block_index);
        size_t chunk;

        if (data == NULL) {
            break;
        }
        chunk = capacity - total;
        if (chunk > available) {
            chunk = available;
        }
        if (chunk > remaining) {
            chunk = remaining;
        }
        vfs_copy((char *)destination + total, (const char *)data + block_offset,
                 chunk);
        total += chunk;
        file->position += (uint32_t)chunk;
    }

    *read_length = total;
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_write(uint32_t fd,
                             const void *data,
                             size_t length,
                             size_t *written)
{
    vfs_file_t *file = vfs_file(fd);
    vfs_node_t *node;
    const uint8_t *source = (const uint8_t *)data;
    size_t total = 0U;
    uint32_t needed;
    goth_status_t status;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (file == NULL || written == NULL ||
        (data == NULL && length != 0U)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if ((file->flags & GOTH_VFS_OPEN_WRITE) == 0U) {
        return GOTH_STATUS_BAD_STATE;
    }

    node = vfs_open_node(file);
    if (node == NULL) {
        return GOTH_STATUS_BAD_STATE;
    }
    *written = 0U;
    if (length == 0U) {
        return GOTH_STATUS_OK;
    }
    if ((uint64_t)file->position + (uint64_t)length >
        GOTH_VFS_MAX_FILE_SIZE) {
        return GOTH_STATUS_FULL;
    }

    needed = (uint32_t)(((uint64_t)file->position + (uint64_t)length +
                         GOTH_VFS_BLOCK_SIZE - 1U) / GOTH_VFS_BLOCK_SIZE);
    status = vfs_ensure_blocks(node, needed);
    if (status != GOTH_STATUS_OK) {
        return status;
    }

    while (total < length) {
        uint32_t block_index = file->position / GOTH_VFS_BLOCK_SIZE;
        uint32_t block_offset = file->position % GOTH_VFS_BLOCK_SIZE;
        uint32_t available = GOTH_VFS_BLOCK_SIZE - block_offset;
        uint8_t *destination = vfs_block_at(node, block_index);
        size_t chunk = length - total;

        if (destination == NULL) {
            break;
        }
        if (chunk > available) {
            chunk = available;
        }
        vfs_copy((char *)destination + block_offset,
                 (const char *)source + total, chunk);
        total += chunk;
        file->position += (uint32_t)chunk;
    }

    if (file->position > node->size) {
        node->size = file->position;
    }
    *written = total;
    return total == length ? GOTH_STATUS_OK : GOTH_STATUS_FULL;
}

goth_status_t goth_vfs_seek(uint32_t fd,
                            int64_t offset,
                            goth_vfs_whence_t whence,
                            uint64_t *position)
{
    vfs_file_t *file = vfs_file(fd);
    vfs_node_t *node;
    int64_t base;
    int64_t result;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (file == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    node = vfs_open_node(file);
    if (node == NULL) {
        return GOTH_STATUS_BAD_STATE;
    }

    switch (whence) {
    case GOTH_VFS_SEEK_SET:
        base = 0;
        break;
    case GOTH_VFS_SEEK_CUR:
        base = (int64_t)file->position;
        break;
    case GOTH_VFS_SEEK_END:
        base = (int64_t)node->size;
        break;
    default:
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    result = base + offset;
    if (result < 0 || (uint64_t)result > GOTH_VFS_MAX_FILE_SIZE) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    file->position = (uint32_t)result;
    if (position != NULL) {
        *position = (uint64_t)result;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_truncate(uint32_t fd, uint64_t size)
{
    vfs_file_t *file = vfs_file(fd);
    vfs_node_t *node;
    uint32_t needed;
    uint16_t block;
    uint16_t previous = VFS_NO_BLOCK;
    uint32_t index = 0U;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    if (file == NULL || size > GOTH_VFS_MAX_FILE_SIZE) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if ((file->flags & GOTH_VFS_OPEN_WRITE) == 0U) {
        return GOTH_STATUS_BAD_STATE;
    }
    node = vfs_open_node(file);
    if (node == NULL) {
        return GOTH_STATUS_BAD_STATE;
    }

    needed = (uint32_t)((size + GOTH_VFS_BLOCK_SIZE - 1U) /
                        GOTH_VFS_BLOCK_SIZE);
    if (needed > 0U) {
        goth_status_t status = vfs_ensure_blocks(node, needed);

        if (status != GOTH_STATUS_OK) {
            return status;
        }
    }

    block = node->first_block;
    while (block != VFS_NO_BLOCK && block < GOTH_VFS_BLOCK_COUNT) {
        uint16_t next = blocks[block].next;

        if (index >= needed) {
            blocks[block].used = false;
            blocks[block].next = VFS_NO_BLOCK;
            if (previous == VFS_NO_BLOCK) {
                node->first_block = VFS_NO_BLOCK;
            } else {
                blocks[previous].next = VFS_NO_BLOCK;
            }
        } else {
            previous = block;
        }
        index++;
        block = next;
    }

    node->size = (uint32_t)size;
    if (file->position > node->size) {
        file->position = node->size;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_stat(const char *path, goth_vfs_stat_t *stat)
{
    vfs_resolved_t resolved;
    goth_status_t status;

    if (!vfs_initialized || stat == NULL) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    status = vfs_resolve(path, &resolved);
    if (status != GOTH_STATUS_OK) {
        return status;
    }
    if (!resolved.found) {
        return GOTH_STATUS_EMPTY;
    }

    stat->node = resolved.node;
    stat->type = nodes[resolved.node].type;
    stat->size = nodes[resolved.node].size;
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_mkdir(const char *path)
{
    vfs_resolved_t resolved;
    goth_status_t status;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    status = vfs_resolve(path, &resolved);
    if (status != GOTH_STATUS_OK) {
        return status;
    }
    if (resolved.found) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (resolved.parent >= GOTH_VFS_MAX_NODES ||
        nodes[resolved.parent].type != GOTH_VFS_NODE_DIRECTORY) {
        return GOTH_STATUS_BAD_STATE;
    }

    if (vfs_create_node(resolved.parent, resolved.name,
                        resolved.name_length,
                        GOTH_VFS_NODE_DIRECTORY) == VFS_NO_NODE) {
        return GOTH_STATUS_FULL;
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_unlink(const char *path)
{
    vfs_resolved_t resolved;
    uint16_t node;
    uint16_t *link;
    goth_status_t status;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    status = vfs_resolve(path, &resolved);
    if (status != GOTH_STATUS_OK) {
        return status;
    }
    if (!resolved.found) {
        return GOTH_STATUS_EMPTY;
    }
    node = resolved.node;
    if (node == 0U) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (nodes[node].open_refs > 0U) {
        return GOTH_STATUS_BAD_STATE;
    }
    if (nodes[node].type == GOTH_VFS_NODE_DIRECTORY &&
        nodes[node].first_child != VFS_NO_NODE) {
        return GOTH_STATUS_BAD_STATE;
    }

    link = &nodes[nodes[node].parent].first_child;
    while (*link != VFS_NO_NODE && *link != node) {
        link = &nodes[*link].next_sibling;
    }
    if (*link == node) {
        *link = nodes[node].next_sibling;
    }

    vfs_free_blocks(nodes[node].first_block);
    vfs_zero(&nodes[node], sizeof(nodes[node]));
    return GOTH_STATUS_OK;
}

goth_status_t goth_vfs_readdir(const char *path,
                               uint32_t index,
                               goth_vfs_dirent_t *entry)
{
    vfs_resolved_t resolved;
    uint16_t child;
    uint32_t position = 0U;
    goth_status_t status;

    if (!vfs_initialized || entry == NULL) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    status = vfs_resolve(path, &resolved);
    if (status != GOTH_STATUS_OK) {
        return status;
    }
    if (!resolved.found) {
        return GOTH_STATUS_EMPTY;
    }
    if (nodes[resolved.node].type != GOTH_VFS_NODE_DIRECTORY) {
        return GOTH_STATUS_BAD_STATE;
    }

    child = nodes[resolved.node].first_child;
    while (child != VFS_NO_NODE) {
        if (position == index) {
            size_t name_length = vfs_strlen(nodes[child].name);

            if (name_length >= sizeof(entry->name)) {
                name_length = sizeof(entry->name) - 1U;
            }
            vfs_copy(entry->name, nodes[child].name, name_length);
            entry->name[name_length] = '\0';
            entry->type = nodes[child].type;
            entry->size = nodes[child].size;
            return GOTH_STATUS_OK;
        }
        position++;
        child = nodes[child].next_sibling;
    }
    return GOTH_STATUS_EMPTY;
}

goth_status_t goth_vfs_usage(uint32_t *used_blocks, uint32_t *total_blocks)
{
    uint32_t index;
    uint32_t used = 0U;

    if (!vfs_initialized) {
        return GOTH_STATUS_NOT_INITIALIZED;
    }
    for (index = 0U; index < GOTH_VFS_BLOCK_COUNT; index++) {
        if (blocks[index].used) {
            used++;
        }
    }
    if (used_blocks != NULL) {
        *used_blocks = used;
    }
    if (total_blocks != NULL) {
        *total_blocks = GOTH_VFS_BLOCK_COUNT;
    }
    return GOTH_STATUS_OK;
}
