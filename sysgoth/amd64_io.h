#ifndef SYSGOTH_AMD64_IO_H
#define SYSGOTH_AMD64_IO_H

#include <stdint.h>

/* x86 port I/O helpers, freestanding. */

static inline void sg_amd64_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t sg_amd64_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t sg_amd64_inw(uint16_t port)
{
    uint16_t value;

    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void sg_amd64_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void sg_amd64_insw(uint16_t port, void *buffer, uint32_t count)
{
    __asm__ volatile("rep insw"
                     : "+D"(buffer), "+c"(count)
                     : "d"(port)
                     : "memory");
}

static inline void sg_amd64_io_wait(void)
{
    /* Port 0x80 is traditionally used for short I/O delays. */
    sg_amd64_outb(0x80, 0);
}

#endif
