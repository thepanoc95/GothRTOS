#ifndef GOTH_LIBC_STDIO_H
#define GOTH_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct goth_libc_file FILE;
typedef long fpos_t;

#define EOF (-1)
#define BUFSIZ 4096
#define L_tmpnam 32
#define FILENAME_MAX 4096

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
FILE *tmpfile(void);
int fclose(FILE *stream);
int fflush(FILE *stream);

size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int fputc(int character, FILE *stream);
int fputs(const char *text, FILE *stream);
int getc(FILE *stream);
int fgetc(FILE *stream);
int ungetc(int character, FILE *stream);
int putc(int character, FILE *stream);
int putchar(int character);
int puts(const char *text);

int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
int fileno(FILE *stream);

int remove(const char *path);
int rename(const char *old_path, const char *new_path);
char *tmpnam(char *buffer);

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list arguments);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list arguments);
int vsnprintf(char *buffer, size_t size, const char *format,
              va_list arguments);

void perror(const char *prefix);

#ifdef __cplusplus
}
#endif

#endif
