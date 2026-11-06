#include "gothrtos/nanokernel.h"

#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/pexpert.h"
#include "kinternal.h"

/* Local freestanding string helper: rtos/ is shared with the bare-metal
 * builds, which do not link a C library. */
static bool nk_equals(const char *first, const char *second)
{
    if (first == NULL || second == NULL) {
        return first == second;
    }
    while (*first != '\0' && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

/* Subsystem registry.
 *
 * The list is the machine-readable statement of the architecture: which
 * components form the nanokernel core (static allocation, no policy) and
 * which are platform or userland policy layered on top. It is deliberately a
 * plain table so it can be audited and enumerated without allocation. */
static const goth_nk_subsystem_t subsystems[] = {
    {"nanokernel", GOTH_NK_SCOPE_KERNEL, "lifecycle, invariants and mode gate",
     true, true},
    {"scheduler", GOTH_NK_SCOPE_KERNEL, "task selection and time", true, true},
    {"ipc", GOTH_NK_SCOPE_KERNEL, "endpoint message queues", true, true},
    {"syscall", GOTH_NK_SCOPE_KERNEL, "the only userland entry gate", true,
     true},
    {"memory", GOTH_NK_SCOPE_KERNEL, "region bookkeeping and stack guards", true,
     true},
    {"vfs", GOTH_NK_SCOPE_KERNEL, "RAM filesystem", true, true},
    {"klog", GOTH_NK_SCOPE_KERNEL, "kernel log ring", true, true},
    {"hdf", GOTH_NK_SCOPE_KERNEL, "hardware driver foundation", true, false},
    {"pexpert", GOTH_NK_SCOPE_PLATFORM, "platform expert and board ports", true,
     true},
    {"userapi", GOTH_NK_SCOPE_USER, "console, identity, process and env policy",
     true, false},
    {"userkernel", GOTH_NK_SCOPE_USER, "kernel modes, profiles and services",
     false, false}
};

#define GOTH_NK_SUBSYSTEM_COUNT \
    (sizeof(subsystems) / sizeof(subsystems[0]))

static goth_nk_state_t nk_state;
static uint32_t kernel_gate_depth;
static uint64_t gate_entries;
static goth_nk_boot_params_t boot_params;
static bool boot_captured;

static bool transition_allowed(goth_nk_state_t from, goth_nk_state_t to)
{
    switch (from) {
    case GOTH_NK_STATE_UNINITIALIZED:
        return to == GOTH_NK_STATE_EARLY;
    case GOTH_NK_STATE_EARLY:
        return to == GOTH_NK_STATE_READY || to == GOTH_NK_STATE_PANIC;
    case GOTH_NK_STATE_READY:
        return to == GOTH_NK_STATE_RUNNING || to == GOTH_NK_STATE_PANIC;
    case GOTH_NK_STATE_RUNNING:
        return to == GOTH_NK_STATE_HALTED || to == GOTH_NK_STATE_PANIC ||
               to == GOTH_NK_STATE_READY;
    case GOTH_NK_STATE_HALTED:
        return to == GOTH_NK_STATE_READY || to == GOTH_NK_STATE_PANIC;
    case GOTH_NK_STATE_PANIC:
        return false;
    default:
        return false;
    }
}

void goth_nk_init(void)
{
    nk_state = GOTH_NK_STATE_UNINITIALIZED;
    kernel_gate_depth = 0U;
    gate_entries = 0U;
    boot_captured = false;
}

goth_nk_state_t goth_nk_state(void)
{
    return nk_state;
}

const char *goth_nk_state_name(goth_nk_state_t state)
{
    switch (state) {
    case GOTH_NK_STATE_UNINITIALIZED:
        return "uninitialized";
    case GOTH_NK_STATE_EARLY:
        return "early";
    case GOTH_NK_STATE_READY:
        return "ready";
    case GOTH_NK_STATE_RUNNING:
        return "running";
    case GOTH_NK_STATE_HALTED:
        return "halted";
    case GOTH_NK_STATE_PANIC:
        return "panic";
    default:
        return "unknown";
    }
}

goth_status_t goth_nk_transition(goth_nk_state_t state)
{
    if (state == nk_state) {
        return GOTH_STATUS_OK;
    }
    if (!transition_allowed(nk_state, state)) {
        goth_klog_printf(GOTH_LOG_WARNING,
                         "nanokernel: refused transition %s -> %s",
                         goth_nk_state_name(nk_state),
                         goth_nk_state_name(state));
        return GOTH_STATUS_BAD_STATE;
    }

    goth_klog_printf(GOTH_LOG_INFO, "nanokernel: state %s -> %s",
                     goth_nk_state_name(nk_state),
                     goth_nk_state_name(state));
    nk_state = state;
    return GOTH_STATUS_OK;
}

bool goth_nk_in_kernel(void)
{
    return kernel_gate_depth != 0U;
}

void goth_nk_enter_kernel(void)
{
    if (kernel_gate_depth == 0U) {
        gate_entries++;
    }
    kernel_gate_depth++;
}

void goth_nk_leave_kernel(void)
{
    if (kernel_gate_depth != 0U) {
        kernel_gate_depth--;
    }
}

uint64_t goth_nk_gate_entries(void)
{
    return gate_entries;
}

size_t goth_nk_subsystem_count(void)
{
    return GOTH_NK_SUBSYSTEM_COUNT;
}

const goth_nk_subsystem_t *goth_nk_subsystem_at(size_t index)
{
    if (index >= GOTH_NK_SUBSYSTEM_COUNT) {
        return NULL;
    }
    return &subsystems[index];
}

const goth_nk_subsystem_t *goth_nk_subsystem_find(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < GOTH_NK_SUBSYSTEM_COUNT; index++) {
        if (nk_equals(subsystems[index].name, name)) {
            return &subsystems[index];
        }
    }
    return NULL;
}

const char *goth_nk_scope_name(goth_nk_scope_t scope)
{
    switch (scope) {
    case GOTH_NK_SCOPE_KERNEL:
        return "kernel";
    case GOTH_NK_SCOPE_PLATFORM:
        return "platform";
    case GOTH_NK_SCOPE_USER:
        return "user";
    default:
        return "unknown";
    }
}

size_t goth_nk_scope_count(goth_nk_scope_t scope)
{
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < GOTH_NK_SUBSYSTEM_COUNT; index++) {
        if (subsystems[index].scope == scope) {
            count++;
        }
    }
    return count;
}

size_t goth_nk_task_foreach(goth_nk_task_visitor_t visitor, void *context)
{
    goth_task_t *task;
    size_t visited = 0U;

    if (visitor == NULL) {
        return 0U;
    }
    for (task = goth_sched_task_list(); task != NULL; task = task->next) {
        if (task->state == GOTH_TASK_UNUSED) {
            continue;
        }
        visited++;
        if (!visitor(task, context)) {
            break;
        }
    }
    return visited;
}

size_t goth_nk_check_invariants(goth_nk_violation_t report, void *context)
{
    size_t violations = 0U;

    /* 1. The kernel core must never contain a component that allocates
     *    dynamically: static allocation is what makes the nanokernel
     *    predictable on bare metal. */
    {
        size_t index;

        for (index = 0U; index < GOTH_NK_SUBSYSTEM_COUNT; index++) {
            const goth_nk_subsystem_t *entry = &subsystems[index];

            if (entry->scope == GOTH_NK_SCOPE_KERNEL &&
                !entry->static_allocation) {
                violations++;
                if (report != NULL) {
                    report("kernel subsystem allocates dynamically",
                           context);
                }
            }
        }
    }

    /* 2. The kernel log must be live once the kernel is past EARLY. */
    if (nk_state >= GOTH_NK_STATE_READY && !goth_klog_is_initialized()) {
        violations++;
        if (report != NULL) {
            report("kernel log not initialized", context);
        }
    }

    /* 3. Userland memory policy must not own kernel ranges. */
    {
        size_t index;

        for (index = 0U; index < goth_mem_region_capacity(); index++) {
            const goth_mem_region_t *region = goth_mem_region_at(index);

            if (region == NULL || region->size == 0U) {
                continue;
            }
            if (region->kind == GOTH_MEM_KERNEL &&
                (region->flags & GOTH_MEM_USER_ACCESS) != 0U) {
                violations++;
                if (report != NULL) {
                    report("kernel region exposed to userland", context);
                }
            }
        }
    }

    return violations;
}

void goth_nk_capture_boot(const goth_nk_boot_params_t *params)
{
    if (params == NULL) {
        return;
    }
    boot_params = *params;
    boot_captured = true;
}

bool goth_nk_boot_params(goth_nk_boot_params_t *out)
{
    if (out == NULL || !boot_captured) {
        return false;
    }
    *out = boot_params;
    return true;
}
