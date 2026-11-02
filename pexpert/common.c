#include "pe_internal.h"

#include <stdbool.h>

static const goth_pe_platform_ops_t *platform_ops;
static goth_pe_info_t platform_info;
static bool initialized;

goth_pe_status_t goth_pe_init(void)
{
    goth_pe_status_t status;

    if (initialized) {
        return GOTH_PE_STATUS_OK;
    }

    platform_ops = goth_pe_platform_get_ops();
    if (platform_ops == NULL || platform_ops->initialize == NULL ||
        platform_ops->time_ns == NULL ||
        platform_ops->console_write == NULL ||
        platform_ops->console_read == NULL) {
        platform_ops = NULL;
        return GOTH_PE_STATUS_PLATFORM_ERROR;
    }

    status = platform_ops->initialize(&platform_info);
    if (status != GOTH_PE_STATUS_OK) {
        platform_ops = NULL;
        return status;
    }
    initialized = true;
    return GOTH_PE_STATUS_OK;
}

goth_pe_status_t goth_pe_get_info(goth_pe_info_t *info)
{
    if (info == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (!initialized) {
        return GOTH_PE_STATUS_NOT_INITIALIZED;
    }

    *info = platform_info;
    return GOTH_PE_STATUS_OK;
}

goth_pe_status_t goth_pe_time_ns(uint64_t *time_ns)
{
    if (time_ns == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (!initialized) {
        return GOTH_PE_STATUS_NOT_INITIALIZED;
    }
    return platform_ops->time_ns(time_ns);
}

goth_pe_status_t goth_pe_console_write(const char *text, size_t length)
{
    if (text == NULL && length != 0U) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (!initialized) {
        return GOTH_PE_STATUS_NOT_INITIALIZED;
    }
    return platform_ops->console_write(text, length);
}

goth_pe_status_t goth_pe_console_read(char *buffer,
                                      size_t capacity,
                                      size_t *read_length)
{
    if (buffer == NULL || read_length == NULL) {
        return GOTH_PE_STATUS_INVALID_ARGUMENT;
    }
    if (!initialized) {
        return GOTH_PE_STATUS_NOT_INITIALIZED;
    }
    *read_length = 0U;
    return platform_ops->console_read(buffer, capacity, read_length);
}