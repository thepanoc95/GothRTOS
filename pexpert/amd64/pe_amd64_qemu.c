#include "pe_amd64.h"

#include "gothrtos/amd64.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* QEMU x86_64 ("pc"/"q35") board: 16550-compatible COM1 serial port and the
   CPU timestamp counter for time. */

#define GOTH_AMD64_COM1_BASE UINT16_C(0x03f8)

#define GOTH_AMD64_UART_DATA 0U     /* receive/transmit buffer, DLAB=0 */
#define GOTH_AMD64_UART_IER 1U      /* interrupt enable */
#define GOTH_AMD64_UART_FCR 2U      /* FIFO control */
#define GOTH_AMD64_UART_LCR 3U      /* line control */
#define GOTH_AMD64_UART_MCR 4U      /* modem control */
#define GOTH_AMD64_UART_LSR 5U      /* line status */
#define GOTH_AMD64_UART_DLL 0U      /* divisor latch low, DLAB=1 */
#define GOTH_AMD64_UART_DLM 1U      /* divisor latch high, DLAB=1 */

#define GOTH_AMD64_LSR_DATA_READY UINT8_C(0x01)
#define GOTH_AMD64_LSR_THR_EMPTY UINT8_C(0x20)
#define GOTH_AMD64_LCR_8N1 UINT8_C(0x03)
#define GOTH_AMD64_LCR_DLAB UINT8_C(0x80)
#define GOTH_AMD64_FCR_ENABLE UINT8_C(0xc7)

/* 115200 baud with a 1.8432 MHz divisor clock: 1843200 / (16 * 115200) = 1. */
#define GOTH_AMD64_BAUD_DIVISOR UINT16_C(1)

/* Assume a 1 GHz timestamp counter unless the board calibrates it. This keeps
   the clock monotonic on machines where TSC frequency is unknown; a board can
   override it. */
#ifndef GOTH_AMD64_TSC_HZ
#define GOTH_AMD64_TSC_HZ UINT64_C(1000000000)
#endif

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void qemu_serial_init(void)
{
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_IER, 0x00); /* no interrupts */
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_LCR,
         GOTH_AMD64_LCR_DLAB); /* enable divisor latch */
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_DLL,
         (uint8_t)(GOTH_AMD64_BAUD_DIVISOR & 0xffU));
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_DLM,
         (uint8_t)((GOTH_AMD64_BAUD_DIVISOR >> 8) & 0xffU));
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_LCR,
         GOTH_AMD64_LCR_8N1); /* 8 bits, no parity, one stop */
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_FCR, GOTH_AMD64_FCR_ENABLE);
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_MCR, 0x03); /* DTR, RTS */
}

static void qemu_serial_putc(char character)
{
    while ((inb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_LSR) &
            GOTH_AMD64_LSR_THR_EMPTY) == 0U) {
        goth_amd64_cpu_relax();
    }
    outb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_DATA,
         (uint8_t)(unsigned char)character);
}

static int qemu_serial_getc(void)
{
    if ((inb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_LSR) &
         GOTH_AMD64_LSR_DATA_READY) == 0U) {
        return -1;
    }
    return (int)inb(GOTH_AMD64_COM1_BASE + GOTH_AMD64_UART_DATA);
}

static uint64_t qemu_time_ns(void)
{
    uint64_t ticks = goth_amd64_read_tsc();
    uint64_t whole = ticks / GOTH_AMD64_TSC_HZ;
    uint64_t remainder = ticks % GOTH_AMD64_TSC_HZ;

    if (whole > (UINT64_MAX - UINT64_C(999999999)) / UINT64_C(1000000000)) {
        return UINT64_MAX;
    }
    return whole * UINT64_C(1000000000) +
           (remainder * UINT64_C(1000000000)) / GOTH_AMD64_TSC_HZ;
}

/* QEMU's isa-debug-exit device: a write of (status << 1) | 1 to port 0xf4
   stops the machine, so a completed boot terminates QEMU instead of hanging.
   The device is optional; without it the CPU simply halts. */
#define GOTH_AMD64_QEMU_EXIT_PORT UINT16_C(0xf4)

static void qemu_exit(int status)
{
    outb(GOTH_AMD64_QEMU_EXIT_PORT,
         (uint8_t)(((unsigned)status << 1) | 1U));
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

static goth_hdf_device_t qemu_serial_device = {
    "com1", "ns16550a", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_CONSOLE, 0U,
    {{GOTH_AMD64_COM1_BASE, 8U, 0U}}, 1U, {{4U, 0U}}, 1U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static goth_hdf_device_t qemu_tsc_device = {
    "tsc", "x86,tsc", GOTH_HDF_BUS_PLATFORM,
    GOTH_HDF_CLASS_TIMER, 0U, {{0U, 0U, 0U}}, 0U, {{0U, 0U}}, 0U,
    NULL, NULL, GOTH_HDF_DEVICE_UNREGISTERED, GOTH_HDF_POWER_OFF, NULL
};

static void qemu_hdf_register(void)
{
    (void)goth_hdf_register_bus(GOTH_HDF_BUS_PLATFORM, "platform");
    (void)goth_hdf_register_device(&qemu_serial_device);
    (void)goth_hdf_register_device(&qemu_tsc_device);
}

static const goth_amd64_board_ops_t qemu_board_ops = {
    "QEMU x86_64 (pc)",
    qemu_serial_init,
    qemu_serial_putc,
    qemu_serial_getc,
    qemu_time_ns,
    qemu_exit,
    qemu_hdf_register
};

const goth_amd64_board_ops_t *goth_amd64_board_get_ops(void)
{
    return &qemu_board_ops;
}
