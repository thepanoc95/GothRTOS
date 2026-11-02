#ifndef GOTHRTOS_PE_ARM32_H
#define GOTHRTOS_PE_ARM32_H

#include <stdint.h>

/* Hardware operations supplied by each ARM32 board port. The cooperative
   scheduler, pexpert glue and console formatting are shared in pe_arm32.c. */
typedef struct {
    const char *name;
    void (*uart_init)(void);
    void (*uart_putc)(char character);
    /* Returns the next received byte, or -1 when the receive FIFO is empty. */
    int (*uart_getc)(void);
    uint64_t (*time_ns)(void);
    void (*exit)(int status);
} goth_arm_board_ops_t;

const goth_arm_board_ops_t *goth_arm_board_get_ops(void);

#endif
