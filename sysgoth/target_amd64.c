#include "sysgoth.h"
#include "amd64_block.h"
#include "amd64_hw.h"
#include "amd64_io.h"
#include "amd64_vga.h"
#include "gothrtos/sparsekernel.h"

/* SYSGOTH for AMD64 bare metal.
 *
 * Outputs to VGA text mode and COM1 serial. Loads a SparseKernel.img3 from:
 *   - serial (COM1), streamed by the host sysgoth-flash tool, or
 *   - a block device (ATA HDD/SSD in IDE compatibility mode, ATAPI CD-ROM)
 *     by scanning for the SparseKernel magic. USB mass storage and AHCI
 *     require dedicated controller drivers not included here.
 *
 * The loader is linked at 4 MiB so kernels that load at 1 MiB do not
 * overwrite it while being applied. */

/* Linker symbols defining the loader's own footprint. */
extern char __image_start[];
extern char __image_end[];

/* Staging buffer for disk-loaded images. 8 MiB is enough for typical kernel
 * images while fitting comfortably in low memory away from the loader. */
#define SG_AMD64_DISK_BUF_SIZE (8U * 1024U * 1024U)
static uint8_t disk_buffer[SG_AMD64_DISK_BUF_SIZE];

/* Writable regions exposed to the image store. We allow the conventional
 * kernel area and high memory, but never the loader itself. */
static sg_region_t sg_regions[3];
static size_t sg_region_count;

static void build_regions(void)
{
    uint64_t loader_start = (uint64_t)(uintptr_t)__image_start;
    uint64_t loader_end = (uint64_t)(uintptr_t)__image_end;

    sg_region_count = 0U;

    /* 1 MiB up to the loader. */
    if (loader_start > UINT64_C(0x00100000)) {
        sg_regions[sg_region_count].base = UINT64_C(0x00100000);
        sg_regions[sg_region_count].size = loader_start - UINT64_C(0x00100000);
        sg_regions[sg_region_count].flags = 0U;
        sg_region_count++;
    }
    /* After the loader up to 512 MiB (within our 1 GiB identity map). */
    if (loader_end < UINT64_C(0x20000000)) {
        sg_regions[sg_region_count].base = loader_end;
        sg_regions[sg_region_count].size = UINT64_C(0x20000000) - loader_end;
        sg_regions[sg_region_count].flags = 0U;
        sg_region_count++;
    }
}

/* Dual console: write to both VGA and COM1. */
static void con_putc(char character)
{
    sg_vga_putc(character);
    sg_amd64_serial_putc(character);
}

static void con_puts(const char *text)
{
    while (text != NULL && *text != '\0') {
        con_putc(*text++);
    }
}

static void con_put_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do {
        buf[--i] = digits[value & 0xFU];
        value >>= 4;
    } while (value != 0U);
    con_puts("0x");
    con_puts(&buf[i]);
}

static void con_put_u32(uint32_t value)
{
    char buf[12];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    if (value == 0U) {
        buf[--i] = '0';
    }
    while (value > 0U) {
        buf[--i] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    con_puts(&buf[i]);
}

/* sg_target_t hooks. Output goes to VGA+serial; input comes from serial. */
static void target_uart_init(uint32_t baud)
{
    sg_amd64_serial_init(baud);
}

static void target_uart_putc(char character)
{
    con_putc(character);
}

static int target_uart_getc(void)
{
    return sg_amd64_serial_getc();
}

static uint64_t target_time_ms(void)
{
    return sg_amd64_time_ms();
}

static void target_jump(uint64_t entry) __attribute__((noreturn));

static void target_jump(uint64_t entry)
{
    void (*kernel_entry)(void) = (void (*)(void))(uintptr_t)entry;

    __asm__ volatile("cli" ::: "memory");
    kernel_entry();
    sg_amd64_halt();
}

static void target_reset(void) __attribute__((noreturn));

static void target_reset(void)
{
    /* 8042 keyboard controller reset pulse. */
    sg_amd64_outb(0x64, 0xFE);
    sg_amd64_halt();
}

static const sg_target_t sg_target = {
    "SYSGOTH-AMD64",
    "amd64-pc",
    target_uart_init,
    target_uart_putc,
    target_uart_getc,
    target_time_ms,
    NULL, /* regions set up at runtime */
    0U,
    target_jump,
    target_reset
};

/* Try to boot an image already in memory (from disk). */
static bool try_boot_buffer(const uint8_t *bytes, size_t size,
                            const char *source)
{
    sg_boot_stats_t stats;
    sg_target_t target = sg_target;
    goth_sparse_status_t status;

    target.regions = sg_regions;
    target.region_count = sg_region_count;

    status = sysgoth_apply_buffer(&target, bytes, size, &stats);
    if (status != GOTH_SPARSE_OK) {
        con_puts("SYSGOTH: image from ");
        con_puts(source);
        con_puts(" rejected: ");
        con_puts(sysgoth_status_name(status));
        con_puts("\r\n");
        return false;
    }

    con_puts("SYSGOTH: OK source=");
    con_puts(source);
    con_puts(" entry=");
    con_put_hex(stats.entry);
    con_puts(" size=");
    con_put_u32(stats.image_size);
    con_puts(" bytes\r\nSYSGOTH: booting GothRTOS\r\n");
    target_jump(stats.entry);
}

void sysgoth_amd64_main(void)
{
    static uint8_t scratch[4096];
    sg_block_dev_t devs[SG_BLOCK_MAX_DEVS];
    int dev_count;
    int i;
    uint32_t disk_scan_interval_ms = 3000U;
    uint64_t last_scan_ms = 0U;
    bool first = true;

    sg_vga_init();
    sg_vga_set_color(SG_VGA_LIGHT_CYAN, SG_VGA_BLACK);
    sg_amd64_serial_init(115200U);

    build_regions();

    con_puts("\r\nSYSGOTH ");
    con_puts(SYSGOTH_VERSION);
    con_puts(" (amd64, VGA + COM1)\r\n");
    con_puts("GothRTOS bootloader: serial and disk image sources\r\n");

    dev_count = sg_block_probe(devs, SG_BLOCK_MAX_DEVS);
    con_puts("Block devices: ");
    con_put_u32((uint32_t)dev_count);
    con_puts("\r\n");
    for (i = 0; i < dev_count; i++) {
        con_puts("  [");
        con_put_u32((uint32_t)i);
        con_puts("] ");
        con_puts(devs[i].type == SG_BLOCK_ATAPI ? "ATAPI (CD/DVD)" : "ATA (HDD/SSD)");
        con_puts(" io=");
        con_put_hex(devs[i].io_base);
        if (devs[i].model[0] != '\0') {
            con_puts(" model=");
            con_puts(devs[i].model);
        }
        con_puts("\r\n");
    }
    if (dev_count == 0) {
        con_puts("  (none detected; will still wait for serial)\r\n");
    }

    for (;;) {
        sg_target_t target = sg_target;
        sg_boot_stats_t stats;
        goth_sparse_status_t status;
        uint64_t now;

        target.regions = sg_regions;
        target.region_count = sg_region_count;

        if (first) {
            con_puts("\r\nWaiting for SparseKernel.img3 on COM1 (115200) or disk...\r\n");
            first = false;
        }

        /* Poll serial for an incoming image. Short magic timeout so we can
         * also check disks; once magic is seen, body bytes get 5000 ms. */
        status = sysgoth_receive_ex(&target, scratch, sizeof(scratch),
                                    250U, 5000U, &stats);
        if (status == GOTH_SPARSE_OK) {
            con_puts("SYSGOTH: OK source=serial entry=");
            con_put_hex(stats.entry);
            con_puts(" size=");
            con_put_u32(stats.image_size);
            con_puts(" bytes\r\nSYSGOTH: booting GothRTOS\r\n");
            target_jump(stats.entry);
        } else if (status != GOTH_SPARSE_TRUNCATED) {
            /* Magic may have been found but the stream was bad. */
            con_puts("SYSGOTH: ERR serial: ");
            con_puts(sysgoth_status_name(status));
            con_puts("\r\n");
        }

        /* Periodically scan block devices for a SparseKernel.img3. */
        now = sg_amd64_time_ms();
        if (dev_count > 0 &&
            (last_scan_ms == 0U || now - last_scan_ms >= disk_scan_interval_ms)) {
            last_scan_ms = now;
            for (i = 0; i < dev_count; i++) {
                size_t img_size = 0U;

                /* Scan the first 64 MiB worth of sectors for ATA, or 32K
                 * sectors for ATAPI, in a filesystem-agnostic way. */
                uint32_t max_sec = (devs[i].type == SG_BLOCK_ATAPI)
                                       ? 32768U
                                       : 131072U;
                if (sg_block_find_image(&devs[i], disk_buffer,
                                        sizeof(disk_buffer), &img_size,
                                        max_sec)) {
                    char name[16];
                    int p = 0;
                    const char *prefix =
                        devs[i].type == SG_BLOCK_ATAPI ? "cdrom" : "disk";
                    const char *q = prefix;

                    while (*q != '\0' && p < 14) {
                        name[p++] = *q++;
                    }
                    name[p++] = (char)('0' + i);
                    name[p] = '\0';
                    (void)try_boot_buffer(disk_buffer, img_size, name);
                }
            }
        }
    }
}

/* QEMU isa-debug-exit helper, useful for automated runs. */
void sg_amd64_exit(int status)
{
    sg_amd64_outb(0xF4, (uint8_t)(((unsigned)status << 1) | 1U));
    sg_amd64_halt();
}
