#ifndef GOTHRTOS_PE_AMD64_H
#define GOTHRTOS_PE_AMD64_H

#include <stdint.h>

/* Hardware operations supplied by each AMD64 board port. The cooperative
   scheduler, pexpert glue and console formatting are shared in pe_amd64.c.
   This mirrors the ARM32 board split (pe_arm32.h) so the two bare-metal
   ports stay structurally the same. */
typedef struct {
    const char *name;
    void (*serial_init)(void);
    void (*serial_putc)(char character);
    /* Returns the next received byte, or -1 when the receive FIFO is empty. */
    int (*serial_getc)(void);
    uint64_t (*time_ns)(void);
    void (*exit)(int status);
    /* Optional: publishes the board's devices to the Hardware Driver
       Foundation. Boards without a device model leave this NULL. */
    void (*hdf_register)(void);
} goth_amd64_board_ops_t;

const goth_amd64_board_ops_t *goth_amd64_board_get_ops(void);

#endif
