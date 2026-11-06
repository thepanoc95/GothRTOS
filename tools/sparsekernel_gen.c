/* sparsekernel-gen: builds a SparseKernel.img3 from a linked kernel ELF.
 *
 * The embedded build links a freestanding GothRTOS image and runs this tool on
 * the result, so the SYSGOTH bootloader has something to stream over the UART.
 * It walks the ELF PT_LOAD segments and encodes each one through the shared
 * sparse encoder, so the format has a single implementation across the host
 * tool, the bootloader and the kernel.
 *
 *   sparsekernel-gen <kernel.elf> <SparseKernel.img3>
 *   sparsekernel-gen --raw <load-address> <kernel.bin> <SparseKernel.img3>
 *
 * Only little-endian ELF32 and ELF64 are handled; every GothRTOS target is
 * little-endian. */

#include "gothrtos/sparsekernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEGMENTS 8U

typedef struct {
    uint64_t address;
    const uint8_t *data;
    uint64_t file_size;
    uint64_t memory_size;
} segment_t;

typedef struct {
    FILE *file;
    uint64_t total;
} sink_t;

static bool sink_write(void *context, const void *data, size_t size)
{
    sink_t *sink = (sink_t *)context;

    if (fwrite(data, 1U, size, sink->file) != size) {
        return false;
    }
    sink->total += size;
    return true;
}

static uint8_t *read_file(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint8_t *bytes;

    if (file == NULL) {
        fprintf(stderr, "sparsekernel-gen: cannot open %s\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)size + 1U);
    if (bytes == NULL) {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(bytes, 1U, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size_out = (size_t)size;
    return bytes;
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) |
           ((uint64_t)read_u32(bytes + 4) << 32);
}

static bool parse_elf(const uint8_t *bytes,
                      size_t size,
                      segment_t *segments,
                      size_t *segment_count,
                      uint64_t *entry,
                      uint32_t *arch)
{
    uint16_t machine;
    uint16_t phnum;
    uint16_t phentsize;
    uint64_t phoff;
    size_t count = 0U;
    uint16_t index;

    if (size < 64U || bytes[0] != 0x7f || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F') {
        fprintf(stderr, "sparsekernel-gen: not an ELF file\n");
        return false;
    }
    /* EI_CLASS is bytes[4] (1 = 32-bit, 2 = 64-bit); EI_DATA is bytes[5]
     * (1 = little-endian). */
    if (bytes[5] != 1) {
        fprintf(stderr, "sparsekernel-gen: only little-endian ELF\n");
        return false;
    }

    machine = read_u16(bytes + 18U);
    if (machine == 0x28U) {
        *arch = GOTH_SPARSE_ARCH_ARM32;
    } else if (machine == 0x3eU) {
        *arch = GOTH_SPARSE_ARCH_AMD64;
    } else {
        fprintf(stderr, "sparsekernel-gen: unknown machine 0x%x\n", machine);
        return false;
    }

    if (bytes[4] == 1U) { /* ELF32 */
        phoff = read_u32(bytes + 28U);
        phentsize = read_u16(bytes + 42U);
        phnum = read_u16(bytes + 44U);
        *entry = read_u32(bytes + 24U);
    } else if (bytes[4] == 2U) { /* ELF64 */
        phoff = read_u64(bytes + 32U);
        phentsize = read_u16(bytes + 54U);
        phnum = read_u16(bytes + 56U);
        *entry = read_u64(bytes + 24U);
    } else {
        fprintf(stderr, "sparsekernel-gen: unsupported ELF class\n");
        return false;
    }

    for (index = 0U; index < phnum; index++) {
        const uint8_t *ph = bytes + phoff + (uint64_t)index * phentsize;
        uint32_t type;
        uint64_t offset;
        uint64_t filesz;

        if ((size_t)(ph - bytes) + phentsize > size) {
            fprintf(stderr, "sparsekernel-gen: program header out of file\n");
            return false;
        }
        type = read_u32(ph);
        if (type != 1U) { /* PT_LOAD */
            continue;
        }
        if (count >= MAX_SEGMENTS) {
            fprintf(stderr, "sparsekernel-gen: too many segments\n");
            return false;
        }

        if (bytes[4] == 1U) {
            offset = read_u32(ph + 4U);
            segments[count].address = read_u32(ph + 8U);
            filesz = read_u32(ph + 16U);
            segments[count].memory_size = read_u32(ph + 20U);
        } else {
            offset = read_u64(ph + 8U);
            segments[count].address = read_u64(ph + 16U);
            filesz = read_u64(ph + 32U);
            segments[count].memory_size = read_u64(ph + 40U);
        }
        if (offset + filesz > (uint64_t)size) {
            fprintf(stderr, "sparsekernel-gen: segment out of file\n");
            return false;
        }
        segments[count].data = bytes + offset;
        segments[count].file_size = filesz;
        count++;
    }

    *segment_count = count;
    return true;
}

int main(int argc, char *argv[])
{
    segment_t segments[MAX_SEGMENTS];
    goth_sparse_segment_t sparse[MAX_SEGMENTS];
    size_t segment_count = 0U;
    uint64_t entry = 0U;
    uint32_t arch = GOTH_SPARSE_ARCH_ARM32;
    uint8_t *file_bytes = NULL;
    size_t file_size = 0U;
    const char *output_path;
    sink_t sink;
    goth_sparse_status_t status;
    size_t index;

    if (argc != 3 && argc != 5) {
        fprintf(stderr,
                "usage: %s <kernel.elf> <SparseKernel.img3>\n"
                "       %s --raw <load-address> <kernel.bin> <out.img3>\n",
                argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--raw") == 0) {
        file_bytes = read_file(argv[3], &file_size);
        if (file_bytes == NULL) {
            return 1;
        }
        segments[0].address = strtoull(argv[2], NULL, 0);
        segments[0].data = file_bytes;
        segments[0].file_size = file_size;
        segments[0].memory_size = file_size;
        segment_count = 1U;
        entry = segments[0].address;
        output_path = argv[4];
        fprintf(stderr,
                "sparsekernel-gen: raw image, entry 0x%llx, %zu bytes\n",
                (unsigned long long)entry, file_size);
    } else {
        file_bytes = read_file(argv[1], &file_size);
        if (file_bytes == NULL) {
            return 1;
        }
        if (!parse_elf(file_bytes, file_size, segments, &segment_count,
                       &entry, &arch)) {
            free(file_bytes);
            return 1;
        }
        output_path = argv[2];
        fprintf(stderr,
                "sparsekernel-gen: %zu loadable segment(s), entry 0x%llx, "
                "arch %s\n",
                segment_count, (unsigned long long)entry,
                goth_sparse_arch_name(arch));
    }

    for (index = 0U; index < segment_count; index++) {
        sparse[index].load_address = segments[index].address;
        sparse[index].data = segments[index].data;
        sparse[index].file_size = segments[index].file_size;
        sparse[index].memory_size = segments[index].memory_size;
        sparse[index].flags = 0U;
    }

    sink.file = fopen(output_path, "wb");
    sink.total = 0U;
    if (sink.file == NULL) {
        fprintf(stderr, "sparsekernel-gen: cannot write %s\n", output_path);
        free(file_bytes);
        return 1;
    }

    status = goth_sparse_encode(sparse, segment_count, entry, arch, 0U, 0U,
                                sink_write, &sink, NULL);
    fclose(sink.file);
    free(file_bytes);

    if (status != GOTH_SPARSE_OK) {
        fprintf(stderr, "sparsekernel-gen: encode failed (%d)\n",
                (int)status);
        return 1;
    }

    fprintf(stderr, "sparsekernel-gen: wrote %s (%llu bytes)\n", output_path,
            (unsigned long long)sink.total);
    return 0;
}
