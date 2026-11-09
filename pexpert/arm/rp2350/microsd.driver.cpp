#define HDF_MANIFEST_VERSION 1

#include <gothrtos/hdf.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SD_SPI_INST	spi0
#define PIN_SD_MISO	16
#define PIN_SD_CS	17
#define PIN_SD_SCK	18
#define PIN_SD_MOSI	19

void uSDspiInit()
{
	spi_init(SD_SPI_INST, 400000);

	gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
	gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
	gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);

	gpio_init(PIN_SD_CS);
	gpio_set_dir(PIN_SD_CS, GPIO_OUT);
	gpio_put(PIN_SD_CS, 1);
}

// Send a byte and read the response byte simultaneously
inline uint8_t sd_spi_transfer_byte(uint8_t byte_out) {
    uint8_t byte_in;
    spi_write_read_blocking(SD_SPI_INST, &byte_out, &byte_in, 1);
    return byte_in;
}

// Read an R1 response from the card
uint8_t sd_read_r1() {
    uint8_t response;
    int timeout = 0;
    // Keep reading until the MSB is 0 (valid R1 token format)
    do {
        response = sd_spi_transfer_byte(0xFF);
        if (timeout++ > 1000) return 0xFF; // Error timeout
    } while (response & 0x80);
    return response;
}

