#include "amd64_hw.h"
#include "amd64_io.h"

#define COM1_BASE UINT16_C(0x03f8)

#define UART_DATA 0U
#define UART_IER 1U
#define UART_FCR 2U
#define UART_LCR 3U
#define UART_MCR 4U
#define UART_LSR 5U
#define UART_DLL 0U
#define UART_DLM 1U

#define LSR_DATA_READY UINT8_C(0x01)
#define LSR_THR_EMPTY UINT8_C(0x20)
#define LCR_8N1 UINT8_C(0x03)
#define LCR_DLAB UINT8_C(0x80)
#define FCR_ENABLE UINT8_C(0xc7)

/* Nominal TSC frequency assumption for timing; same pragmatic default as the
 * AMD64 pexpert. On QEMU/KVM TSC is usually stable enough for bootloader
 * timeouts. */
#ifndef SYSGOTH_AMD64_TSC_HZ
#define SYSGOTH_AMD64_TSC_HZ UINT64_C(1000000000)
#endif

static uint64_t sg_tsc_hz = SYSGOTH_AMD64_TSC_HZ;

static uint64_t read_tsc(void)
{
    uint32_t low;
    uint32_t high;

    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | (uint64_t)low;
}

uint64_t sg_amd64_time_ms(void)
{
    uint64_t ticks = read_tsc();

    if (sg_tsc_hz == 0U) {
        return 0U;
    }
    return ticks / (sg_tsc_hz / 1000U);
}

void sg_amd64_cpu_relax(void)
{
    __asm__ volatile("pause" ::: "memory");
}

void sg_amd64_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void sg_amd64_serial_init(uint32_t baud)
{
    uint16_t divisor;

    if (baud == 0U) {
        baud = 115200U;
    }
    divisor = (uint16_t)(115200U / baud);
    if (divisor == 0U) {
        divisor = 1U;
    }

    sg_amd64_outb(COM1_BASE + UART_IER, 0x00);
    sg_amd64_outb(COM1_BASE + UART_LCR, LCR_DLAB);
    sg_amd64_outb(COM1_BASE + UART_DLL, (uint8_t)(divisor & 0xffU));
    sg_amd64_outb(COM1_BASE + UART_DLM, (uint8_t)((divisor >> 8) & 0xffU));
    sg_amd64_outb(COM1_BASE + UART_LCR, LCR_8N1);
    sg_amd64_outb(COM1_BASE + UART_FCR, FCR_ENABLE);
    sg_amd64_outb(COM1_BASE + UART_MCR, 0x03);
}

void sg_amd64_serial_putc(char character)
{
    while ((sg_amd64_inb(COM1_BASE + UART_LSR) & LSR_THR_EMPTY) == 0U) {
        sg_amd64_cpu_relax();
    }
    sg_amd64_outb(COM1_BASE + UART_DATA, (uint8_t)(unsigned char)character);
}

int sg_amd64_serial_getc(void)
{
    if ((sg_amd64_inb(COM1_BASE + UART_LSR) & LSR_DATA_READY) == 0U) {
        return -1;
    }
    return (int)sg_amd64_inb(COM1_BASE + UART_DATA);
}

int sg_amd64_serial_getc_timeout(uint32_t timeout_ms)
{
    uint64_t start = sg_amd64_time_ms();

    for (;;) {
        int c = sg_amd64_serial_getc();

        if (c >= 0) {
            return c;
        }
        if (timeout_ms != 0xffffffffU &&
            sg_amd64_time_ms() - start >= (uint64_t)timeout_ms) {
            return -1;
        }
        sg_amd64_cpu_relax();
    }
}
