#include "gothrtos/cmdline.h"
#include "gothrtos/kernel.h"
#include "gothrtos/luaexec.h"
#include "gothrtos/pexpert.h"
#include "gothrtos/um.h"

#include <stdio.h>
#include <string.h>

#define KERNEL_BOOT_STACK_SIZE 8192U
#define KERNEL_CMDLINE_BUFFER_SIZE 1024U
#define KERNEL_DEFAULT_INIT "os/init/init.d/init.lua"

static uint8_t boot_stack[KERNEL_BOOT_STACK_SIZE];
static goth_task_t boot_task = {0};
static char cmdline_buffer[KERNEL_CMDLINE_BUFFER_SIZE];
static bool kernel_verbose;

static void print_usage(const char *program)
{
    printf("usage: %s [-option[=value] ...]\n", program);
    printf("  -kern_sym=PATH   symbol file path\n");
    printf("  -root=PATH       root image path\n");
    printf("  -drv_root=PATH   driver root path\n");
    printf("  -mem=MODE        memory policy\n");
    printf("  -init[=PATH]     run a Lua init script (default: %s)\n",
           KERNEL_DEFAULT_INIT);
    printf("  -verbose[=N]     enable or set verbosity\n");
    printf("  -debug[=MASK]    enable or set debug mask\n");
    printf("  -help            show this help\n");
}

static const char *lua_status_name(goth_lua_status_t status)
{
    switch (status) {
    case GOTH_LUA_OK:
        return "ok";
    case GOTH_LUA_INVALID_ARGUMENT:
        return "invalid argument";
    case GOTH_LUA_OUT_OF_MEMORY:
        return "out of memory";
    case GOTH_LUA_COMPILE_ERROR:
        return "compile error";
    case GOTH_LUA_RUNTIME_ERROR:
        return "runtime error";
    default:
        return "unknown error";
    }
}

static bool run_init_script(const goth_cmdline_t *cmdline)
{
    const char *script = goth_cmdline_get(cmdline, "init");
    const char *lua_arguments[GOTH_CMDLINE_MAX_OPTIONS];
    goth_lua_status_t lua_status;
    size_t index;

    if (!goth_cmdline_has(cmdline, "init")) {
        return true;
    }
    if (script == NULL) {
        script = KERNEL_DEFAULT_INIT;
    }

    /* Each parsed token is null-terminated and still carries its "key=value"
       spelling, so Lua sees the same options regardless of whether the boot
       loader passed separate arguments or one command-line string. */
    for (index = 0U; index < cmdline->count; index++) {
        lua_arguments[index] = cmdline->options[index].key;
    }

    if (kernel_verbose) {
        printf("kern.ldr: running init script %s\n", script);
    }
    lua_status = goth_lua_exec_file(script, (int)cmdline->count, lua_arguments);
    if (lua_status != GOTH_LUA_OK) {
        fprintf(stderr, "kern.ldr: init script %s failed: %s\n",
                script, lua_status_name(lua_status));
        return false;
    }
    return true;
}

static bool parse_command_line(goth_cmdline_t *cmdline,
                               int argument_count,
                               char *arguments[])
{
    goth_status_t status;

    if (argument_count <= 1) {
        goth_cmdline_init(cmdline);
        return true;
    }

    if (argument_count == 2 &&
        strpbrk(arguments[1], " \t\r\n") != NULL) {
        size_t length = strlen(arguments[1]);

        if (length >= sizeof(cmdline_buffer)) {
            fprintf(stderr, "kern.ldr: kernel command line is too long\n");
            return false;
        }
        memcpy(cmdline_buffer, arguments[1], length + 1U);
        status = goth_cmdline_parse_string(cmdline, cmdline_buffer);
    } else {
        status = goth_cmdline_parse_args(
            cmdline,
            argument_count - 1,
            (const char *const *)(arguments + 1));
    }

    if (status != GOTH_STATUS_OK) {
        fprintf(stderr, "kern.ldr: invalid kernel command line (%d)\n",
                (int)status);
        return false;
    }
    return true;
}

static bool read_optional_u32(const goth_cmdline_t *cmdline,
                              const char *key,
                              uint32_t *value)
{
    if (!goth_cmdline_has(cmdline, key)) {
        return true;
    }

    if (goth_cmdline_get(cmdline, key) == NULL) {
        *value = 1U;
        return true;
    }
    if (goth_cmdline_get_u32(cmdline, key, value) != GOTH_STATUS_OK) {
        fprintf(stderr, "kern.ldr: invalid value for -%s\n", key);
        return false;
    }
    return true;
}

static void boot_task_entry(void *argument)
{
    const goth_cmdline_t *cmdline = (const goth_cmdline_t *)argument;

    if (kernel_verbose) {
        printf("kern.ldr: boot task started with %zu option(s)\n",
               cmdline->count);
    }
    (void)goth_task_exit();
}

int main(int argument_count, char *arguments[])
{
    goth_cmdline_t cmdline;
    goth_pe_info_t platform_info;
    goth_pe_status_t platform_status;
    goth_status_t status;
    uint32_t verbose = 0U;
    uint32_t debug = 0U;
    size_t index;

    if (!parse_command_line(&cmdline, argument_count, arguments)) {
        return 1;
    }

    if (goth_cmdline_has(&cmdline, "help") ||
        goth_cmdline_has(&cmdline, "h")) {
        print_usage(argument_count > 0 ? arguments[0] : "kern.ldr");
        return 0;
    }

    if (!read_optional_u32(&cmdline, "verbose", &verbose) ||
        !read_optional_u32(&cmdline, "debug", &debug)) {
        return 1;
    }
    kernel_verbose = verbose > 0U;

    platform_status = goth_pe_init();
    if (platform_status != GOTH_PE_STATUS_OK ||
        goth_pe_get_info(&platform_info) != GOTH_PE_STATUS_OK) {
        fprintf(stderr, "kern.ldr: platform initialization failed\n");
        return 1;
    }

    printf("GothRTOS kernel: %s, %s, %u-bit\n",
           platform_info.platform_name,
           platform_info.architecture,
           (unsigned int)platform_info.pointer_width);

    if (kernel_verbose) {
        printf("kern.ldr: %u logical CPU(s), debug mask 0x%x\n",
               (unsigned int)platform_info.logical_cpu_count,
               (unsigned int)debug);
        for (index = 0U; index < cmdline.count; index++) {
            const goth_cmdline_option_t *option = &cmdline.options[index];

            if (option->value != NULL) {
                printf("kern.ldr: option %.*s=%.*s\n",
                       (int)option->key_length, option->key,
                       (int)option->value_length, option->value);
            } else {
                printf("kern.ldr: option %.*s\n",
                       (int)option->key_length, option->key);
            }
        }
    }

    goth_kernel_init();
    if (!goth_kernel_is_initialized()) {
        fprintf(stderr, "kern.ldr: kernel initialization failed\n");
        return 1;
    }

    if (!run_init_script(&cmdline)) {
        return 1;
    }

    status = goth_task_create(&boot_task, boot_stack, sizeof(boot_stack),
                              1U, boot_task_entry, &cmdline);
    if (status != GOTH_STATUS_OK) {
        fprintf(stderr, "kern.ldr: could not start boot task (%d)\n",
                (int)status);
        return 1;
    }

    status = goth_um_run(1U);
    if (status != GOTH_STATUS_OK) {
        fprintf(stderr, "kern.ldr: kernel run loop failed (%d)\n",
                (int)status);
        return 1;
    }

    if (kernel_verbose) {
        printf("kern.ldr: halted after %llu tick(s)\n",
               (unsigned long long)goth_kernel_ticks());
    }
    return 0;
}
