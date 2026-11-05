#ifndef GOTHRTOS_KAPI_H
#define GOTHRTOS_KAPI_H

#include "gothrtos/kernel.h"
#include "gothrtos/vfs.h"

typedef struct {
    const char *name;
    goth_task_t *task;
    void *stack;
    size_t stack_size;
    uint8_t priority;
    goth_task_entry_t entry;
    void *argument;
} goth_kapi_program_t;

goth_status_t goth_kapi_yield(goth_task_id_t *next_task);
goth_status_t goth_kapi_sleep(uint32_t ticks, goth_task_id_t *next_task);
goth_status_t goth_kapi_exit(goth_task_id_t *next_task);
goth_status_t goth_kapi_get_ticks(uint64_t *ticks);
goth_status_t goth_kapi_self(goth_task_id_t *self);
goth_status_t goth_kapi_set_preemptive(bool enabled);
goth_status_t goth_kapi_is_preemptive(bool *enabled);
goth_status_t goth_kapi_set_quantum(uint32_t ticks);
goth_status_t goth_kapi_get_quantum(uint32_t *ticks);
goth_status_t goth_kapi_endpoint_send(goth_endpoint_t *endpoint,
                                       uint32_t type,
                                       uintptr_t value0,
                                       uintptr_t value1);
goth_status_t goth_kapi_endpoint_receive(goth_endpoint_t *endpoint,
                                          goth_message_t *message);
goth_status_t goth_kapi_endpoint_poll(goth_endpoint_t *endpoint,
                                       goth_message_t *message);

goth_status_t goth_kapi_start_programs(const goth_kapi_program_t *programs,
                                       size_t program_count,
                                       size_t *started_count);

goth_status_t goth_kapi_vfs_open(const char *path,
                                 uint32_t flags,
                                 uint32_t *fd);
goth_status_t goth_kapi_vfs_close(uint32_t fd);
goth_status_t goth_kapi_vfs_read(uint32_t fd,
                                 void *buffer,
                                 size_t capacity,
                                 size_t *read_length);
goth_status_t goth_kapi_vfs_write(uint32_t fd,
                                  const void *data,
                                  size_t length,
                                  size_t *written);
goth_status_t goth_kapi_vfs_seek(uint32_t fd,
                                 int64_t offset,
                                 goth_vfs_whence_t whence,
                                 uint64_t *position);
goth_status_t goth_kapi_vfs_stat(const char *path, goth_vfs_stat_t *stat);
goth_status_t goth_kapi_vfs_mkdir(const char *path);
goth_status_t goth_kapi_vfs_unlink(const char *path);
goth_status_t goth_kapi_vfs_readdir(const char *path,
                                    uint32_t index,
                                    goth_vfs_dirent_t *entry);

#endif
