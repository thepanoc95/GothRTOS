#ifndef GOTH_LIBC_TIME_H
#define GOTH_LIBC_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000000L

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void);
time_t time(time_t *result);
double difftime(time_t later, time_t earlier);
struct tm *gmtime(const time_t *value);
struct tm *localtime(const time_t *value);
time_t mktime(struct tm *value);
size_t strftime(char *buffer, size_t size, const char *format,
                const struct tm *value);

#ifdef __cplusplus
}
#endif

#endif
