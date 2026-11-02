#include <errno.h>
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

void *memchr(const void *memory, int character, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)memory;
    unsigned char target = (unsigned char)character;
    size_t index;

    for (index = 0U; index < count; index++) {
        if (bytes[index] == target) {
            return (void *)(bytes + index);
        }
    }
    return NULL;
}

char *strncat(char *destination, const char *source, size_t count)
{
    char *result = destination;

    while (*destination != '\0') {
        destination++;
    }
    while (count > 0U && *source != '\0') {
        *destination++ = *source++;
        count--;
    }
    *destination = '\0';
    return result;
}

char *strstr(const char *text, const char *needle)
{
    size_t needle_length;

    if (*needle == '\0') {
        return (char *)text;
    }
    needle_length = strlen(needle);
    while (*text != '\0') {
        if (*text == *needle &&
            strncmp(text, needle, needle_length) == 0) {
            return (char *)text;
        }
        text++;
    }
    return NULL;
}

char *strpbrk(const char *text, const char *accept)
{
    while (*text != '\0') {
        if (strchr(accept, *text) != NULL) {
            return (char *)text;
        }
        text++;
    }
    return NULL;
}

size_t strspn(const char *text, const char *accept)
{
    size_t length = 0U;

    while (text[length] != '\0' &&
           strchr(accept, text[length]) != NULL) {
        length++;
    }
    return length;
}

size_t strcspn(const char *text, const char *reject)
{
    size_t length = 0U;

    while (text[length] != '\0' &&
           strchr(reject, text[length]) == NULL) {
        length++;
    }
    return length;
}

int strcoll(const char *left, const char *right)
{
    return strcmp(left, right);
}

char *strerror(int error_number)
{
    switch (error_number) {
    case 0:
        return (char *)"no error";
    case EPERM:
        return (char *)"operation not permitted";
    case ENOENT:
        return (char *)"no such file or directory";
    case EINTR:
        return (char *)"interrupted system call";
    case EIO:
        return (char *)"input/output error";
    case EAGAIN:
        return (char *)"resource temporarily unavailable";
    case ENOMEM:
        return (char *)"out of memory";
    case EACCES:
        return (char *)"permission denied";
    case EEXIST:
        return (char *)"file exists";
    case ENOTDIR:
        return (char *)"not a directory";
    case EISDIR:
        return (char *)"is a directory";
    case EINVAL:
        return (char *)"invalid argument";
    case EMFILE:
        return (char *)"too many open files";
    case ESPIPE:
        return (char *)"illegal seek";
    case EDOM:
        return (char *)"domain error";
    case ERANGE:
        return (char *)"result out of range";
    case ENOSYS:
        return (char *)"function not implemented";
    case ENAMETOOLONG:
        return (char *)"file name too long";
    case ENOTEMPTY:
        return (char *)"directory not empty";
    case ELOOP:
        return (char *)"too many symbolic links";
    case EOVERFLOW:
        return (char *)"value too large";
    default:
        return (char *)"unknown error";
    }
}
