#ifndef SYSGOTH_AMD64_HW_H
#define SYSGOTH_AMD64_HW_H

#include <stdint.h>

/* Minimal COM1 serial and TSC time for the AMD64 SYSGOTH bootloader. */

void sg_amd64_serial_init(uint32_t baud);
void sg_amd64_serial_putc(char character);
int sg_amd64_serial_getc(void);
int sg_amd64_serial_getc_timeout(uint32_t timeout_ms);

uint64_t sg_amd64_time_ms(void);
void sg_amd64_cpu_relax(void);
void sg_amd64_halt(void) __attribute__((noreturn));

#endif
