#ifndef GOTHRTOS_MEM_H
#define GOTHRTOS_MEM_H

#include "gothrtos/kernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Basic memory protection for the single-address-space nanokernel.
 *
 * GothRTOS does not require an MMU, so protection is cooperative and
 * bookkeeping-based:
 *
 *   - every kernel object that userland can reach is registered with an
 *     address range, a permission mask and an owner;
 *   - the syscall gate refuses ranges that overlap a kernel-only region
 *     without the required permission;
 *   - task stacks carry a canary, and the Linux user-mode port additionally
 *     puts a PROT_NONE guard page under the host stack.
 *
 * This is deliberately basic: it catches the common corruption paths without
 * pretending to be full virtual memory. */

#define GOTH_MEM_MAX_REGIONS 96U
#define GOTH_STACK_GUARD_WORD UINT32_C(0x474F5448) /* "GOTH" */
#define GOTH_STACK_GUARD_XOR UINT32_C(0xA5A5A5A5)

typedef enum {
    GOTH_MEM_READ = 1U << 0,
    GOTH_MEM_WRITE = 1U << 1,
    GOTH_MEM_EXECUTE = 1U << 2,
    /* The range may be referenced by userland. Kernel ranges without this
     * bit are off limits to syscall buffers. */
    GOTH_MEM_USER_ACCESS = 1U << 3
} goth_mem_flags_t;

typedef enum {
    GOTH_MEM_KERNEL = 0,
    GOTH_MEM_USER,
    GOTH_MEM_DEVICE
} goth_mem_kind_t;

typedef struct {
    uintptr_t base;
    size_t size;
    uint32_t flags;
    goth_mem_kind_t kind;
    goth_task_id_t owner;
    const char *name;
} goth_mem_region_t;

void goth_mem_init(void);
bool goth_mem_is_initialized(void);

goth_status_t goth_mem_register(void *base,
                                size_t size,
                                uint32_t flags,
                                goth_mem_kind_t kind,
                                goth_task_id_t owner,
                                const char *name);
goth_status_t goth_mem_unregister(void *base);
goth_status_t goth_mem_protect(void *base, uint32_t flags);

size_t goth_mem_region_count(void);
size_t goth_mem_region_capacity(void);
const goth_mem_region_t *goth_mem_region_at(size_t index);

/* True when [pointer, pointer+length) is fully covered by registered ranges
 * that grant `required`. Untracked memory is accepted by this generic check,
 * which is what the scheduler and the kernel's own helpers want. */
bool goth_mem_check_range(const void *pointer,
                          size_t length,
                          uint32_t required);

/* Stricter boundary check for the syscall gate: a range that overlaps any
 * kernel/device region without GOTH_MEM_USER_ACCESS is rejected, and a range
 * overlaps a user region must satisfy `required`. Untracked user memory (Lua
 * strings, libc heap, caller stacks in the test harness) is allowed. */
bool goth_mem_check_user(const void *pointer,
                         size_t length,
                         uint32_t required);

/* Registers a task stack as a user range and paints the platform canary. */
goth_status_t goth_mem_guard_task(goth_task_t *task);
void goth_mem_forget_task(goth_task_id_t id);
bool goth_mem_check_task_stack(const goth_task_t *task);

uint64_t goth_mem_violations(void);
void goth_mem_note_violation(const char *reason);

#endif
