#ifndef GOTHRTOS_PE_CORTEXM_H
#define GOTHRTOS_PE_CORTEXM_H

#include <stdint.h>

/* Hardware operations supplied by each Cortex-M board port (RP2040, RP2350).
   The cooperative scheduler, pexpert glue and console formatting are shared in
   pe_cortexm.c, mirroring the ARM32 and AMD64 ports. */
typedef struct {
    const char *name;
    void (*uart_init)(void);
    void (*uart_putc)(char character);
    /* Returns the next received byte, or -1 when the receive FIFO is empty. */
    int (*uart_getc)(void);
    uint64_t (*time_ns)(void);
    void (*exit)(int status);
    /* Optional: publishes the board's devices to the Hardware Driver
       Foundation. Boards without a device model leave this NULL. */
    void (*hdf_register)(void);
} goth_cm_board_ops_t;

const goth_cm_board_ops_t *goth_cm_board_get_ops(void);

#endif
