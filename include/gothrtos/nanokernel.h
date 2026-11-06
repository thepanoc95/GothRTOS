#ifndef GOTHRTOS_NANOKERNEL_H
#define GOTHRTOS_NANOKERNEL_H

#include "gothrtos/kernel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Nanokernel reinforcement layer.
 *
 * GothRTOS is a nanokernel: the core only schedules tasks, keeps time and
 * moves messages between endpoints. Everything else is policy and lives in
 * userland. That boundary is easy to state and easy to erode, so this module
 * makes it explicit and checkable:
 *
 *   - a lifecycle state machine (EARLY -> READY -> RUNNING -> HALTED/PANIC)
 *     with validated transitions, so a subsystem cannot claim to be running
 *     before the kernel is initialized;
 *   - a subsystem registry that classifies each component as kernel core,
 *     platform expert or userland policy, together with whether it is allowed
 *     to allocate dynamically;
 *   - a kernel-mode gate that the syscall dispatcher raises while it executes
 *     kernel code, so kernel-only entry points can refuse to be re-entered;
 *   - invariant checks that userland and the platform can run to prove the
 *     reinforced architecture still holds.
 *
 * Nothing here allocates: the registry is a static table. */

#define GOTH_NK_NAME "goth@nanokernel"
#define GOTH_NK_ABI_VERSION 1U

typedef enum {
    GOTH_NK_STATE_UNINITIALIZED = 0,
    GOTH_NK_STATE_EARLY,   /* log and platform probe done, scheduler not up */
    GOTH_NK_STATE_READY,   /* kernel objects exist, userland may be loaded */
    GOTH_NK_STATE_RUNNING, /* the scheduler is dispatching tasks */
    GOTH_NK_STATE_HALTED,  /* ran to completion, no tasks remain */
    GOTH_NK_STATE_PANIC    /* protection fault or unrecoverable error */
} goth_nk_state_t;

/* Where a component lives. Only GOTH_NK_SCOPE_KERNEL components are part of
 * the nanokernel proper; the others must not be reached by userland except
 * through the syscall gate. */
typedef enum {
    GOTH_NK_SCOPE_KERNEL = 0,
    GOTH_NK_SCOPE_PLATFORM,
    GOTH_NK_SCOPE_USER
} goth_nk_scope_t;

typedef struct {
    const char *name;
    goth_nk_scope_t scope;
    const char *description;
    /* Kernel core components must use static allocation only. */
    bool static_allocation;
    bool present;
} goth_nk_subsystem_t;

typedef struct {
    const char *platform;
    const char *architecture;
    uint32_t pointer_width;
    uint32_t logical_cpus;
    goth_sched_policy_t scheduler;
    uint32_t quantum;
} goth_nk_boot_params_t;

typedef bool (*goth_nk_task_visitor_t)(goth_task_t *task, void *context);
typedef void (*goth_nk_violation_t)(const char *invariant, void *context);

void goth_nk_init(void);

goth_nk_state_t goth_nk_state(void);
const char *goth_nk_state_name(goth_nk_state_t state);
/* Validates and applies a transition, returning GOTH_STATUS_BAD_STATE when the
 * transition is not allowed by the state machine. */
goth_status_t goth_nk_transition(goth_nk_state_t state);

/* Kernel-mode gate. The syscall dispatcher raises it while running kernel
 * code; kernel-only helpers assert it. The nesting counter tolerates the
 * scheduler re-entering the gate through a nested syscall. */
bool goth_nk_in_kernel(void);
void goth_nk_enter_kernel(void);
void goth_nk_leave_kernel(void);
uint64_t goth_nk_gate_entries(void);

/* Subsystem registry. */
size_t goth_nk_subsystem_count(void);
const goth_nk_subsystem_t *goth_nk_subsystem_at(size_t index);
const goth_nk_subsystem_t *goth_nk_subsystem_find(const char *name);
const char *goth_nk_scope_name(goth_nk_scope_t scope);
size_t goth_nk_scope_count(goth_nk_scope_t scope);

/* Enumerates the task list without exposing the intrusive list. The visitor
 * returns false to stop early. Returns the number of tasks visited. */
size_t goth_nk_task_foreach(goth_nk_task_visitor_t visitor, void *context);

/* Checks the reinforced-architecture invariants. Returns the number of
 * violations found and, when `report` is non-NULL, describes each one. */
size_t goth_nk_check_invariants(goth_nk_violation_t report, void *context);

/* Boot parameters captured once the platform is probed. */
void goth_nk_capture_boot(const goth_nk_boot_params_t *params);
bool goth_nk_boot_params(goth_nk_boot_params_t *out);

#endif
