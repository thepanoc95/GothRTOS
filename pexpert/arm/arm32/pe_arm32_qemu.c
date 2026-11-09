#include "pe_arm32.h"

#include "gothrtos/arm32.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* QEMU "virt" machine, ARM32, PL011 UART. */
#define GOTH_ARM32_UART_BASE UINT32_C(0x09000000)
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
#define GOTH_ARM32_DEFAULT_TIMER_HZ UINT32_C(62500000)

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t)GOTH_ARM32_UART_BASE;

static inline uint32_t qemu_read_cntfrq(void)
{
    uint32_t value;

    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(value));
    return value;
}

static inline uint64_t qemu_read_cntvct(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("mrrc p15, 1, %0, %1, c14"
                     : "=r"(low), "=r"(high));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

static uint64_t qemu_time_ns(void)
{
    uint64_t ticks = qemu_read_cntvct();
    uint64_t frequency = (uint64_t)qemu_read_cntfrq();
    uint64_t whole;
    uint64_t remainder;

    if (frequency == 0U) {
        frequency = (uint64_t)GOTH_ARM32_DEFAULT_TIMER_HZ;
    }

    whole = ticks / frequency;
    remainder = ticks % frequency;
    if (whole > (UINT64_MAX - UINT64_C(999999999)) / UINT64_C(1000000000)) {
        return UINT64_MAX;
    }

    return whole * UINT64_C(1000000000) +
           (remainder * UINT64_C(1000000000)) / frequency;
}

static void qemu_uart_init(void)
{
    uart[GOTH_ARM32_UART_CR / 4U] = 0U;
    uart[GOTH_ARM32_UART_ICR / 4U] = UINT32_C(0x7ff);
    /* 115200 baud assuming the QEMU virt 24 MHz UART clock. */
    uart[GOTH_ARM32_UART_IBRD / 4U] = 13U;
    uart[GOTH_ARM32_UART_FBRD / 4U] = 1U;
    uart[GOTH_ARM32_UART_LCR_H / 4U] = UINT32_C(0x70);
    uart[GOTH_ARM32_UART_IMSC / 4U] = 0U;
    uart[GOTH_ARM32_UART_CR / 4U] = GOTH_ARM32_UART_CR_ENABLE;
}

static void qemu_uart_putc(char character)
{
    while ((uart[GOTH_ARM32_UART_FR / 4U] & GOTH_ARM32_UART_FR_TXFF) != 0U) {
        goth_arm32_cpu_relax();
    }
    uart[GOTH_ARM32_UART_DR / 4U] = (uint32_t)(unsigned char)character;
}

static int qemu_uart_getc(void)
{
    if ((uart[GOTH_ARM32_UART_FR / 4U] & GOTH_ARM32_UART_FR_RXFE) != 0U) {
        return -1;
    }
    return (int)(uart[GOTH_ARM32_UART_DR / 4U] & UINT32_C(0xff));
}

static void qemu_exit(int status)
{
    register uint32_t r0 __asm__("r0") = UINT32_C(0x84000008);

    (void)status;

    /* QEMU virt implements PSCI SYSTEM_OFF over the HVC conduit when the
       selected CPU has EL2 (Cortex-A15 does). This powers the machine off, so
       a successful boot terminates QEMU instead of spinning forever. */
    __asm__ volatile("hvc #0" : : "r"(r0) : "memory");

    /* Fallback for configurations without PSCI: ARM semihosting SYS_EXIT. */
    r0 = UINT32_C(0x18);
    {
        register uint32_t r1 __asm__("r1") = UINT32_C(0x20026);

        __asm__ volatile("bkpt 0xab" : : "r"(r0), "r"(r1) : "memory");
    }

    for (;;) {
        goth_arm32_cpu_relax();
    }
}

static const goth_arm_board_ops_t qemu_board_ops = {
    "QEMU ARM virt (Cortex-A15)",
    qemu_uart_init,
    qemu_uart_putc,
    qemu_uart_getc,
    qemu_time_ns,
    qemu_exit,
    NULL
};

const goth_arm_board_ops_t *goth_arm_board_get_ops(void)
{
    return &qemu_board_ops;
}
