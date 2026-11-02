#ifndef GOTH_LIBC_STRING_H
#define GOTH_LIBC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *memory, int character, size_t count);
size_t strlen(const char *text);
size_t strnlen(const char *text, size_t max_length);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
int strcoll(const char *left, const char *right);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t count);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *text, const char *needle);
char *strpbrk(const char *text, const char *accept);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strerror(int error_number);

#ifdef __cplusplus
}
#endif

#endif
