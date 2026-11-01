#include "gothrtos/cmdline.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_argument_parsing(void)
{
    const char *arguments[] = {
        "-kern_sym=/kern.symbols",
        "-verbose=1",
        "-root=/img/root.bin",
        "-mem=dynamic",
        "--debug=0x01",
        "-flag",
        "-root=/img/other.bin",
        "plain",
        "--",
        "-empty="
    };
    goth_cmdline_t cmdline;
    uint32_t value;

    assert(goth_cmdline_parse_args(&cmdline, 10, arguments) ==
           GOTH_STATUS_OK);
    assert(cmdline.count == 9U);
    assert(strcmp(goth_cmdline_get(&cmdline, "kern_sym"),
                  "/kern.symbols") == 0);
    assert(strcmp(goth_cmdline_get(&cmdline, "root"),
                  "/img/other.bin") == 0);
    assert(strcmp(goth_cmdline_get(&cmdline, "mem"), "dynamic") == 0);
    assert(goth_cmdline_has(&cmdline, "flag"));
    assert(goth_cmdline_get(&cmdline, "flag") == NULL);
    assert(goth_cmdline_has(&cmdline, "plain"));
    assert(strcmp(goth_cmdline_get(&cmdline, "empty"), "") == 0);
    assert(!goth_cmdline_has(&cmdline, "--"));
    assert(goth_cmdline_get(&cmdline, "missing") == NULL);

    assert(goth_cmdline_get_u32(&cmdline, "verbose", &value) ==
           GOTH_STATUS_OK);
    assert(value == 1U);
    assert(goth_cmdline_get_u32(&cmdline, "debug", &value) ==
           GOTH_STATUS_OK);
    assert(value == 1U);
    assert(goth_cmdline_get_u32(&cmdline, "flag", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_get_u32(&cmdline, "empty", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_get_u32(&cmdline, "missing", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
}

static void test_numeric_parsing(void)
{
    const char *arguments[] = {
        "-decimal=4294967295",
        "-hex=0xffffffff",
        "-too_big=4294967296",
        "-hex_too_big=0x100000000",
        "-not_a_number=hello",
        "-base10_hex=0x1f"
    };
    goth_cmdline_t cmdline;
    uint32_t value;

    assert(goth_cmdline_parse_args(&cmdline, 6, arguments) ==
           GOTH_STATUS_OK);
    assert(goth_cmdline_get_u32(&cmdline, "decimal", &value) ==
           GOTH_STATUS_OK);
    assert(value == UINT32_MAX);
    assert(goth_cmdline_get_u32(&cmdline, "hex", &value) ==
           GOTH_STATUS_OK);
    assert(value == UINT32_MAX);
    assert(goth_cmdline_get_u32(&cmdline, "too_big", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_get_u32(&cmdline, "hex_too_big", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_get_u32(&cmdline, "not_a_number", &value) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_get_u32(&cmdline, "base10_hex", &value) ==
           GOTH_STATUS_OK);
    assert(value == 31U);
}

static void test_string_parsing(void)
{
    char text[] = "  -kern_sym=/kern.symbols\t-verbose=1\n"
                  "-flag -mem=dynamic  ";
    goth_cmdline_t cmdline;
    uint32_t value;

    assert(goth_cmdline_parse_string(&cmdline, text) == GOTH_STATUS_OK);
    assert(cmdline.count == 4U);
    assert(strcmp(goth_cmdline_get(&cmdline, "kern_sym"),
                  "/kern.symbols") == 0);
    assert(goth_cmdline_get_u32(&cmdline, "verbose", &value) ==
           GOTH_STATUS_OK);
    assert(value == 1U);
    assert(goth_cmdline_has(&cmdline, "flag"));
    assert(strcmp(goth_cmdline_get(&cmdline, "mem"), "dynamic") == 0);
}

static void test_invalid_and_full(void)
{
    const char *bad_arguments[] = {"-good=1", NULL};
    const char *empty_key[] = {"=value"};
    const char *separator[] = {"--"};
    const char *many[GOTH_CMDLINE_MAX_OPTIONS + 1U];
    goth_cmdline_t cmdline;
    size_t index;

    assert(goth_cmdline_parse_args(NULL, 0, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_parse_args(&cmdline, -1, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_parse_args(&cmdline, 1, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(goth_cmdline_parse_args(&cmdline, 2, bad_arguments) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(cmdline.count == 0U);
    assert(goth_cmdline_parse_args(&cmdline, 1, empty_key) ==
           GOTH_STATUS_INVALID_ARGUMENT);
    assert(cmdline.count == 0U);
    assert(goth_cmdline_parse_args(&cmdline, 1, separator) ==
           GOTH_STATUS_OK);
    assert(cmdline.count == 0U);
    assert(goth_cmdline_parse_string(NULL, NULL) ==
           GOTH_STATUS_INVALID_ARGUMENT);

    for (index = 0U; index < GOTH_CMDLINE_MAX_OPTIONS + 1U; index++) {
        many[index] = "-option";
    }
    assert(goth_cmdline_parse_args(&cmdline,
                                   (int)(GOTH_CMDLINE_MAX_OPTIONS + 1U),
                                   many) == GOTH_STATUS_FULL);
    assert(cmdline.count == 0U);
}

int main(void)
{
    test_argument_parsing();
    test_numeric_parsing();
    test_string_parsing();
    test_invalid_and_full();
    return 0;
}
