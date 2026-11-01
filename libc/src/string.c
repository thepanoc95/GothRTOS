#include <string.h>

#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    size_t index;

    for (index = 0U; index < count; index++) {
        out[index] = in[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;

    if ((uintptr_t)out < (uintptr_t)in) {
        size_t index;

        for (index = 0U; index < count; index++) {
            out[index] = in[index];
        }
    } else if ((uintptr_t)out > (uintptr_t)in) {
        while (count > 0U) {
            count--;
            out[count] = in[count];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *out = (unsigned char *)destination;
    size_t index;

    for (index = 0U; index < count; index++) {
        out[index] = (unsigned char)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    size_t index;

    for (index = 0U; index < count; index++) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
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

size_t strnlen(const char *text, size_t max_length)
{
    size_t length = 0U;

    while (length < max_length && text[length] != '\0') {
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
    return (unsigned char)*left < (unsigned char)*right
               ? -1
               : (unsigned char)*left != (unsigned char)*right;
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
    return (unsigned char)*left < (unsigned char)*right
               ? -1
               : (unsigned char)*left != (unsigned char)*right;
}

char *strcpy(char *destination, const char *source)
{
    char *result = destination;

    while ((*destination++ = *source++) != '\0') {
    }
    return result;
}

char *strncpy(char *destination, const char *source, size_t count)
{
    char *result = destination;
    size_t index = 0U;

    while (index < count && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }
    while (index < count) {
        destination[index++] = '\0';
    }
    return result;
}

char *strcat(char *destination, const char *source)
{
    char *result = destination;

    while (*destination != '\0') {
        destination++;
    }
    while ((*destination++ = *source++) != '\0') {
    }
    return result;
}

char *strchr(const char *text, int character)
{
    unsigned char target = (unsigned char)character;

    for (;;) {
        if ((unsigned char)*text == target) {
            return (char *)text;
        }
        if (*text == '\0') {
            return NULL;
        }
        text++;
    }
}

char *strrchr(const char *text, int character)
{
    const char *match = NULL;
    unsigned char target = (unsigned char)character;

    for (;;) {
        if ((unsigned char)*text == target) {
            match = text;
        }
        if (*text == '\0') {
            return (char *)match;
        }
        text++;
    }
}
