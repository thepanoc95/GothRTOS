#include "rp2350.hal.h"
#include <cstdint>

static spi_t spis[] {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 12500 * 1000
    }
};

static sd_card_t sd_cards[] = {
    .pcName = "0:",
    .spi = &spis[0],
    .ss_gpio = 17,
    .use_card_detect = false,
    .m_Stauts = STA_NOINT
};

size_t spi_get_by_num(size_t num) { return num < sizeof(spis)/sizeof(spis[0]) ? &spis[num] : NULL; }
size_t sd_get_by_num(size_t num) { return num < sizeof(sd_cards)/sizeof(sd_cards[0]) ? &sd_cards[num] : NULL; }

void OSinit_uart() {
    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    HWPuts(uart0, "OSBoot: RPI_INIT_UART\n");
}

void OSInitRP2350() {
    stdio_init_all();
    uint32_t chip_id = sysinfo_hw->chip_id;
    uint8_t revision     = (chip_id & SYSINFO_CHIP_ID_REVISION_BITS)     >> SYSINFO_CHIP_ID_REVISION_LSB;
    uint16_t part_number = (chip_id & SYSINFO_CHIP_ID_PART_BITS)         >> SYSINFO_CHIP_ID_PART_LSB;
    uint16_t designer    = (chip_id & SYSINFO_CHIP_ID_MANUFACTURER_BITS) >> SYSINFO_CHIP_ID_MANUFACTURER_LSB;

    HWPuts(uart0, "OSBoot: getting hardware information :3\n");

    if (part_number == 0x0002) {
        HWPuts(uart0, "Chip: RP2040\n");
        HWPuts(uart0, "Revision: %s\n", (revision == 1) ? "B0/B1" : (revision == 2) ? "B2" : "Unknown");
    } else if (part_number == 0x0004) {
        HWPuts(uart0, "Chip: RP2350\n");
        HWPuts(uart0, "Revision: A%d\n", revision);
    } else {
        HWPuts(uart0, "Chip: Unknown (Part: 0x%04x)\n", part_number);
    }
    HWPuts(uart0, "Designer ID: 0x%03x (0x927 = Raspberry Pi)\n", designer);
}

void OSInitSD() {
    HWPuts(uart0, "OSBoot: Initializing SD card\n");
    if (sd_init(&sd_cards[0]) != 0) {
        HWPuts(uart0, "OSBoot: SD card initialization failed\n");
    } else {
        HWPuts(uart0, "OSBoot: SD card initialized successfully\n");
    }
}

void OSBootStartup() {
    OSinit_uart();
    OSInitRP2350();
    OSInitSD();
}