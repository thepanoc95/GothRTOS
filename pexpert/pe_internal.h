#ifndef GOTHRTOS_PE_INTERNAL_H
#define GOTHRTOS_PE_INTERNAL_H

#include "gothrtos/pexpert.h"

typedef struct {
    goth_pe_status_t (*initialize)(goth_pe_info_t *info);
    goth_pe_status_t (*time_ns)(uint64_t *time_ns);
    goth_pe_status_t (*console_write)(const char *text, size_t length);
    goth_pe_status_t (*console_read)(char *buffer,
                                     size_t capacity,
                                     size_t *read_length);
    /* Optional: registers the platform's hardware devices with the HDF. Ports
     * that have no device model leave this NULL. */
    goth_pe_status_t (*hdf_register)(void);
} goth_pe_platform_ops_t;

const goth_pe_platform_ops_t *goth_pe_platform_get_ops(void);

#endif
