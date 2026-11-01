#include <errno.h>
#include <gothrtos/libc.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define GOTH_LIBC_BLOCK_MAGIC UINT32_C(0x474C4942)

typedef union goth_libc_block goth_libc_block_t;

union goth_libc_block {
    struct {
        size_t size;
        uint32_t magic;
        unsigned int is_free;
        goth_libc_block_t *previous;
        goth_libc_block_t *next;
    } fields;
    max_align_t alignment;
};

int errno;

static goth_libc_block_t *heap_first;
static uintptr_t heap_begin;
static uintptr_t heap_end;

static size_t block_header_size(void)
{
    return sizeof(goth_libc_block_t);
}

static size_t align_size(size_t size)
{
    const size_t alignment = _Alignof(max_align_t);

    if (size > SIZE_MAX - (alignment - 1U)) {
        return 0U;
    }
    return (size + alignment - 1U) & ~(alignment - 1U);
}

static int heap_contains(const void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    return heap_first != NULL && address >= heap_begin && address < heap_end;
}

static goth_libc_block_t *block_from_pointer(const void *pointer)
{
    goth_libc_block_t *candidate;
    goth_libc_block_t *block;

    if (!heap_contains(pointer) ||
        (uintptr_t)pointer < heap_begin + block_header_size()) {
        return NULL;
    }
    candidate = (goth_libc_block_t *)((unsigned char *)pointer -
                                      block_header_size());
    for (block = heap_first; block != NULL && block != candidate;
         block = block->fields.next) {
    }
    if (block == NULL ||
        (uintptr_t)block > heap_end - block_header_size() ||
        block->fields.magic != GOTH_LIBC_BLOCK_MAGIC ||
        block->fields.size >
            (size_t)(heap_end - (uintptr_t)block - block_header_size())) {
        return NULL;
    }
    return block;
}

int goth_libc_heap_init(void *memory, size_t size)
{
    uintptr_t raw_begin;
    uintptr_t begin;
    uintptr_t end;
    size_t alignment = _Alignof(max_align_t);
    goth_libc_block_t *block;

    if (memory == NULL || size <= block_header_size()) {
        errno = EINVAL;
        return -1;
    }
    raw_begin = (uintptr_t)memory;
    if (raw_begin > UINTPTR_MAX - (alignment - 1U) ||
        size > UINTPTR_MAX - raw_begin) {
        errno = EINVAL;
        return -1;
    }

    begin = (raw_begin + alignment - 1U) & ~(uintptr_t)(alignment - 1U);
    end = (raw_begin + size) & ~(uintptr_t)(alignment - 1U);
    if (end <= begin || end - begin <= block_header_size()) {
        errno = EINVAL;
        return -1;
    }

    heap_begin = begin;
    heap_end = end;
    block = (goth_libc_block_t *)begin;
    block->fields.size = (size_t)(end - begin) - block_header_size();
    block->fields.magic = GOTH_LIBC_BLOCK_MAGIC;
    block->fields.is_free = 1U;
    block->fields.previous = NULL;
    block->fields.next = NULL;
    heap_first = block;
    errno = 0;
    return 0;
}

void goth_libc_heap_reset(void)
{
    heap_first = NULL;
    heap_begin = 0U;
    heap_end = 0U;
}

size_t goth_libc_heap_available(void)
{
    const goth_libc_block_t *block;
    size_t available = 0U;

    for (block = heap_first; block != NULL; block = block->fields.next) {
        if (block->fields.magic == GOTH_LIBC_BLOCK_MAGIC &&
            block->fields.is_free != 0U &&
            block->fields.size <= SIZE_MAX - available) {
            available += block->fields.size;
        }
    }
    return available;
}

void *malloc(size_t size)
{
    goth_libc_block_t *block;
    size_t wanted;
    size_t header_size = block_header_size();

    if (size == 0U) {
        return NULL;
    }
    if (heap_first == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    wanted = align_size(size);
    if (wanted == 0U) {
        errno = ENOMEM;
        return NULL;
    }

    for (block = heap_first; block != NULL; block = block->fields.next) {
        size_t remainder;
        goth_libc_block_t *split;

        if (block->fields.magic != GOTH_LIBC_BLOCK_MAGIC) {
            errno = EINVAL;
            return NULL;
        }
        if (block->fields.is_free == 0U || block->fields.size < wanted) {
            continue;
        }

        remainder = block->fields.size - wanted;
        if (remainder >= header_size + _Alignof(max_align_t)) {
            split = (goth_libc_block_t *)((unsigned char *)block +
                                          header_size + wanted);
            split->fields.size = remainder - header_size;
            split->fields.magic = GOTH_LIBC_BLOCK_MAGIC;
            split->fields.is_free = 1U;
            split->fields.previous = block;
            split->fields.next = block->fields.next;
            if (split->fields.next != NULL) {
                split->fields.next->fields.previous = split;
            }
            block->fields.next = split;
            block->fields.size = wanted;
        }
        block->fields.is_free = 0U;
        errno = 0;
        return (unsigned char *)block + header_size;
    }

    errno = ENOMEM;
    return NULL;
}

void free(void *pointer)
{
    goth_libc_block_t *block;

    if (pointer == NULL) {
        return;
    }
    block = block_from_pointer(pointer);
    if (block == NULL || block->fields.is_free != 0U) {
        errno = EINVAL;
        return;
    }

    block->fields.is_free = 1U;
    if (block->fields.next != NULL &&
        block->fields.next->fields.magic == GOTH_LIBC_BLOCK_MAGIC &&
        block->fields.next->fields.is_free != 0U) {
        goth_libc_block_t *next = block->fields.next;

        block->fields.size += block_header_size() + next->fields.size;
        block->fields.next = next->fields.next;
        if (block->fields.next != NULL) {
            block->fields.next->fields.previous = block;
        }
        next->fields.magic = 0U;
    }
    if (block->fields.previous != NULL &&
        block->fields.previous->fields.magic == GOTH_LIBC_BLOCK_MAGIC &&
        block->fields.previous->fields.is_free != 0U) {
        goth_libc_block_t *previous = block->fields.previous;

        previous->fields.size += block_header_size() + block->fields.size;
        previous->fields.next = block->fields.next;
        if (previous->fields.next != NULL) {
            previous->fields.next->fields.previous = previous;
        }
        block->fields.magic = 0U;
    }
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    unsigned char *memory;
    size_t index;

    if (size != 0U && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    total = count * size;
    memory = (unsigned char *)malloc(total);
    if (memory == NULL) {
        return NULL;
    }
    for (index = 0U; index < total; index++) {
        memory[index] = 0U;
    }
    return memory;
}

void *realloc(void *pointer, size_t size)
{
    goth_libc_block_t *block;
    size_t wanted;
    void *replacement;
    size_t copy_size;
    size_t index;

    if (pointer == NULL) {
        return malloc(size);
    }
    if (size == 0U) {
        free(pointer);
        return NULL;
    }

    block = block_from_pointer(pointer);
    if (block == NULL || block->fields.is_free != 0U) {
        errno = EINVAL;
        return NULL;
    }
    wanted = align_size(size);
    if (wanted == 0U) {
        errno = ENOMEM;
        return NULL;
    }
    if (wanted <= block->fields.size) {
        return pointer;
    }

    replacement = malloc(size);
    if (replacement == NULL) {
        return NULL;
    }
    copy_size = block->fields.size < size ? block->fields.size : size;
    for (index = 0U; index < copy_size; index++) {
        ((unsigned char *)replacement)[index] =
            ((const unsigned char *)pointer)[index];
    }
    free(pointer);
    return replacement;
}

static int ascii_space(unsigned char character)
{
    return character == ' ' || (character >= '\t' && character <= '\r');
}

static int digit_value(unsigned char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'Z') {
        return character - 'A' + 10;
    }
    return -1;
}

static unsigned long long parse_integer(const char *text,
                                        char **endptr,
                                        int base,
                                        unsigned long long limit,
                                        int *negative_out)
{
    const char *cursor = text;
    unsigned long long value = 0ULL;
    int negative = 0;
    int any = 0;
    int overflow = 0;

    if (text == NULL || base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr != NULL) {
            *endptr = (char *)text;
        }
        return 0UL;
    }

    while (ascii_space((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }
    if ((base == 0 || base == 16) && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value((unsigned char)cursor[2]) >= 0 &&
        digit_value((unsigned char)cursor[2]) < 16) {
        cursor += 2;
        base = 16;
    } else if (base == 0) {
        base = cursor[0] == '0' ? 8 : 10;
    }

    while (digit_value((unsigned char)*cursor) >= 0 &&
           digit_value((unsigned char)*cursor) < base) {
        unsigned int digit = (unsigned int)digit_value((unsigned char)*cursor);

        any = 1;
        if (value > (limit - digit) / (unsigned int)base) {
            overflow = 1;
        } else if (!overflow) {
            value = value * (unsigned int)base + digit;
        }
        cursor++;
    }

    if (endptr != NULL) {
        *endptr = (char *)(any ? cursor : text);
    }
    if (negative_out != NULL) {
        *negative_out = negative;
    }
    if (!any) {
        errno = 0;
        return 0UL;
    }
    if (overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    errno = 0;
    return value;
}

unsigned long long strtoull(const char *text, char **endptr, int base)
{
    int negative = 0;
    unsigned long long value = parse_integer(text, endptr, base, ULLONG_MAX,
                                             &negative);

    return negative ? 0UL - value : value;
}

unsigned long strtoul(const char *text, char **endptr, int base)
{
    int negative = 0;
    unsigned long long value = parse_integer(text, endptr, base, ULONG_MAX,
                                             &negative);

    return negative ? 0UL - (unsigned long)value : (unsigned long)value;
}

long long strtoll(const char *text, char **endptr, int base)
{
    unsigned long long magnitude;
    unsigned long long limit = (unsigned long long)LLONG_MAX;
    int negative = 0;

    if (text != NULL) {
        const char *cursor = text;

        while (ascii_space((unsigned char)*cursor)) {
            cursor++;
        }
        negative = *cursor == '-';
    }
    if (negative) {
        limit++;
    }
    magnitude = parse_integer(text, endptr, base, limit, NULL);
    if (errno == ERANGE) {
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    if (negative) {
        if (magnitude == (unsigned long long)LLONG_MAX + 1ULL) {
            return LLONG_MIN;
        }
        return -(long long)magnitude;
    }
    return (long long)magnitude;
}

long strtol(const char *text, char **endptr, int base)
{
    unsigned long long magnitude;
    unsigned long long limit = (unsigned long long)LONG_MAX;
    int negative = 0;

    if (text != NULL) {
        const char *cursor = text;

        while (ascii_space((unsigned char)*cursor)) {
            cursor++;
        }
        negative = *cursor == '-';
    }
    if (negative) {
        limit++;
    }
    magnitude = parse_integer(text, endptr, base, limit, NULL);
    if (errno == ERANGE) {
        return negative ? LONG_MIN : LONG_MAX;
    }
    if (negative) {
        if (magnitude == (unsigned long long)LONG_MAX + 1ULL) {
            return LONG_MIN;
        }
        return -(long)magnitude;
    }
    return (long)magnitude;
}

int atoi(const char *text)
{
    return (int)strtol(text, NULL, 10);
}

long atol(const char *text)
{
    return strtol(text, NULL, 10);
}

long long atoll(const char *text)
{
    return strtoll(text, NULL, 10);
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

long labs(long value)
{
    return value < 0L ? -value : value;
}
