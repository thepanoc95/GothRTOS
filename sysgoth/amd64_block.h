#ifndef SYSGOTH_AMD64_BLOCK_H
#define SYSGOTH_AMD64_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Minimal ATA PIO / ATAPI block access for the AMD64 SYSGOTH bootloader.
 *
 * This probes the legacy IDE buses (primary 0x1F0, secondary 0x170) for
 * ATA disks (HDD/SSD in IDE compatibility mode) and ATAPI devices
 * (CD/DVD-ROM). It can read sectors and scan a device for a
 * SparseKernel.img3 image by searching for the format's magic.
 *
 * Native USB mass storage and AHCI require substantial controller drivers
 * and are not implemented here; on machines that expose removable media
 * through IDE compatibility they are still reachable. */

#define SG_BLOCK_SECTOR_SIZE 512U
#define SG_BLOCK_ATAPI_SECTOR 2048U

typedef enum {
    SG_BLOCK_NONE = 0,
    SG_BLOCK_ATA,    /* HDD / SSD */
    SG_BLOCK_ATAPI   /* CD / DVD-ROM */
} sg_block_type_t;

typedef struct {
    sg_block_type_t type;
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t drive;       /* 0 = master, 1 = slave */
    uint32_t sectors;    /* LBA28 sector count (ATA) or approximate (ATAPI) */
    uint32_t sector_size;
    char model[41];
} sg_block_dev_t;

#define SG_BLOCK_MAX_DEVS 4U

/* Probe all legacy IDE positions. Returns the number of devices found. */
int sg_block_probe(sg_block_dev_t *devs, int max_devs);

/* Read `count` sectors starting at LBA `lba` into `buffer`. `buffer` must
 * hold `count * dev->sector_size` bytes. */
bool sg_block_read(const sg_block_dev_t *dev,
                   uint32_t lba,
                   uint32_t count,
                   void *buffer);

/* Scan a block device for a SparseKernel.img3. Searches up to `max_sectors`
 * sectors (from LBA 0) for the magic, reads and validates the full image
 * into `out` (capacity `out_cap`), and returns true on success. */
bool sg_block_find_image(const sg_block_dev_t *dev,
                         uint8_t *out,
                         size_t out_cap,
                         size_t *out_size,
                         uint32_t max_sectors);

#endif
