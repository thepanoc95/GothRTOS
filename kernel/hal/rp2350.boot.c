#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "HardDriverFoundation.h"

static HdfStatus start_uart(HdfDevice *device) {
    (void)device;
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uart_puts(uart0, "GothRTOS: RP2350 UART driver started\r\n");
    return HDF_SUCCESS;
}

static const HdfDevice uart_device = {
    .name = "uart0",
    .compatible = "raspberrypi,rp2350-uart"
};

static const HdfDriver uart_driver = {
    .name = "rp2350-uart",
    .compatible = "raspberrypi,rp2350-uart",
    .probe = 0,
    .start = start_uart,
    .stop = 0
};

void OSBootStartup(void) {
    HdfInitialize();
    if (HdfRegisterDriver(&uart_driver) != HDF_SUCCESS ||
        HdfRegisterDevice(&uart_device) != HDF_SUCCESS) {
        return;
    }
    HdfStartDrivers();
}