#include "pe_arm32.h"

#include "gothrtos/arm32.h"
#include "gothrtos/hdf.h"

#include <stddef.h>
#include <stdint.h>

/* Test Board board port.
 *
 * Scaffolded by OSBuild. Fill in the register bases and the four hardware
 * operations below; the scheduler, pexpert glue and console formatting are
 * shared in pe_arm32.c. */

#define TEST_BOARD_UART_BASE UINT32_C(0x00000000)
#define TEST_BOARD_UART_BAUD UINT32_C(115200)

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t)TEST_BOARD_UART_BASE;

static void TEST_BOARD_uart_init(void)
{
    /* TODO: configure GPIO/pin muxing and the UART divisor for the clock. */
}

static void TEST_BOARD_uart_putc(char character)
{
    (void)character;
    /* TODO: wait for space, then write the character to the data register. */
}

static int TEST_BOARD_uart_getc(void)
{
    /* TODO: return the next received byte, or -1 when the FIFO is empty. */
    return -1;
}

static uint64_t TEST_BOARD_time_ns(void)
{
    /* TODO: return the board's monotonic clock in nanoseconds. */
    return 0U;
}

static void TEST_BOARD_exit(int status)
{
    (void)status;
    for (;;) {
        goth_arm32_cpu_relax();
    }
}

static void TEST_BOARD_hdf_register(void)
{
    /* TODO: describe the board's devices and register them with the HDF. */
}

static const goth_arm32_board_ops_t TEST_BOARD_board_ops = {
    "Test Board",
    TEST_BOARD_uart_init,
    TEST_BOARD_uart_putc,
    TEST_BOARD_uart_getc,
    TEST_BOARD_time_ns,
    TEST_BOARD_exit,
    TEST_BOARD_hdf_register
};

const goth_arm32_board_ops_t *goth_arm32_board_get_ops(void)
{
    return &TEST_BOARD_board_ops;
}
