#ifndef GOTH_LIBC_STDLIB_H
#define GOTH_LIBC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

long strtol(const char *text, char **endptr, int base);
unsigned long strtoul(const char *text, char **endptr, int base);
long long strtoll(const char *text, char **endptr, int base);
unsigned long long strtoull(const char *text, char **endptr, int base);
double strtod(const char *text, char **endptr);
float strtof(const char *text, char **endptr);
long double strtold(const char *text, char **endptr);
int atoi(const char *text);
long atol(const char *text);
long long atoll(const char *text);
double atof(const char *text);
int abs(int value);
long labs(long value);
long long llabs(long long value);

void exit(int status);
void _Exit(int status);
void abort(void);
char *getenv(const char *name);
int system(const char *command);

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif
