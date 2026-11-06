#include "sysgoth.h"

/* SYSGOTH target glue for the Raspberry Pi Pico family.
 *
 * Both the Pico (RP2040, Cortex-M0+) and the Pico 2 (RP2350, Cortex-M33)
 * expose a PL011 UART at the same offsets and a 1 MHz microsecond timer, so
 * one implementation serves both. Which SoC is compiled in is selected by
 * SG_TARGET_RP2350.
 *
 * The loader's writable region is the SRAM window. The received image is
 * written into XIP-backed RAM (or SRAM) and then entered. This file is only
 * built by the Pico SDK CMake target; the host build never compiles it. */

#if defined(SG_TARGET_RP2350)
#include "pico/stdlib.h"
#define SG_TARGET_NAME "pico2"
#define SG_UART_ID uart0
#else
#include "pico/stdlib.h"
#define SG_TARGET_NAME "pico"
#define SG_UART_ID uart0
#endif

/* RP2040 and RP2350 both map 264 KiB of SRAM at 0x20000000. The image is
 * loaded there; a board port with a different memory map adjusts this. */
#define SG_SRAM_BASE UINT64_C(0x20000000)
#define SG_SRAM_SIZE (264U * 1024U)

static const sg_region_t sg_regions[] = {
    {SG_SRAM_BASE, SG_SRAM_SIZE, 0U}
};

static void sg_uart_init(uint32_t baud)
{
    uart_init(SG_UART_ID, baud);
    gpio_set_function(PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART);
}

static void sg_uart_putc(char character)
{
    if (character == '\n') {
        uart_putc_raw(SG_UART_ID, '\r');
    }
    uart_putc_raw(SG_UART_ID, character);
}

static int sg_uart_getc(void)
{
    if (!uart_is_readable(SG_UART_ID)) {
        return -1;
    }
    return (int)uart_getc(SG_UART_ID);
}

static uint64_t sg_time_ms(void)
{
    return (uint64_t)(time_us_64() / 1000U);
}

static void sg_jump(uint64_t entry) __attribute__((noreturn));

static void sg_jump(uint64_t entry)
{
    /* The SparseKernel image is position-dependent: the chunks were written
     * to their link addresses, so entering the recorded entry point runs it
     * in place. */
    void (*kernel_entry)(void) = (void (*)(void))(uintptr_t)entry;

    __asm__ volatile("cpsid i" ::: "memory");
    kernel_entry();
    for (;;) {
        tight_loop_contents();
    }
}

static void sg_reset(void) __attribute__((noreturn));

static void sg_reset(void)
{
    /* A watchdog reset would be the hardware path; without the SDK's
     * watchdog header the loader just spins and waits for a new image. */
    for (;;) {
        tight_loop_contents();
    }
}

static const sg_target_t sg_target = {
    SG_TARGET_NAME,
    SG_TARGET_NAME,
    sg_uart_init,
    sg_uart_putc,
    sg_uart_getc,
    sg_time_ms,
    sg_regions,
    sizeof(sg_regions) / sizeof(sg_regions[0]),
    sg_jump,
    sg_reset
};

int main(void)
{
    /* 115200 is the default SparseKernel UART rate. */
    (void)sysgoth_run(&sg_target, 115200U);
    return 0;
}
