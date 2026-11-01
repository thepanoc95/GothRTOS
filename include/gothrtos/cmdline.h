#ifndef GOTHRTOS_CMDLINE_H
#define GOTHRTOS_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gothrtos/kernel.h"

#define GOTH_CMDLINE_MAX_OPTIONS 32U

typedef struct {
    const char *key;
    size_t key_length;
    const char *value;
    size_t value_length;
} goth_cmdline_option_t;

typedef struct {
    goth_cmdline_option_t options[GOTH_CMDLINE_MAX_OPTIONS];
    size_t count;
} goth_cmdline_t;

void goth_cmdline_init(goth_cmdline_t *cmdline);
goth_status_t goth_cmdline_parse_args(goth_cmdline_t *cmdline,
                                      int argument_count,
                                      const char *const arguments[]);
goth_status_t goth_cmdline_parse_string(goth_cmdline_t *cmdline,
                                        char *text);
bool goth_cmdline_has(const goth_cmdline_t *cmdline, const char *key);
const char *goth_cmdline_get(const goth_cmdline_t *cmdline, const char *key);
goth_status_t goth_cmdline_get_u32(const goth_cmdline_t *cmdline,
                                   const char *key,
                                   uint32_t *value);

#endif
