#include "gothrtos/arm32.h"
#include "gothrtos/kernel.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stddef.h>
#include <stdint.h>

#define ARM32_TASK_STACK_SIZE 4096U

static uint8_t task_a_stack[ARM32_TASK_STACK_SIZE];
static uint8_t task_b_stack[ARM32_TASK_STACK_SIZE];
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

static void print_u64(uint64_t value)
{
    char buffer[20];
    size_t position = sizeof(buffer);

    if (value == 0U) {
        print_text("0");
        return;
    }

    while (value > 0U) {
        buffer[--position] = (char)('0' + (uint32_t)(value % 10U));
        value /= 10U;
    }
    (void)goth_pe_console_write(&buffer[position], sizeof(buffer) - position);
}

static void task_a_entry(void *argument)
{
    uint32_t iteration;

    (void)argument;
    for (iteration = 1U; iteration <= 3U; iteration++) {
        print_text("arm32: task A iteration ");
        print_u32(iteration);
        print_text("\n");
        (void)goth_task_sleep(1U);
    }
    print_text("arm32: task A done\n");
    (void)goth_task_exit();
}

static void task_b_entry(void *argument)
{
    uint32_t iteration;

    (void)argument;
    for (iteration = 1U; iteration <= 3U; iteration++) {
        print_text("arm32: task B iteration ");
        print_u32(iteration);
        print_text("\n");
        (void)goth_task_yield();
    }
    print_text("arm32: task B done\n");
    (void)goth_task_exit();
}

static void start_task(goth_task_t *task,
                       uint8_t *stack,
                       uint8_t priority,
                       goth_task_entry_t entry)
{
    goth_status_t status = goth_task_create(task, stack, ARM32_TASK_STACK_SIZE,
                                            priority, entry, NULL);

    if (status != GOTH_STATUS_OK) {
        print_text("arm32: task creation failed: ");
        print_u32((uint32_t)status);
        print_text("\n");
        goth_arm32_exit(1);
    }
}

void kmain(void)
{
    goth_pe_info_t info;
    goth_status_t status;

    /* Initialize the UART before the first write: goth_pe_console_write does
       not touch hardware until the platform ops are initialized. */
    if (goth_pe_init() != GOTH_PE_STATUS_OK) {
        print_text("arm32: pexpert initialization failed\n");
        goth_arm32_exit(1);
    }

    print_text("\nGothRTOS ARM32 bare-metal boot\n");

    if (goth_pe_get_info(&info) != GOTH_PE_STATUS_OK) {
        print_text("arm32: cannot query platform information\n");
        goth_arm32_exit(1);
    }

    print_text("platform: ");
    print_text(info.platform_name);
    print_text("\narchitecture: ");
    print_text(info.architecture);
    print_text(", ");
    print_u32(info.pointer_width);
    print_text("-bit, cpus: ");
    print_u32(info.logical_cpu_count);
    print_text("\n");

    goth_kernel_init();
    if (!goth_kernel_is_initialized()) {
        print_text("arm32: kernel initialization failed\n");
        goth_arm32_exit(1);
    }

    start_task(&task_a, task_a_stack, 2U, task_a_entry);
    start_task(&task_b, task_b_stack, 1U, task_b_entry);

    print_text("arm32: scheduler starting\n");
    status = goth_um_run(1U);
    if (status != GOTH_STATUS_OK) {
        print_text("arm32: scheduler failed: ");
        print_u32((uint32_t)status);
        print_text("\n");
        goth_arm32_exit(1);
    }

    print_text("arm32: kernel halted after ");
    print_u64(goth_kernel_ticks());
    print_text(" tick(s)\n");
    goth_arm32_exit(0);
}
