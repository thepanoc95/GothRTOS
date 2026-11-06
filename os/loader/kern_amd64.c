#include "gothrtos/amd64.h"
#include "gothrtos/kernel.h"
#include "gothrtos/klog.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stddef.h>
#include <stdint.h>

/* AMD64 bare-metal entry point. The 64-bit sibling of os/loader/kern_arm.c:
   the same bring-up order and the same two demo tasks, so the two bare-metal
   ports can be compared side by side. */

#define AMD64_TASK_STACK_SIZE 8192U

static uint8_t task_a_stack[AMD64_TASK_STACK_SIZE];
static uint8_t task_b_stack[AMD64_TASK_STACK_SIZE];
static goth_task_t task_a = {0};
static goth_task_t task_b = {0};

static void print_text(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    (void)goth_pe_console_write(text, length);
}

static void print_u32(uint32_t value)
{
    char buffer[10];
    size_t position = sizeof(buffer);

    if (value == 0U) {
        print_text("0");
        return;
    }

    while (value > 0U) {
        buffer[--position] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    (void)goth_pe_console_write(&buffer[position], sizeof(buffer) - position);
}

static void task_a_entry(void *argument)
{
    uint32_t iteration;

    (void)argument;
    for (iteration = 1U; iteration <= 3U; iteration++) {
        print_text("amd64: task A iteration ");
        print_u32(iteration);
        print_text("\n");
        (void)goth_task_sleep(1U);
    }
    print_text("amd64: task A done\n");
    (void)goth_task_exit();
}

static void task_b_entry(void *argument)
{
    uint32_t iteration;

    (void)argument;
    for (iteration = 1U; iteration <= 3U; iteration++) {
        print_text("amd64: task B iteration ");
        print_u32(iteration);
        print_text("\n");
        (void)goth_task_yield();
    }
    print_text("amd64: task B done\n");
    (void)goth_task_exit();
}

static void start_task(goth_task_t *task,
                       uint8_t *stack,
                       uint8_t priority,
                       goth_task_entry_t entry)
{
    goth_status_t status = goth_task_create(task, stack, AMD64_TASK_STACK_SIZE,
                                            priority, entry, NULL);

    if (status != GOTH_STATUS_OK) {
        print_text("amd64: task creation failed: ");
        print_u32((uint32_t)status);
        print_text("\n");
        goth_amd64_exit(1);
    }
}

void kmain(void)
{
    goth_pe_info_t info;
    goth_status_t status;

    if (goth_pe_init() != GOTH_PE_STATUS_OK) {
        print_text("amd64: pexpert initialization failed\n");
        goth_amd64_exit(1);
    }

    goth_klog_init();
    goth_klog_set_console_level(GOTH_LOG_INFO);

    goth_klog_printf(GOTH_LOG_INFO, "GothRTOS AMD64 bare-metal boot");
    if (goth_pe_get_info(&info) != GOTH_PE_STATUS_OK) {
        goth_klog_printf(GOTH_LOG_ERR,
                         "amd64: cannot query platform information");
        goth_amd64_exit(1);
    }

    goth_klog_printf(GOTH_LOG_INFO, "platform: %s", info.platform_name);
    goth_klog_printf(GOTH_LOG_INFO,
                     "architecture: %s, %u-bit, cpus: %u",
                     info.architecture, (unsigned int)info.pointer_width,
                     (unsigned int)info.logical_cpu_count);

    goth_kernel_init();
    if (!goth_kernel_is_initialized()) {
        print_text("amd64: kernel initialization failed\n");
        goth_amd64_exit(1);
    }

    start_task(&task_a, task_a_stack, 2U, task_a_entry);
    start_task(&task_b, task_b_stack, 1U, task_b_entry);

    goth_klog_printf(GOTH_LOG_INFO, "amd64: scheduler starting");
    status = goth_um_run(1U);
    if (status != GOTH_STATUS_OK) {
        goth_klog_printf(GOTH_LOG_ERR, "amd64: scheduler failed: %u",
                         (unsigned int)status);
        goth_amd64_exit(1);
    }

    goth_klog_printf(GOTH_LOG_INFO, "amd64: kernel halted after %llu tick(s)",
                     (unsigned long long)goth_kernel_ticks());
    goth_amd64_exit(0);
}
