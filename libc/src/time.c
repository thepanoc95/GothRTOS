#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "goth_syscall.h"

static struct tm broken_down;

static long long floor_division(long long value, long long divisor)
{
    long long quotient = value / divisor;

    if (value % divisor != 0 && ((value < 0) != (divisor < 0))) {
        quotient--;
    }
    return quotient;
}

static long long floor_modulo(long long value, long long divisor)
{
    return value - floor_division(value, divisor) * divisor;
}

static void civil_from_days(long long days, int *year, int *month, int *day)
{
    long long shifted = days + 719468;
    long long era = floor_division(shifted, 146097);
    unsigned int day_of_era = (unsigned int)(shifted - era * 146097);
    unsigned int year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) /
        365U;
    int computed_year = (int)year_of_era + (int)(era * 400);
    unsigned int day_of_year =
        day_of_era - (365U * year_of_era + year_of_era / 4U -
                      year_of_era / 100U);
    unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    unsigned int computed_day =
        day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    int computed_month =
        (int)month_prime + (month_prime < 10U ? 3 : -9);

    computed_year += computed_month <= 2 ? 1 : 0;
    *year = computed_year;
    *month = computed_month;
    *day = (int)computed_day;
}

static long long days_from_civil(int year, int month, int day)
{
    int adjusted_year = year - (month <= 2 ? 1 : 0);
    long long era = floor_division(adjusted_year, 400);
    unsigned int year_of_era =
        (unsigned int)(adjusted_year - (int)(era * 400));
    unsigned int month_prime =
        (unsigned int)(month + (month > 2 ? -3 : 9));
    unsigned int day_of_year =
        (153U * month_prime + 2U) / 5U + (unsigned int)day - 1U;
    unsigned int day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U +
        day_of_year;

    return era * 146097 + (long long)day_of_era - 719468;
}

static time_t current_time(int clock_id)
{
    struct goth_timespec value;

    if (goth_sys_clock_gettime(clock_id, &value) < 0) {
        return (time_t)-1;
    }
    return (time_t)value.tv_sec;
}

time_t time(time_t *result)
{
    time_t value = current_time(GOTH_CLOCK_REALTIME);

    if (result != NULL) {
        *result = value;
    }
    return value;
}

clock_t clock(void)
{
    struct goth_timespec value;

    if (goth_sys_clock_gettime(GOTH_CLOCK_MONOTONIC, &value) < 0) {
        return (clock_t)-1;
    }
    return (clock_t)((long long)value.tv_sec * CLOCKS_PER_SEC +
                     (long long)value.tv_nsec / (1000000000L /
                                                  CLOCKS_PER_SEC));
}

double difftime(time_t later, time_t earlier)
{
    return (double)(later - earlier);
}

struct tm *gmtime(const time_t *value)
{
    long long seconds;
    long long days;
    long long seconds_in_day;
    int year;
    int month;
    int day;

    if (value == NULL) {
        errno = EINVAL;
        return NULL;
    }

    seconds = (long long)*value;
    days = floor_division(seconds, 86400);
    seconds_in_day = floor_modulo(seconds, 86400);
    civil_from_days(days, &year, &month, &day);

    broken_down.tm_sec = (int)(seconds_in_day % 60);
    broken_down.tm_min = (int)((seconds_in_day / 60) % 60);
    broken_down.tm_hour = (int)(seconds_in_day / 3600);
    broken_down.tm_mday = day;
    broken_down.tm_mon = month - 1;
    broken_down.tm_year = (int)year - 1900;
    broken_down.tm_wday = (int)floor_modulo(days + 4, 7);
    broken_down.tm_yday =
        (int)(days - days_from_civil((int)year, 1, 1));
    broken_down.tm_isdst = 0;
    return &broken_down;
}

struct tm *localtime(const time_t *value)
{
    return gmtime(value);
}

time_t mktime(struct tm *value)
{
    long long days;
    long long seconds;

    if (value == NULL) {
        errno = EINVAL;
        return (time_t)-1;
    }

    days = days_from_civil(value->tm_year + 1900, value->tm_mon + 1,
                           value->tm_mday);
    seconds = days * 86400 + (long long)value->tm_hour * 3600 +
              (long long)value->tm_min * 60 + value->tm_sec;
    {
        time_t result = (time_t)seconds;
        struct tm *normalized = gmtime(&result);

        if (normalized != NULL) {
            *value = *normalized;
        }
        return result;
    }
}

static size_t append_text(char *buffer, size_t size, size_t position,
                          const char *text)
{
    while (*text != '\0' && position + 1U < size) {
        buffer[position++] = *text++;
    }
    if (position < size) {
        buffer[position] = '\0';
    }
    return position;
}

static size_t append_number(char *buffer, size_t size, size_t position,
                            long value, int width, char fill)
{
    char digits[32];
    int length = 0;
    int negative = value < 0;
    unsigned long magnitude =
        (unsigned long)(negative ? -(long long)value : (long long)value);

    do {
        digits[length++] = (char)('0' + (int)(magnitude % 10U));
        magnitude /= 10U;
    } while (magnitude != 0U);

    if (negative) {
        digits[length++] = '-';
        width--;
    }
    while (width > length && position + 1U < size) {
        buffer[position++] = fill;
        width--;
    }
    while (length > 0 && position + 1U < size) {
        buffer[position++] = digits[--length];
    }
    if (position < size) {
        buffer[position] = '\0';
    }
    return position;
}

static const char *const short_days[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const long_days[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
    "Saturday"
};
static const char *const short_months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
    "Nov", "Dec"
};
static const char *const long_months[] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

size_t strftime(char *buffer, size_t size, const char *format,
                const struct tm *value)
{
    size_t position = 0U;

    if (buffer == NULL || format == NULL || value == NULL || size == 0U) {
        return 0U;
    }
    buffer[0] = '\0';

    while (*format != '\0') {
        if (*format != '%') {
            if (position + 1U < size) {
                buffer[position++] = *format;
                buffer[position] = '\0';
            } else {
                position++;
            }
            format++;
            continue;
        }

        format++;
        switch (*format) {
        case '\0':
            format--;
            break;
        case '%':
            position = append_text(buffer, size, position, "%");
            break;
        case 'n':
            position = append_text(buffer, size, position, "\n");
            break;
        case 't':
            position = append_text(buffer, size, position, "\t");
            break;
        case 'Y':
            position = append_number(buffer, size, position,
                                     value->tm_year + 1900, 4, '0');
            break;
        case 'C':
            position = append_number(buffer, size, position,
                                     (value->tm_year + 1900) / 100, 2,
                                     '0');
            break;
        case 'y':
            position = append_number(buffer, size, position,
                                     (value->tm_year + 1900) % 100, 2,
                                     '0');
            break;
        case 'm':
            position = append_number(buffer, size, position,
                                     value->tm_mon + 1, 2, '0');
            break;
        case 'd':
            position = append_number(buffer, size, position,
                                     value->tm_mday, 2, '0');
            break;
        case 'e':
            position = append_number(buffer, size, position,
                                     value->tm_mday, 2, ' ');
            break;
        case 'H':
            position = append_number(buffer, size, position, value->tm_hour,
                                     2, '0');
            break;
        case 'I': {
            int hour = value->tm_hour % 12;

            if (hour == 0) {
                hour = 12;
            }
            position = append_number(buffer, size, position, hour, 2, '0');
            break;
        }
        case 'M':
            position = append_number(buffer, size, position, value->tm_min,
                                     2, '0');
            break;
        case 'S':
            position = append_number(buffer, size, position, value->tm_sec,
                                     2, '0');
            break;
        case 'p':
            position = append_text(buffer, size, position,
                                   value->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'a':
            position = append_text(buffer, size, position,
                                   short_days[value->tm_wday % 7]);
            break;
        case 'A':
            position = append_text(buffer, size, position,
                                   long_days[value->tm_wday % 7]);
            break;
        case 'b':
        case 'h':
            position = append_text(buffer, size, position,
                                   short_months[value->tm_mon % 12]);
            break;
        case 'B':
            position = append_text(buffer, size, position,
                                   long_months[value->tm_mon % 12]);
            break;
        case 'j':
            position = append_number(buffer, size, position,
                                     value->tm_yday + 1, 3, '0');
            break;
        case 'w':
            position = append_number(buffer, size, position, value->tm_wday,
                                     1, '0');
            break;
        case 'u':
            position = append_number(buffer, size, position,
                                     value->tm_wday == 0 ? 7
                                                         : value->tm_wday,
                                     1, '0');
            break;
        case 'U':
            position = append_number(buffer, size, position,
                                     (value->tm_yday + 7 - value->tm_wday) /
                                         7,
                                     2, '0');
            break;
        case 'W':
            position = append_number(buffer, size, position,
                                     (value->tm_yday + 7 -
                                      ((value->tm_wday + 6) % 7)) /
                                         7,
                                     2, '0');
            break;
        case 's': {
            struct tm copy = *value;
            time_t seconds = mktime(&copy);

            position = append_number(buffer, size, position, (long)seconds,
                                     1, '0');
            break;
        }
        case 'z':
            position = append_text(buffer, size, position, "+0000");
            break;
        case 'Z':
            position = append_text(buffer, size, position, "UTC");
            break;
        case 'D': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%m/%d/%y", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'F': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%Y-%m-%d", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'T':
        case 'X': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%H:%M:%S", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'R': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%H:%M", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'r': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%I:%M:%S %p", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'x': {
            char temporary[32];

            if (strftime(temporary, sizeof(temporary), "%m/%d/%y", value) >
                0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        case 'c': {
            char temporary[64];

            if (strftime(temporary, sizeof(temporary),
                         "%a %b %e %H:%M:%S %Y", value) > 0U) {
                position = append_text(buffer, size, position, temporary);
            }
            break;
        }
        default:
            if (position + 1U < size) {
                buffer[position++] = '%';
            } else {
                position++;
            }
            if (position + 1U < size) {
                buffer[position++] = *format;
                buffer[position] = '\0';
            } else {
                position++;
            }
            break;
        }
        format++;
    }

    return position < size ? position : 0U;
}
