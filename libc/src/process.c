#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "goth_syscall.h"

char **goth_libc_environ;

void exit(int status)
{
    goth_sys_exit(status);
}

void _Exit(int status)
{
    goth_sys_exit(status);
}

void abort(void)
{
    goth_sys_exit(128 + 6);
}

char *getenv(const char *name)
{
    size_t name_length;
    char **entry;

    if (name == NULL || *name == '\0' || goth_libc_environ == NULL) {
        return NULL;
    }
    name_length = strlen(name);
    for (entry = goth_libc_environ; *entry != NULL; entry++) {
        if (strncmp(*entry, name, name_length) == 0 &&
            (*entry)[name_length] == '=') {
            return *entry + name_length + 1U;
        }
    }
    return NULL;
}

int system(const char *command)
{
    if (command == NULL) {
        return 1;
    }
    errno = ENOSYS;
    return -1;
}

static void swap_bytes(unsigned char *left, unsigned char *right, size_t size)
{
    size_t index;

    for (index = 0U; index < size; index++) {
        unsigned char temporary = left[index];

        left[index] = right[index];
        right[index] = temporary;
    }
}

static void sift_down(unsigned char *base, size_t start, size_t end, size_t size,
                      int (*compare)(const void *, const void *))
{
    size_t root = start;

    for (;;) {
        size_t child = root * 2U + 1U;

        if (child >= end) {
            break;
        }
        if (child + 1U < end &&
            compare(base + child * size, base + (child + 1U) * size) < 0) {
            child++;
        }
        if (compare(base + root * size, base + child * size) >= 0) {
            break;
        }
        swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *))
{
    unsigned char *bytes = (unsigned char *)base;
    size_t index;
    size_t end;

    if (base == NULL || compare == NULL || size == 0U || count < 2U) {
        return;
    }

    for (index = count / 2U; index > 0U; index--) {
        sift_down(bytes, index - 1U, count, size, compare);
    }
    for (end = count; end > 1U; end--) {
        swap_bytes(bytes, bytes + (end - 1U) * size, size);
        sift_down(bytes, 0U, end - 1U, size, compare);
    }
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *))
{
    const unsigned char *bytes = (const unsigned char *)base;
    size_t low = 0U;
    size_t high = count;

    if (base == NULL || compare == NULL || size == 0U) {
        return NULL;
    }
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        int order = compare(key, bytes + middle * size);

        if (order == 0) {
            return (void *)(bytes + middle * size);
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1U;
        }
    }
    return NULL;
}
