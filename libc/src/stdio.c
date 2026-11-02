#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goth_syscall.h"

#define STB_SPRINTF_IMPLEMENTATION
#include "../third_party/stb_sprintf.h"

struct goth_libc_file {
    int descriptor;
    int eof;
    int error;
    int ungot;
    int persistent;
    size_t position;
    size_t length;
    unsigned char buffer[BUFSIZ];
};

static struct goth_libc_file standard_input = {
    0, 0, 0, -1, 1, 0U, 0U, {0U}
};
static struct goth_libc_file standard_output = {
    1, 0, 0, -1, 1, 0U, 0U, {0U}
};
static struct goth_libc_file standard_error = {
    2, 0, 0, -1, 1, 0U, 0U, {0U}
};

FILE *stdin = &standard_input;
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;

static void file_reset(FILE *stream)
{
    stream->eof = 0;
    stream->error = 0;
    stream->ungot = -1;
    stream->position = 0U;
    stream->length = 0U;
}

static int file_fill(FILE *stream)
{
    long result;

    if (stream->position < stream->length) {
        return 1;
    }
    if (stream->eof || stream->error) {
        return 0;
    }

    result = goth_sys_read(stream->descriptor, stream->buffer, BUFSIZ);
    if (result == 0) {
        stream->eof = 1;
        return 0;
    }
    if (result < 0) {
        stream->error = 1;
        return 0;
    }
    stream->position = 0U;
    stream->length = (size_t)result;
    return 1;
}

static FILE *file_allocate(int descriptor, int persistent)
{
    FILE *stream = (FILE *)calloc(1U, sizeof(*stream));

    if (stream == NULL) {
        return NULL;
    }
    stream->descriptor = descriptor;
    stream->persistent = persistent;
    stream->ungot = -1;
    return stream;
}

static int file_open_path(const char *path, const char *mode)
{
    int flags;
    char first;

    if (path == NULL || mode == NULL) {
        errno = EINVAL;
        return -1;
    }

    first = mode[0];
    if (first == 'r') {
        flags = GOTH_O_RDONLY;
    } else if (first == 'w') {
        flags = GOTH_O_WRONLY | GOTH_O_CREAT | GOTH_O_TRUNC;
    } else if (first == 'a') {
        flags = GOTH_O_WRONLY | GOTH_O_CREAT | GOTH_O_APPEND;
    } else {
        errno = EINVAL;
        return -1;
    }

    for (mode++; *mode != '\0'; mode++) {
        if (*mode == '+') {
            flags = (flags & ~(GOTH_O_RDONLY | GOTH_O_WRONLY)) | GOTH_O_RDWR;
        }
    }

    return (int)goth_sys_openat(GOTH_AT_FDCWD, path, flags, 0666);
}

FILE *fopen(const char *path, const char *mode)
{
    int descriptor = file_open_path(path, mode);
    FILE *stream;

    if (descriptor < 0) {
        return NULL;
    }
    stream = file_allocate(descriptor, 0);
    if (stream == NULL) {
        (void)goth_sys_close(descriptor);
    }
    return stream;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    int descriptor;

    if (stream == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (path == NULL) {
        return stream;
    }

    descriptor = file_open_path(path, mode);
    if (descriptor < 0) {
        return NULL;
    }
    (void)goth_sys_close(stream->descriptor);
    stream->descriptor = descriptor;
    file_reset(stream);
    return stream;
}

int fclose(FILE *stream)
{
    int result;

    if (stream == NULL) {
        errno = EINVAL;
        return EOF;
    }
    result = (int)goth_sys_close(stream->descriptor);
    if (!stream->persistent) {
        free(stream);
    }
    return result < 0 ? EOF : 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    unsigned char *destination = (unsigned char *)buffer;
    size_t wanted;
    size_t total = 0U;

    if (stream == NULL || buffer == NULL || size == 0U || count == 0U) {
        return 0U;
    }
    if (count > (size_t)-1 / size) {
        errno = EOVERFLOW;
        return 0U;
    }
    wanted = size * count;

    while (total < wanted) {
        int character = getc(stream);

        if (character == EOF) {
            break;
        }
        destination[total++] = (unsigned char)character;
    }
    return total / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    const unsigned char *source = (const unsigned char *)buffer;
    size_t wanted;
    size_t total = 0U;

    if (stream == NULL || buffer == NULL || size == 0U || count == 0U) {
        return 0U;
    }
    if (count > (size_t)-1 / size) {
        errno = EOVERFLOW;
        return 0U;
    }
    wanted = size * count;

    while (total < wanted) {
        long written = goth_sys_write(stream->descriptor, source + total,
                                      wanted - total);

        if (written <= 0) {
            stream->error = 1;
            break;
        }
        total += (size_t)written;
    }
    return total / size;
}

int getc(FILE *stream)
{
    if (stream == NULL) {
        errno = EINVAL;
        return EOF;
    }
    if (stream->ungot >= 0) {
        int character = stream->ungot;

        stream->ungot = -1;
        return character;
    }
    if (!file_fill(stream)) {
        return EOF;
    }
    return stream->buffer[stream->position++];
}

int fgetc(FILE *stream)
{
    return getc(stream);
}

int ungetc(int character, FILE *stream)
{
    if (stream == NULL || character == EOF) {
        return EOF;
    }
    stream->ungot = (unsigned char)character;
    stream->eof = 0;
    return (unsigned char)character;
}

char *fgets(char *buffer, int size, FILE *stream)
{
    int index = 0;

    if (buffer == NULL || size <= 0 || stream == NULL) {
        errno = EINVAL;
        return NULL;
    }

    while (index < size - 1) {
        int character = getc(stream);

        if (character == EOF) {
            break;
        }
        buffer[index++] = (char)character;
        if (character == '\n') {
            break;
        }
    }
    if (index == 0) {
        return NULL;
    }
    buffer[index] = '\0';
    return buffer;
}

int fputc(int character, FILE *stream)
{
    unsigned char value = (unsigned char)character;

    if (fwrite(&value, 1U, 1U, stream) != 1U) {
        return EOF;
    }
    return value;
}

int putc(int character, FILE *stream)
{
    return fputc(character, stream);
}

int fputs(const char *text, FILE *stream)
{
    size_t length;

    if (text == NULL || stream == NULL) {
        errno = EINVAL;
        return EOF;
    }
    length = strlen(text);
    return fwrite(text, 1U, length, stream) == length ? 0 : EOF;
}

int puts(const char *text)
{
    if (fputs(text, stdout) == EOF) {
        return EOF;
    }
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

int putchar(int character)
{
    return fputc(character, stdout);
}

int feof(FILE *stream)
{
    return stream != NULL && stream->eof;
}

int ferror(FILE *stream)
{
    return stream != NULL && stream->error;
}

void clearerr(FILE *stream)
{
    if (stream != NULL) {
        stream->eof = 0;
        stream->error = 0;
    }
}

int fseek(FILE *stream, long offset, int origin)
{
    long result;

    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (origin == GOTH_SEEK_CUR) {
        offset -= (long)(stream->length - stream->position);
        if (stream->ungot >= 0) {
            offset--;
        }
    }
    result = goth_sys_lseek(stream->descriptor, offset, origin);
    if (result < 0) {
        stream->error = 1;
        return -1;
    }
    file_reset(stream);
    return 0;
}

long ftell(FILE *stream)
{
    long position;

    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    position = goth_sys_lseek(stream->descriptor, 0L, GOTH_SEEK_CUR);
    if (position < 0) {
        stream->error = 1;
        return -1;
    }
    position -= (long)(stream->length - stream->position);
    if (stream->ungot >= 0) {
        position--;
    }
    return position;
}

void rewind(FILE *stream)
{
    if (stream != NULL) {
        (void)fseek(stream, 0L, GOTH_SEEK_SET);
        stream->error = 0;
    }
}

int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    (void)stream;
    (void)buffer;
    (void)mode;
    (void)size;
    return 0;
}

void setbuf(FILE *stream, char *buffer)
{
    (void)stream;
    (void)buffer;
}

int fileno(FILE *stream)
{
    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    return stream->descriptor;
}

int remove(const char *path)
{
    long result;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    result = goth_sys_unlinkat(GOTH_AT_FDCWD, path, 0);
    if (result < 0 && errno == EISDIR) {
        result = goth_sys_unlinkat(GOTH_AT_FDCWD, path, GOTH_AT_REMOVEDIR);
    }
    return result < 0 ? -1 : 0;
}

int rename(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    return goth_sys_renameat(GOTH_AT_FDCWD, old_path, GOTH_AT_FDCWD,
                             new_path) < 0
               ? -1
               : 0;
}

char *tmpnam(char *buffer)
{
    static char storage[L_tmpnam];
    static unsigned long counter;
    char *result = buffer != NULL ? buffer : storage;

    (void)snprintf(result, L_tmpnam, "/tmp/lua_%ld_%lu",
                   goth_sys_getpid(), counter++);
    return result;
}

FILE *tmpfile(void)
{
    unsigned int attempt;

    for (attempt = 0U; attempt < 100U; attempt++) {
        char name[L_tmpnam];
        int descriptor;

        (void)tmpnam(name);
        descriptor = (int)goth_sys_openat(GOTH_AT_FDCWD, name,
                                          GOTH_O_RDWR | GOTH_O_CREAT |
                                              GOTH_O_EXCL,
                                          0600);
        if (descriptor >= 0) {
            FILE *stream;

            (void)goth_sys_unlinkat(GOTH_AT_FDCWD, name, 0);
            stream = file_allocate(descriptor, 0);
            if (stream == NULL) {
                (void)goth_sys_close(descriptor);
            }
            return stream;
        }
        if (errno != EEXIST) {
            return NULL;
        }
    }
    errno = EEXIST;
    return NULL;
}

int vsnprintf(char *buffer, size_t size, const char *format,
              va_list arguments)
{
    if (size > (size_t)INT_MAX) {
        size = (size_t)INT_MAX;
    }
    return stbsp_vsnprintf(buffer, (int)size, format, arguments);
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int vsprintf(char *buffer, const char *format, va_list arguments)
{
    return stbsp_vsprintf(buffer, format, arguments);
}

int sprintf(char *buffer, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return result;
}

int vfprintf(FILE *stream, const char *format, va_list arguments)
{
    char local[512];
    va_list copy;
    int length;

    va_copy(copy, arguments);
    length = stbsp_vsnprintf(local, (int)sizeof(local), format, copy);
    va_end(copy);

    if (length < 0) {
        stream->error = 1;
        return -1;
    }
    if ((size_t)length < sizeof(local)) {
        return fwrite(local, 1U, (size_t)length, stream) == (size_t)length
                   ? length
                   : -1;
    }

    {
        char *dynamic = (char *)malloc((size_t)length + 1U);
        int written;

        if (dynamic == NULL) {
            stream->error = 1;
            return -1;
        }
        written = stbsp_vsnprintf(dynamic, length + 1, format, arguments);
        if (written < 0 ||
            fwrite(dynamic, 1U, (size_t)written, stream) !=
                (size_t)written) {
            free(dynamic);
            stream->error = 1;
            return -1;
        }
        free(dynamic);
        return written;
    }
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vprintf(const char *format, va_list arguments)
{
    return vfprintf(stdout, format, arguments);
}

int printf(const char *format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

void perror(const char *prefix)
{
    int error_number = errno;

    if (prefix != NULL && *prefix != '\0') {
        (void)fprintf(stderr, "%s: %s\n", prefix, strerror(error_number));
    } else {
        (void)fprintf(stderr, "%s\n", strerror(error_number));
    }
}
