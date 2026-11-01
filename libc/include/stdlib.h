#ifndef GOTH_LIBC_STDLIB_H
#define GOTH_LIBC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

long strtol(const char *text, char **endptr, int base);
unsigned long strtoul(const char *text, char **endptr, int base);
long long strtoll(const char *text, char **endptr, int base);
unsigned long long strtoull(const char *text, char **endptr, int base);
int atoi(const char *text);
long atol(const char *text);
long long atoll(const char *text);
int abs(int value);
long labs(long value);

#ifdef __cplusplus
}
#endif

#endif
