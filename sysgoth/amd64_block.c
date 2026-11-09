#include "amd64_block.h"
#include "amd64_hw.h"
#include "amd64_io.h"
#include "gothrtos/sparsekernel.h"

/* Freestanding local helpers (no libc). */
static void blk_memset(void *dst, int value, size_t count)
{
    uint8_t *p = (uint8_t *)dst;

    while (count-- > 0U) {
        *p++ = (uint8_t)value;
    }
}

static int blk_memcmp(const void *a, const void *b, size_t count)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;

    while (count-- > 0U) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}

/* ATA task-file registers (offsets from io_base / ctrl_base). */
#define ATA_REG_DATA       0U
#define ATA_REG_ERROR      1U
#define ATA_REG_FEATURES   1U
#define ATA_REG_SECCOUNT0  2U
#define ATA_REG_LBA0       3U
#define ATA_REG_LBA1       4U
#define ATA_REG_LBA2       5U
#define ATA_REG_HDDEVSEL   6U
#define ATA_REG_COMMAND    7U
#define ATA_REG_STATUS     7U
#define ATA_REG_ALTSTATUS  0U /* ctrl_base + 0 */
#define ATA_REG_DEVCONTROL 0U

#define ATA_SR_BSY  0x80U
#define ATA_SR_DRDY 0x40U
#define ATA_SR_DRQ  0x08U
#define ATA_SR_ERR  0x01U

#define ATA_CMD_READ_PIO     0x20U
#define ATA_CMD_IDENTIFY     0xECU
#define ATA_CMD_IDENTIFY_PKT 0xA1U
#define ATA_CMD_PACKET       0xA0U

#define ATAPI_SECTOR_SIZE 2048U

static void ata_delay(uint16_t ctrl_base)
{
    /* 400 ns delay: read alternate status four times. */
    (void)sg_amd64_inb(ctrl_base + ATA_REG_ALTSTATUS);
    (void)sg_amd64_inb(ctrl_base + ATA_REG_ALTSTATUS);
    (void)sg_amd64_inb(ctrl_base + ATA_REG_ALTSTATUS);
    (void)sg_amd64_inb(ctrl_base + ATA_REG_ALTSTATUS);
}

static bool ata_wait_not_bsy(uint16_t io_base, uint16_t ctrl_base)
{
    uint32_t timeout = 100000U;

    (void)ctrl_base;
    while (timeout-- > 0U) {
        uint8_t status = sg_amd64_inb(io_base + ATA_REG_STATUS);

        if ((status & ATA_SR_BSY) == 0U) {
            return true;
        }
        sg_amd64_cpu_relax();
    }
    return false;
}

static bool ata_wait_drq(uint16_t io_base, uint16_t ctrl_base)
{
    uint32_t timeout = 100000U;

    (void)ctrl_base;
    while (timeout-- > 0U) {
        uint8_t status = sg_amd64_inb(io_base + ATA_REG_STATUS);

        if ((status & ATA_SR_ERR) != 0U) {
            return false;
        }
        if ((status & ATA_SR_BSY) == 0U && (status & ATA_SR_DRQ) != 0U) {
            return true;
        }
        sg_amd64_cpu_relax();
    }
    return false;
}

static void ata_select(uint16_t io_base, uint16_t ctrl_base, uint8_t drive,
                       uint8_t lba_high4)
{
    sg_amd64_outb(io_base + ATA_REG_HDDEVSEL,
                  (uint8_t)(0xE0U | ((drive & 1U) << 4) | (lba_high4 & 0x0FU)));
    ata_delay(ctrl_base);
}

static bool ata_identify_one(uint16_t io_base, uint16_t ctrl_base,
                             uint8_t drive, sg_block_dev_t *out)
{
    uint16_t buf[256];
    uint8_t status;
    uint8_t cl;
    uint8_t ch;
    uint32_t i;

    /* Disable interrupts (nIEN). */
    sg_amd64_outb(ctrl_base + ATA_REG_DEVCONTROL, 0x02U);

    ata_select(io_base, ctrl_base, drive, 0U);
    sg_amd64_outb(io_base + ATA_REG_SECCOUNT0, 0U);
    sg_amd64_outb(io_base + ATA_REG_LBA0, 0U);
    sg_amd64_outb(io_base + ATA_REG_LBA1, 0U);
    sg_amd64_outb(io_base + ATA_REG_LBA2, 0U);
    sg_amd64_outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(ctrl_base);

    status = sg_amd64_inb(io_base + ATA_REG_STATUS);
    if (status == 0U || status == 0xFFU) {
        return false;
    }

    if (!ata_wait_not_bsy(io_base, ctrl_base)) {
        return false;
    }

    cl = sg_amd64_inb(io_base + ATA_REG_LBA1);
    ch = sg_amd64_inb(io_base + ATA_REG_LBA2);

    out->type = SG_BLOCK_NONE;
    if (cl == 0x14U && ch == 0xEBU) {
        /* ATAPI: issue IDENTIFY PACKET DEVICE. */
        out->type = SG_BLOCK_ATAPI;
        sg_amd64_outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PKT);
        ata_delay(ctrl_base);
        if (!ata_wait_drq(io_base, ctrl_base)) {
            return false;
        }
    } else if (cl == 0U && ch == 0U) {
        out->type = SG_BLOCK_ATA;
        if (!ata_wait_drq(io_base, ctrl_base)) {
            return false;
        }
    } else {
        /* Unknown device signature. */
        return false;
    }

    for (i = 0U; i < 256U; i++) {
        buf[i] = sg_amd64_inw(io_base + ATA_REG_DATA);
    }

    out->io_base = io_base;
    out->ctrl_base = ctrl_base;
    out->drive = drive;
    out->sector_size = (out->type == SG_BLOCK_ATAPI) ? ATAPI_SECTOR_SIZE
                                                     : SG_BLOCK_SECTOR_SIZE;

    /* Model string: words 27-46, byte-swapped. */
    for (i = 0U; i < 20U; i++) {
        out->model[i * 2U] = (char)(buf[27U + i] >> 8);
        out->model[i * 2U + 1U] = (char)(buf[27U + i] & 0xFFU);
    }
    out->model[40] = '\0';
    /* Trim trailing spaces. */
    for (i = 40U; i > 0U; i--) {
        if (out->model[i - 1U] == ' ' || out->model[i - 1U] == '\0') {
            out->model[i - 1U] = '\0';
        } else {
            break;
        }
    }

    if (out->type == SG_BLOCK_ATA) {
        /* LBA28 sector count: words 60-61. */
        out->sectors = (uint32_t)buf[60] | ((uint32_t)buf[61] << 16);
        if (out->sectors == 0U) {
            out->sectors = 1U;
        }
    } else {
        /* ATAPI capacity is reported via READ CAPACITY; use a scan limit
         * instead of trusting identify for size here. */
        out->sectors = 0U;
    }

    return true;
}

int sg_block_probe(sg_block_dev_t *devs, int max_devs)
{
    static const struct {
        uint16_t io;
        uint16_t ctrl;
    } buses[] = {
        {0x1F0U, 0x3F6U}, /* primary */
        {0x170U, 0x376U}  /* secondary */
    };
    int found = 0;
    int bus;
    int drive;

    for (bus = 0; bus < 2 && found < max_devs; bus++) {
        for (drive = 0; drive < 2 && found < max_devs; drive++) {
            sg_block_dev_t tmp;

            blk_memset(&tmp, 0, sizeof(tmp));
            if (ata_identify_one(buses[bus].io, buses[bus].ctrl,
                                 (uint8_t)drive, &tmp)) {
                devs[found++] = tmp;
            }
        }
    }
    return found;
}

static bool ata_read_pio28(const sg_block_dev_t *dev, uint32_t lba,
                           uint32_t count, void *buffer)
{
    uint16_t *out = (uint16_t *)buffer;
    uint32_t sector;

    if (count == 0U || count > 256U) {
        return false;
    }
    if (lba > 0x0FFFFFFFU) {
        return false;
    }

    for (sector = 0U; sector < count; sector++) {
        uint32_t cur = lba + sector;

        if (!ata_wait_not_bsy(dev->io_base, dev->ctrl_base)) {
            return false;
        }
        ata_select(dev->io_base, dev->ctrl_base, dev->drive,
                   (uint8_t)((cur >> 24) & 0x0FU));
        sg_amd64_outb(dev->io_base + ATA_REG_FEATURES, 0x00U);
        sg_amd64_outb(dev->io_base + ATA_REG_SECCOUNT0, 1U);
        sg_amd64_outb(dev->io_base + ATA_REG_LBA0, (uint8_t)(cur & 0xFFU));
        sg_amd64_outb(dev->io_base + ATA_REG_LBA1,
                      (uint8_t)((cur >> 8) & 0xFFU));
        sg_amd64_outb(dev->io_base + ATA_REG_LBA2,
                      (uint8_t)((cur >> 16) & 0xFFU));
        sg_amd64_outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        ata_delay(dev->ctrl_base);

        if (!ata_wait_drq(dev->io_base, dev->ctrl_base)) {
            return false;
        }
        sg_amd64_insw(dev->io_base + ATA_REG_DATA, out, 256U);
        out += 256U;
    }
    return true;
}

static bool atapi_read_sector(const sg_block_dev_t *dev, uint32_t lba,
                              void *buffer)
{
    /* SCSI READ(12) packet, 12 bytes, padded to 6 words. */
    uint8_t packet[12];
    uint16_t *out = (uint16_t *)buffer;
    uint32_t byte_count;
    uint32_t words;
    uint8_t status;

    blk_memset(packet, 0, sizeof(packet));
    packet[0] = 0xA8U; /* READ(12) */
    packet[2] = (uint8_t)((lba >> 24) & 0xFFU);
    packet[3] = (uint8_t)((lba >> 16) & 0xFFU);
    packet[4] = (uint8_t)((lba >> 8) & 0xFFU);
    packet[5] = (uint8_t)(lba & 0xFFU);
    packet[9] = 1U; /* transfer length: 1 block */

    if (!ata_wait_not_bsy(dev->io_base, dev->ctrl_base)) {
        return false;
    }
    ata_select(dev->io_base, dev->ctrl_base, dev->drive, 0U);
    ata_delay(dev->ctrl_base);
    sg_amd64_outb(dev->io_base + ATA_REG_FEATURES, 0x00U);
    sg_amd64_outb(dev->io_base + ATA_REG_LBA1,
                  (uint8_t)(ATAPI_SECTOR_SIZE & 0xFFU));
    sg_amd64_outb(dev->io_base + ATA_REG_LBA2,
                  (uint8_t)((ATAPI_SECTOR_SIZE >> 8) & 0xFFU));
    sg_amd64_outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_PACKET);

    if (!ata_wait_drq(dev->io_base, dev->ctrl_base)) {
        return false;
    }

    /* Send 12-byte packet as 6 words. */
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[0] | ((uint16_t)packet[1] << 8));
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[2] | ((uint16_t)packet[3] << 8));
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[4] | ((uint16_t)packet[5] << 8));
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[6] | ((uint16_t)packet[7] << 8));
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[8] | ((uint16_t)packet[9] << 8));
    sg_amd64_outw(dev->io_base + ATA_REG_DATA,
                  (uint16_t)packet[10] | ((uint16_t)packet[11] << 8));

    if (!ata_wait_drq(dev->io_base, dev->ctrl_base)) {
        return false;
    }

    byte_count = (uint32_t)sg_amd64_inb(dev->io_base + ATA_REG_LBA1) |
                 ((uint32_t)sg_amd64_inb(dev->io_base + ATA_REG_LBA2) << 8);
    if (byte_count == 0U || byte_count > ATAPI_SECTOR_SIZE) {
        byte_count = ATAPI_SECTOR_SIZE;
    }
    words = (byte_count + 1U) / 2U;
    sg_amd64_insw(dev->io_base + ATA_REG_DATA, out, words);

    /* Drain status. */
    status = sg_amd64_inb(dev->io_base + ATA_REG_STATUS);
    (void)status;
    return true;
}

bool sg_block_read(const sg_block_dev_t *dev, uint32_t lba, uint32_t count,
                   void *buffer)
{
    uint8_t *out = (uint8_t *)buffer;

    if (dev == NULL || buffer == NULL || count == 0U) {
        return false;
    }
    if (dev->type == SG_BLOCK_ATA) {
        return ata_read_pio28(dev, lba, count, buffer);
    }
    if (dev->type == SG_BLOCK_ATAPI) {
        uint32_t i;

        for (i = 0U; i < count; i++) {
            if (!atapi_read_sector(dev, lba + i, out + i * ATAPI_SECTOR_SIZE)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *p)
{
    return (uint64_t)read_u32_le(p) |
           ((uint64_t)read_u32_le(p + 4U) << 32);
}

bool sg_block_find_image(const sg_block_dev_t *dev, uint8_t *out,
                         size_t out_cap, size_t *out_size,
                         uint32_t max_sectors)
{
    /* Scan the device looking for the SparseKernel magic. Read 2 KiB windows
     * and search for the magic at sector-aligned offsets within each window,
     * so images placed at any sector boundary are found. When a candidate is
     * located, read and verify the full contiguous image. This is
     * filesystem-agnostic and works for images stored on ATA HDDs/SSDs (IDE
     * compatibility mode) and ATAPI CD/DVD-ROMs. Native USB/AHCI are not
     * driven directly here. */
    uint8_t window[4096];
    uint32_t window_sectors;
    uint32_t step;
    uint32_t lba;
    uint32_t limit;

    if (dev == NULL || out == NULL || out_cap < GOTH_SPARSE_HEADER_SIZE) {
        return false;
    }
    if (out_size != NULL) {
        *out_size = 0U;
    }

    if (dev->sector_size != 512U && dev->sector_size != 2048U) {
        return false;
    }
    window_sectors = (dev->sector_size == 2048U) ? 2U : 4U; /* 4 KiB window */
    if (window_sectors * dev->sector_size > sizeof(window)) {
        window_sectors = 1U;
    }
    step = window_sectors;

    limit = max_sectors;
    if (dev->type == SG_BLOCK_ATA && dev->sectors > 0U &&
        dev->sectors < limit) {
        limit = dev->sectors;
    }

    for (lba = 0U; lba + window_sectors <= limit; lba += step) {
        uint32_t off;
        uint32_t count = window_sectors;

        if (!sg_block_read(dev, lba, count, window)) {
            /* Retry one sector at a time to tolerate partial failures. */
            count = 1U;
            if (!sg_block_read(dev, lba, 1U, window)) {
                continue;
            }
        }

        for (off = 0U; off + GOTH_SPARSE_HEADER_SIZE <= count * dev->sector_size;
             off += dev->sector_size) {
            const uint8_t *scan = window + off;
            uint64_t total_size;
            uint32_t image_lba;
            uint32_t sectors_needed;
            uint32_t i;
            goth_sparse_image_t img;

            if (blk_memcmp(scan, goth_sparse_magic, GOTH_SPARSE_MAGIC_SIZE) !=
                0) {
                continue;
            }
            if (read_u16_le(scan + 8U) != GOTH_SPARSE_VERSION ||
                read_u16_le(scan + 10U) != GOTH_SPARSE_HEADER_SIZE) {
                continue;
            }
            total_size = read_u64_le(scan + 32U);
            if (total_size < GOTH_SPARSE_HEADER_SIZE ||
                total_size > (uint64_t)out_cap) {
                continue;
            }

            image_lba = lba + (off / dev->sector_size);
            sectors_needed =
                (uint32_t)((total_size + dev->sector_size - 1U) /
                           dev->sector_size);
            blk_memset(out, 0, (size_t)total_size);
            for (i = 0U; i < sectors_needed; i++) {
                if (!sg_block_read(dev, image_lba + i, 1U,
                                   out + (size_t)i * dev->sector_size)) {
                    goto next_window;
                }
            }

            if (goth_sparse_open(out, (size_t)total_size, &img) !=
                GOTH_SPARSE_OK) {
                goto next_window;
            }
            if (goth_sparse_verify(&img) != GOTH_SPARSE_OK) {
                goto next_window;
            }
            if (out_size != NULL) {
                *out_size = (size_t)total_size;
            }
            return true;
        }
    next_window:
        continue;
    }

    return false;
}
