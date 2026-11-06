#ifndef GOTHRTOS_PEXPERT_H
#define GOTHRTOS_PEXPERT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    GOTH_PE_STATUS_OK = 0,
    GOTH_PE_STATUS_INVALID_ARGUMENT,
    GOTH_PE_STATUS_NOT_INITIALIZED,
    GOTH_PE_STATUS_PLATFORM_ERROR,
    GOTH_PE_STATUS_TIME_RANGE
} goth_pe_status_t;

typedef struct {
    const char *platform_name;
    const char *architecture;
    uint32_t logical_cpu_count;
    uint8_t pointer_width;
} goth_pe_info_t;

goth_pe_status_t goth_pe_init(void);
goth_pe_status_t goth_pe_get_info(goth_pe_info_t *info);
goth_pe_status_t goth_pe_time_ns(uint64_t *time_ns);
goth_pe_status_t goth_pe_console_write(const char *text, size_t length);
/* Reads at most capacity bytes from the console into buffer. The number of
   bytes read is stored in read_length; a return of GOTH_PE_STATUS_OK with a
   zero length means the console reached end of input. */
goth_pe_status_t goth_pe_console_read(char *buffer,
                                      size_t capacity,
                                      size_t *read_length);

/* Registers the platform's hardware devices with the HDF. A port without a
 * device model leaves the hook unset, and this then succeeds without doing
 * anything. */
goth_pe_status_t goth_pe_register_hardware(void);

#endif
