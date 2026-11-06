#include "gothrtos/kernel.h"

#include "gothrtos/hdf.h"
#include "gothrtos/klog.h"
#include "gothrtos/mem.h"
#include "gothrtos/nanokernel.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/vfs.h"
#include "kinternal.h"
#include "um_internal.h"

/* Nanokernel lifecycle.
 *
 * The nanokernel is deliberately small: kernel.c wires the platform port to
 * the scheduler, memory protection and the RAM VFS, while sched.c owns tasks
 * and time, ipc.c owns endpoints and syscall.c is the only gate between
 * userland and the kernel. All operating system policy (console, users,
 * environment, process table, mount layout) lives in the userland layer. */

#define GOTH_KERNEL_VERSION "0.0-indev"

static bool kernel_initialized;
static bool kernel_panicked;

static void log_platform(void)
{
    goth_pe_info_t info;

    if (goth_pe_get_info(&info) != GOTH_PE_STATUS_OK) {
        goth_klog_printf(GOTH_LOG_NOTICE,
                         "Platform: unknown (pexpert not initialized)");
        return;
    }

    goth_klog_printf(GOTH_LOG_INFO,
                     "Platform: %s, %s, %u-bit, %u logical CPU(s)",
                     info.platform_name, info.architecture,
                     (unsigned int)info.pointer_width,
                     (unsigned int)info.logical_cpu_count);
}

void goth_kernel_init(void)
{
    kernel_panicked = false;

    if (goth_um_is_running()) {
        return;
    }

    goth_nk_init();
    goth_klog_init();
    goth_klog_printf(GOTH_LOG_INFO,
                     "GothRTOS version %s (%s) #1",
                     GOTH_KERNEL_VERSION, GOTH_NK_NAME);
    goth_nk_transition(GOTH_NK_STATE_EARLY);
    log_platform();

    if (goth_um_platform_reset(goth_sched_task_list()) != GOTH_STATUS_OK) {
        goth_klog_printf(GOTH_LOG_ERR,
                         "Cannot reset the platform scheduler state");
        return;
    }

    goth_sched_reset();
    goth_mem_init();
    goth_klog_register_memory();
    goth_vfs_init();
    goth_nk_transition(GOTH_NK_STATE_READY);

    goth_klog_printf(GOTH_LOG_INFO,
                     "Scheduler: %s, quantum=%u tick(s)",
                     goth_kernel_is_preemptive() ? "preemptive"
                                                 : "cooperative",
                     (unsigned int)goth_kernel_quantum());
    goth_klog_printf(GOTH_LOG_INFO,
                     "Memory protection: %zu region(s), stack guards active",
                     goth_mem_region_count());
    goth_klog_printf(GOTH_LOG_INFO,
                     "RAM VFS: %s mounted at %s (%u blocks of %u bytes)",
                     goth_vfs_backend_name(), goth_vfs_mount_point(),
                     (unsigned int)GOTH_VFS_BLOCK_COUNT,
                     (unsigned int)GOTH_VFS_BLOCK_SIZE);

    if (goth_um_platform_init() != GOTH_STATUS_OK) {
        goth_klog_printf(GOTH_LOG_ERR,
                         "Platform expert initialization failed");
        return;
    }

    /* Hardware Driver Foundation: initialize the device model, let the
     * platform port publish its devices, then bind drivers and start them.
     * The kernel core owns the HDF; drivers are the only code that touches
     * device registers. */
    goth_hdf_init();
    (void)goth_pe_register_hardware();
    (void)goth_hdf_probe_all();

    {
        goth_pe_info_t info;
        goth_nk_boot_params_t params;

        if (goth_pe_get_info(&info) != GOTH_PE_STATUS_OK) {
            info.platform_name = "unknown";
            info.architecture = "unknown";
            info.pointer_width = 0U;
            info.logical_cpu_count = 0U;
        }
        params.platform = info.platform_name;
        params.architecture = info.architecture;
        params.pointer_width = info.pointer_width;
        params.logical_cpus = info.logical_cpu_count;
        params.scheduler = goth_kernel_get_scheduler();
        params.quantum = goth_kernel_quantum();
        goth_nk_capture_boot(&params);
    }

    kernel_initialized = true;
    goth_klog_printf(GOTH_LOG_INFO,
                     "Nanokernel: %zu subsystem(s), %zu kernel, %zu platform, "
                     "%zu userland",
                     goth_nk_subsystem_count(),
                     goth_nk_scope_count(GOTH_NK_SCOPE_KERNEL),
                     goth_nk_scope_count(GOTH_NK_SCOPE_PLATFORM),
                     goth_nk_scope_count(GOTH_NK_SCOPE_USER));
    goth_klog_printf(GOTH_LOG_INFO,
                     "Kernel initialized; %zu task slot(s), %llu tick(s)",
                     goth_kernel_task_count(),
                     (unsigned long long)goth_kernel_ticks());
}

bool goth_kernel_is_initialized(void)
{
    return kernel_initialized;
}

void goth_kernel_panic(const char *reason)
{
    if (kernel_panicked) {
        return;
    }
    kernel_panicked = true;
    goth_nk_transition(GOTH_NK_STATE_PANIC);
    goth_klog_printf(GOTH_LOG_EMERG, "%s",
                     reason != NULL ? reason : "unknown kernel fault");
    goth_klog_printf(GOTH_LOG_EMERG,
                     "Kernel halted; scheduling is disabled");
}

bool goth_kernel_panicked(void)
{
    return kernel_panicked;
}
