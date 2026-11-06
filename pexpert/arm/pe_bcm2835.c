#include "pe_arm32.h"

#include "gothrtos/arm32.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* Raspberry Pi Zero (BCM2835) peripherals. */
#define GOTH_ARM32_GPIO_BASE UINT32_C(0x20200000)
#define GOTH_ARM32_UART_BASE UINT32_C(0x20201000)
#define GOTH_ARM32_SYSTIMER_BASE UINT32_C(0x20003000)

#define GOTH_ARM32_GPFSEL1 UINT32_C(0x004)
#define GOTH_ARM32_SYSTIMER_CLO UINT32_C(0x004)
#define GOTH_ARM32_SYSTIMER_CHI UINT32_C(0x008)

#define GOTH_ARM32_UART_DR UINT32_C(0x000)
#define GOTH_ARM32_UART_FR UINT32_C(0x018)
#define GOTH_ARM32_UART_IBRD UINT32_C(0x024)
#define GOTH_ARM32_UART_FBRD UINT32_C(0x028)
#define GOTH_ARM32_UART_LCR_H UINT32_C(0x02c)
#define GOTH_ARM32_UART_CR UINT32_C(0x030)
#define GOTH_ARM32_UART_IMSC UINT32_C(0x038)
#define GOTH_ARM32_UART_ICR UINT32_C(0x044)

#define GOTH_ARM32_UART_FR_TXFF (UINT32_C(1) << 5)
#define GOTH_ARM32_UART_FR_RXFE (UINT32_C(1) << 4)
#define GOTH_ARM32_UART_CR_ENABLE (UINT32_C(0x301))
#define GOTH_ARM32_UART_BAUD UINT32_C(115200)

/* The PL011 is clocked from the BCM2835 core clock. The firmware default is
   250 MHz; override this at build time if config.txt changes core_freq. */
#ifndef GOTH_ARM32_BCM2835_UART_CLOCK
#define GOTH_ARM32_BCM2835_UART_CLOCK UINT32_C(250000000)
#endif

static volatile uint32_t *const gpio =
    (volatile uint32_t *)(uintptr_t)GOTH_ARM32_GPIO_BASE;
static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t)GOTH_ARM32_UART_BASE;
static volatile uint32_t *const systimer =
    (volatile uint32_t *)(uintptr_t)GOTH_ARM32_SYSTIMER_BASE;

static void bcm2835_gpio_uart(void)
{
    uint32_t value = gpio[GOTH_ARM32_GPFSEL1 / 4U];

    /* GPIO14 and GPIO15 to ALT0 (UART0 TX/RX). */
    value &= ~((UINT32_C(7) << 12) | (UINT32_C(7) << 15));
    value |= (UINT32_C(4) << 12) | (UINT32_C(4) << 15);
    gpio[GOTH_ARM32_GPFSEL1 / 4U] = value;
}

static void bcm2835_uart_init(void)
{
    uint64_t scaled;
    uint32_t ibrd;
    uint32_t fbrd;

    bcm2835_gpio_uart();

    /* divisor = clock / (16 * baud); IBRD = integer part, FBRD = fractional
       part in 1/64ths, rounded to nearest. */
    scaled = ((uint64_t)GOTH_ARM32_BCM2835_UART_CLOCK * UINT64_C(1000)) /
             ((uint64_t)16U * GOTH_ARM32_UART_BAUD);
    ibrd = (uint32_t)(scaled / UINT64_C(1000));
    fbrd = (uint32_t)(((scaled % UINT64_C(1000)) * UINT64_C(64) +
                       UINT64_C(500)) / UINT64_C(1000));

    uart[GOTH_ARM32_UART_CR / 4U] = 0U;
    uart[GOTH_ARM32_UART_ICR / 4U] = UINT32_C(0x7ff);
    uart[GOTH_ARM32_UART_IBRD / 4U] = ibrd;
    uart[GOTH_ARM32_UART_FBRD / 4U] = fbrd;
    uart[GOTH_ARM32_UART_LCR_H / 4U] = UINT32_C(0x70);
    uart[GOTH_ARM32_UART_IMSC / 4U] = 0U;
    uart[GOTH_ARM32_UART_CR / 4U] = GOTH_ARM32_UART_CR_ENABLE;
}

static void bcm2835_uart_putc(char character)
{
    while ((uart[GOTH_ARM32_UART_FR / 4U] & GOTH_ARM32_UART_FR_TXFF) != 0U) {
        goth_arm32_cpu_relax();
    }
    uart[GOTH_ARM32_UART_DR / 4U] = (uint32_t)(unsigned char)character;
}

static int bcm2835_uart_getc(void)
{
    if ((uart[GOTH_ARM32_UART_FR / 4U] & GOTH_ARM32_UART_FR_RXFE) != 0U) {
        return -1;
    }
    return (int)(uart[GOTH_ARM32_UART_DR / 4U] & UINT32_C(0xff));
}

static uint64_t bcm2835_time_ns(void)
{
    uint32_t high;
    uint32_t low;
    uint32_t high_again;

    /* The system timer counts at 1 MHz. Read CHI/CLO/CHI so a carry into the
       high word cannot produce a non-monotonic timestamp. */
    do {
        high = systimer[GOTH_ARM32_SYSTIMER_CHI / 4U];
        low = systimer[GOTH_ARM32_SYSTIMER_CLO / 4U];
        high_again = systimer[GOTH_ARM32_SYSTIMER_CHI / 4U];
    } while (high != high_again);

    return (((uint64_t)high << 32) | (uint64_t)low) * UINT64_C(1000);
}

static void bcm2835_exit(int status)
{
    (void)status;

    /* There is no PSCI or semihosting on real hardware: park the CPU. A
       debugger or QEMU test harness stops the machine from the outside. */
    for (;;) {
        goth_arm32_cpu_relax();
    }
}

/* Device model for the BCM2835 board. The UART is a PL011 on the platform
 * bus; the system timer is a 1 MHz free-running counter. Both are published
 * so drivers can bind instead of reaching for the registers directly. */
static goth_hdf_device_t bcm2835_uart_device = {
    "bcm2835-uart0", "brcm,bcm2835-pl011", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_CONSOLE, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};
static goth_hdf_device_t bcm2835_timer_device = {
    "bcm2835-systimer", "brcm,bcm2835-system-timer", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_TIMER, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static void bcm2835_hdf_register(void)
{
    bcm2835_uart_device.mmio[0].base = GOTH_ARM32_UART_BASE;
    bcm2835_uart_device.mmio[0].size = 0x1000U;
    bcm2835_uart_device.mmio_count = 1U;

    bcm2835_timer_device.mmio[0].base = GOTH_ARM32_SYSTIMER_BASE;
    bcm2835_timer_device.mmio[0].size = 0x1000U;
    bcm2835_timer_device.mmio_count = 1U;

    (void)goth_hdf_register_device(&bcm2835_uart_device);
    (void)goth_hdf_register_device(&bcm2835_timer_device);
}

static const goth_arm_board_ops_t bcm2835_board_ops = {
    "Raspberry Pi Zero (BCM2835)",
    bcm2835_uart_init,
    bcm2835_uart_putc,
    bcm2835_uart_getc,
    bcm2835_time_ns,
    bcm2835_exit,
    bcm2835_hdf_register
};

const goth_arm_board_ops_t *goth_arm_board_get_ops(void)
{
    return &bcm2835_board_ops;
}
