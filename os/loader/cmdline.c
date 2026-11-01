#include "gothrtos/cmdline.h"

#include <string.h>

static bool is_space(char character)
{
    return character == ' ' || character == '\t' ||
           character == '\r' || character == '\n';
}

static goth_status_t add_option(goth_cmdline_t *cmdline, const char *token)
{
    goth_cmdline_option_t *option;
    const char *key = token;
    const char *equals;
    size_t key_length;

    while (*key == '-') {
        key++;
    }
    if (*key == '\0') {
        return GOTH_STATUS_OK;
    }

    equals = strchr(key, '=');
    key_length = equals != NULL ? (size_t)(equals - key) : strlen(key);
    if (key_length == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (cmdline->count >= GOTH_CMDLINE_MAX_OPTIONS) {
        return GOTH_STATUS_FULL;
    }

    option = &cmdline->options[cmdline->count++];
    option->key = key;
    option->key_length = key_length;
    if (equals != NULL) {
        option->value = equals + 1;
        option->value_length = strlen(option->value);
    } else {
        option->value = NULL;
        option->value_length = 0U;
    }
    return GOTH_STATUS_OK;
}

void goth_cmdline_init(goth_cmdline_t *cmdline)
{
    if (cmdline == NULL) {
        return;
    }
    cmdline->count = 0U;
}

goth_status_t goth_cmdline_parse_args(goth_cmdline_t *cmdline,
                                      int argument_count,
                                      const char *const arguments[])
{
    int index;

    if (cmdline == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (argument_count < 0 ||
        (argument_count > 0 && arguments == NULL)) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    goth_cmdline_init(cmdline);
    for (index = 0; index < argument_count; index++) {
        goth_status_t status;

        if (arguments[index] == NULL) {
            cmdline->count = 0U;
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
        status = add_option(cmdline, arguments[index]);
        if (status != GOTH_STATUS_OK) {
            cmdline->count = 0U;
            return status;
        }
    }
    return GOTH_STATUS_OK;
}

goth_status_t goth_cmdline_parse_string(goth_cmdline_t *cmdline, char *text)
{
    char *cursor;

    if (cmdline == NULL || text == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    goth_cmdline_init(cmdline);
    cursor = text;
    while (*cursor != '\0') {
        char *token;
        goth_status_t status;

        while (is_space(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        token = cursor;
        while (*cursor != '\0' && !is_space(*cursor)) {
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }

        status = add_option(cmdline, token);
        if (status != GOTH_STATUS_OK) {
            cmdline->count = 0U;
            return status;
        }
    }
    return GOTH_STATUS_OK;
}

static const goth_cmdline_option_t *find_option(
    const goth_cmdline_t *cmdline,
    const char *key)
{
    size_t key_length;
    size_t index;

    if (cmdline == NULL || key == NULL) {
        return NULL;
    }

    key_length = strlen(key);
    for (index = cmdline->count; index > 0U; index--) {
        const goth_cmdline_option_t *option = &cmdline->options[index - 1U];

        if (option->key_length == key_length &&
            memcmp(option->key, key, key_length) == 0) {
            return option;
        }
    }
    return NULL;
}

bool goth_cmdline_has(const goth_cmdline_t *cmdline, const char *key)
{
    return find_option(cmdline, key) != NULL;
}

const char *goth_cmdline_get(const goth_cmdline_t *cmdline, const char *key)
{
    const goth_cmdline_option_t *option = find_option(cmdline, key);

    return option != NULL ? option->value : NULL;
}

goth_status_t goth_cmdline_get_u32(const goth_cmdline_t *cmdline,
                                   const char *key,
                                   uint32_t *value)
{
    const goth_cmdline_option_t *option = find_option(cmdline, key);
    const char *cursor;
    size_t remaining;
    uint32_t result = 0U;
    uint32_t base = 10U;

    if (option == NULL || value == NULL || option->value == NULL) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    cursor = option->value;
    remaining = option->value_length;
    if (remaining == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }
    if (*cursor == '+') {
        cursor++;
        remaining--;
    }
    if (remaining >= 2U && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X')) {
        base = 16U;
        cursor += 2;
        remaining -= 2U;
    }
    if (remaining == 0U) {
        return GOTH_STATUS_INVALID_ARGUMENT;
    }

    while (remaining > 0U) {
        uint32_t digit;

        if (*cursor >= '0' && *cursor <= '9') {
            digit = (uint32_t)(*cursor - '0');
        } else if (base == 16U && *cursor >= 'a' && *cursor <= 'f') {
            digit = (uint32_t)(*cursor - 'a') + 10U;
        } else if (base == 16U && *cursor >= 'A' && *cursor <= 'F') {
            digit = (uint32_t)(*cursor - 'A') + 10U;
        } else {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
        if (digit >= base) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
        if (result > (UINT32_MAX - digit) / base) {
            return GOTH_STATUS_INVALID_ARGUMENT;
        }
        result = result * base + digit;
        cursor++;
        remaining--;
    }

    *value = result;
    return GOTH_STATUS_OK;
}
