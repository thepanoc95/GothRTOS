#ifndef _RP2350_HAL_H
#define _RP2350_HAL_H
#include "pico/stdlib.h"
#include "hw_config.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/structs/sysinfo.h"

#define HWPuts(r) uart_puts(r)

#endif
