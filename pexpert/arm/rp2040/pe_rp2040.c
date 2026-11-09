#include "pe_cortexm.h"

#include "gothrtos/cortexm.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* Raspberry Pi Pico (RP2040) and Pico 2 (RP2350) board port.
 *
 * Both SoCs expose a PL011-compatible UART (the same register layout as the
 * BCM2835 port), a 64-bit microsecond timer and a GPIO block. The register
 * bases differ, so they are selected at compile time with GOTH_CM_RP2350. */

#if defined(GOTH_CM_RP2350)
#define GOTH_CM_BOARD_NAME "Raspberry Pi Pico 2 (RP2350)"
#define GOTH_CM_UART0_BASE UINT32_C(0x40070000)
#define GOTH_CM_TIMER_BASE UINT32_C(0x400b0000)
#define GOTH_CM_IO_BANK0_BASE UINT32_C(0x40028000)
#define GOTH_CM_PADS_BANK0_BASE UINT32_C(0x40038000)
#define GOTH_CM_COMPAT_UART "raspberrypi,rp2350-uart"
#define GOTH_CM_COMPAT_TIMER "raspberrypi,rp2350-timer"
#else
#define GOTH_CM_BOARD_NAME "Raspberry Pi Pico (RP2040)"
#define GOTH_CM_UART0_BASE UINT32_C(0x40034000)
#define GOTH_CM_TIMER_BASE UINT32_C(0x40054000)
#define GOTH_CM_IO_BANK0_BASE UINT32_C(0x40014000)
#define GOTH_CM_PADS_BANK0_BASE UINT32_C(0x4001c000)
#define GOTH_CM_COMPAT_UART "raspberrypi,rp2040-uart"
#define GOTH_CM_COMPAT_TIMER "raspberrypi,rp2040-timer"
#endif

/* PL011 registers. */
#define GOTH_CM_UART_DR UINT32_C(0x000)
#define GOTH_CM_UART_FR UINT32_C(0x018)
#define GOTH_CM_UART_IBRD UINT32_C(0x024)
#define GOTH_CM_UART_FBRD UINT32_C(0x028)
#define GOTH_CM_UART_LCR_H UINT32_C(0x02c)
#define GOTH_CM_UART_CR UINT32_C(0x030)
#define GOTH_CM_UART_IMSC UINT32_C(0x038)
#define GOTH_CM_UART_ICR UINT32_C(0x044)

#define GOTH_CM_UART_FR_TXFF (UINT32_C(1) << 5)
#define GOTH_CM_UART_FR_RXFE (UINT32_C(1) << 4)
#define GOTH_CM_UART_CR_ENABLE UINT32_C(0x301)
#define GOTH_CM_UART_BAUD UINT32_C(115200)

/* Timer: TIMERAWL/TIMERAWH are the low/high words of a free-running
   microsecond counter. Reading TIMERAWL latches TIMERAWH, so low-then-high is
   an atomic 64-bit read. */
#define GOTH_CM_TIMERAWL UINT32_C(0x028)
#define GOTH_CM_TIMERAWH UINT32_C(0x02c)

/* IO_BANK0 GPIO control: FUNCSEL field selects the peripheral. */
#define GOTH_CM_GPIO_CTRL(pin) (UINT32_C(0x004) + (pin) * UINT32_C(8))
#define GOTH_CM_GPIO_FUNC_UART UINT32_C(2)

/* PADS_BANK0: clear ISO (bit 8) and set IE (bit 6) on the RX pad. */
#define GOTH_CM_PAD(pin) (UINT32_C(0x004) + (pin) * UINT32_C(4))
#define GOTH_CM_PAD_ISO (UINT32_C(1) << 8)
#define GOTH_CM_PAD_IE (UINT32_C(1) << 6)

/* The default core clock is 125 MHz for RP2040 and 150 MHz for RP2350. */
#if defined(GOTH_CM_RP2350)
#define GOTH_CM_PERIPHERAL_CLOCK UINT32_C(150000000)
#else
#define GOTH_CM_PERIPHERAL_CLOCK UINT32_C(125000000)
#endif

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t)GOTH_CM_UART0_BASE;
static volatile uint32_t *const timer =
    (volatile uint32_t *)(uintptr_t)GOTH_CM_TIMER_BASE;
static volatile uint32_t *const io_bank0 =
    (volatile uint32_t *)(uintptr_t)GOTH_CM_IO_BANK0_BASE;
static volatile uint32_t *const pads_bank0 =
    (volatile uint32_t *)(uintptr_t)GOTH_CM_PADS_BANK0_BASE;

static void rp_gpio_uart(void)
{
    /* GPIO0 = UART0 TX, GPIO1 = UART0 RX. */
    io_bank0[GOTH_CM_GPIO_CTRL(0) / 4U] = GOTH_CM_GPIO_FUNC_UART;
    io_bank0[GOTH_CM_GPIO_CTRL(1) / 4U] = GOTH_CM_GPIO_FUNC_UART;
    pads_bank0[GOTH_CM_PAD(0) / 4U] =
        pads_bank0[GOTH_CM_PAD(0) / 4U] & ~GOTH_CM_PAD_ISO;
    pads_bank0[GOTH_CM_PAD(1) / 4U] =
        (pads_bank0[GOTH_CM_PAD(1) / 4U] & ~GOTH_CM_PAD_ISO) | GOTH_CM_PAD_IE;
}

static void rp_uart_init(void)
{
    uint64_t scaled;
    uint32_t ibrd;
    uint32_t fbrd;

    rp_gpio_uart();

    /* divisor = clock / (16 * baud); IBRD = integer part, FBRD = fractional
       part in 1/64ths, rounded to nearest. */
    scaled = ((uint64_t)GOTH_CM_PERIPHERAL_CLOCK * UINT64_C(1000)) /
             ((uint64_t)16U * GOTH_CM_UART_BAUD);
    ibrd = (uint32_t)(scaled / UINT64_C(1000));
    fbrd = (uint32_t)(((scaled % UINT64_C(1000)) * UINT64_C(64) +
                       UINT64_C(500)) / UINT64_C(1000));

    uart[GOTH_CM_UART_CR / 4U] = 0U;
    uart[GOTH_CM_UART_ICR / 4U] = UINT32_C(0x7ff);
    uart[GOTH_CM_UART_IBRD / 4U] = ibrd;
    uart[GOTH_CM_UART_FBRD / 4U] = fbrd;
    uart[GOTH_CM_UART_LCR_H / 4U] = UINT32_C(0x70);
    uart[GOTH_CM_UART_IMSC / 4U] = 0U;
    uart[GOTH_CM_UART_CR / 4U] = GOTH_CM_UART_CR_ENABLE;
}

static void rp_uart_putc(char character)
{
    while ((uart[GOTH_CM_UART_FR / 4U] & GOTH_CM_UART_FR_TXFF) != 0U) {
        goth_cm_cpu_relax();
    }
    uart[GOTH_CM_UART_DR / 4U] = (uint32_t)(unsigned char)character;
}

static int rp_uart_getc(void)
{
    if ((uart[GOTH_CM_UART_FR / 4U] & GOTH_CM_UART_FR_RXFE) != 0U) {
        return -1;
    }
    return (int)(uart[GOTH_CM_UART_DR / 4U] & UINT32_C(0xff));
}

static uint64_t rp_time_ns(void)
{
    uint32_t low = timer[GOTH_CM_TIMERAWL / 4U];
    uint32_t high = timer[GOTH_CM_TIMERAWH / 4U];

    return (((uint64_t)high << 32) | (uint64_t)low) * UINT64_C(1000);
}

static void rp_exit(int status)
{
    (void)status;

    /* There is no PSCI or semihosting on the Pico: park the CPU. A debugger
       or the QEMU test harness stops the machine from the outside. */
    for (;;) {
        goth_cm_cpu_relax();
    }
}

static goth_hdf_device_t rp_uart_device = {
    "uart0", GOTH_CM_COMPAT_UART, GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_CONSOLE, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};
static goth_hdf_device_t rp_timer_device = {
    "timer0", GOTH_CM_COMPAT_TIMER, GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_TIMER, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static void rp_hdf_register(void)
{
    rp_uart_device.mmio[0].base = GOTH_CM_UART0_BASE;
    rp_uart_device.mmio[0].size = 0x1000U;
    rp_uart_device.mmio_count = 1U;

    rp_timer_device.mmio[0].base = GOTH_CM_TIMER_BASE;
    rp_timer_device.mmio[0].size = 0x1000U;
    rp_timer_device.mmio_count = 1U;

    (void)goth_hdf_register_bus(GOTH_HDF_BUS_PLATFORM, "platform");
    (void)goth_hdf_register_device(&rp_uart_device);
    (void)goth_hdf_register_device(&rp_timer_device);
}

static const goth_cm_board_ops_t rp_board_ops = {
    GOTH_CM_BOARD_NAME,
    rp_uart_init,
    rp_uart_putc,
    rp_uart_getc,
    rp_time_ns,
    rp_exit,
    rp_hdf_register
};

const goth_cm_board_ops_t *goth_cm_board_get_ops(void)
{
    return &rp_board_ops;
}
