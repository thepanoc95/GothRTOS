/* Freestanding mem and str helpers for the AMD64 bare-metal port.
 *
 * rtos/ and the bare-metal ports are -ffreestanding -fno-builtin and do not
 * link a C library, so the port provides the small subset it needs. This is
 * the same set as the ARM32 port (pexpert/arm/arm32_libc.c). */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;

    while (count-- > 0U) {
        *to++ = *from++;
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *to = (unsigned char *)destination;
    const unsigned char *from = (const unsigned char *)source;

    if (to < from) {
        while (count-- > 0U) {
            *to++ = *from++;
        }
    } else if (to > from) {
        to += count;
        from += count;
        while (count-- > 0U) {
            *--to = *--from;
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *to = (unsigned char *)destination;

    while (count-- > 0U) {
        *to++ = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;

    while (count-- > 0U) {
        if (*a != *b) {
            return (int)*a - (int)*b;
        }
        a++;
        b++;
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int strncmp(const char *left, const char *right, size_t count)
{
    while (count > 0U && *left != '\0' && *left == *right) {
        left++;
        right++;
        count--;
    }
    if (count == 0U) {
        return 0;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}
