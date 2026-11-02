#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "goth_syscall.h"

static int ascii_space(unsigned char character)
{
    return character == ' ' || (character >= '\t' && character <= '\r');
}

static int ascii_equal_fold(unsigned char left, unsigned char right)
{
    if (left >= 'A' && left <= 'Z') {
        left = (unsigned char)(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z') {
        right = (unsigned char)(right + ('a' - 'A'));
    }
    return left == right;
}

static int match_word(const char **cursor, const char *word)
{
    const char *text = *cursor;

    while (*word != '\0') {
        if (*text == '\0' ||
            !ascii_equal_fold((unsigned char)*text, (unsigned char)*word)) {
            return 0;
        }
        text++;
        word++;
    }
    *cursor = text;
    return 1;
}

static double make_infinity(int negative)
{
    return negative ? -HUGE_VAL : HUGE_VAL;
}

static double parse_hex_number(const char *cursor, char **endptr)
{
    long double value = 0.0L;
    int exponent = 0;
    int exponent_sign = 1;
    int any = 0;

    while (*cursor != '\0') {
        int digit;

        if (*cursor >= '0' && *cursor <= '9') {
            digit = *cursor - '0';
        } else if (*cursor >= 'a' && *cursor <= 'f') {
            digit = *cursor - 'a' + 10;
        } else if (*cursor >= 'A' && *cursor <= 'F') {
            digit = *cursor - 'A' + 10;
        } else {
            break;
        }
        value = value * 16.0L + (long double)digit;
        any = 1;
        cursor++;
    }

    if (*cursor == '.') {
        cursor++;
        while (*cursor != '\0') {
            int digit;

            if (*cursor >= '0' && *cursor <= '9') {
                digit = *cursor - '0';
            } else if (*cursor >= 'a' && *cursor <= 'f') {
                digit = *cursor - 'a' + 10;
            } else if (*cursor >= 'A' && *cursor <= 'F') {
                digit = *cursor - 'A' + 10;
            } else {
                break;
            }
            value = value * 16.0L + (long double)digit;
            exponent -= 4;
            any = 1;
            cursor++;
        }
    }

    if (!any) {
        if (endptr != NULL) {
            *endptr = (char *)cursor;
        }
        errno = 0;
        return 0.0;
    }

    if (*cursor == 'p' || *cursor == 'P') {
        cursor++;
        if (*cursor == '+' || *cursor == '-') {
            exponent_sign = *cursor == '-' ? -1 : 1;
            cursor++;
        }
        if (*cursor >= '0' && *cursor <= '9') {
            while (*cursor >= '0' && *cursor <= '9') {
                exponent = exponent * 10 + (*cursor - '0');
                cursor++;
            }
        } else {
            exponent = 0;
        }
    }

    exponent *= exponent_sign;
    if (exponent > 4096) {
        errno = ERANGE;
        if (endptr != NULL) {
            *endptr = (char *)cursor;
        }
        return make_infinity(0);
    }
    if (exponent < -4096) {
        errno = ERANGE;
        if (endptr != NULL) {
            *endptr = (char *)cursor;
        }
        return 0.0;
    }

    value *= (long double)pow(2.0, (double)exponent);
    if (endptr != NULL) {
        *endptr = (char *)cursor;
    }
    errno = 0;
    return (double)value;
}

double strtod(const char *text, char **endptr)
{
    const char *cursor = text;
    int negative = 0;
    long double mantissa = 0.0L;
    int exponent = 0;
    int any = 0;

    if (text == NULL) {
        errno = EINVAL;
        if (endptr != NULL) {
            *endptr = (char *)text;
        }
        return 0.0;
    }

    while (ascii_space((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        cursor++;
    }

    if (match_word(&cursor, "infinity") || match_word(&cursor, "inf")) {
        if (endptr != NULL) {
            *endptr = (char *)cursor;
        }
        errno = 0;
        return make_infinity(negative);
    }
    if (match_word(&cursor, "nan")) {
        if (*cursor == '(') {
            cursor++;
            while (*cursor != '\0' && *cursor != ')') {
                cursor++;
            }
            if (*cursor == ')') {
                cursor++;
            }
        }
        if (endptr != NULL) {
            *endptr = (char *)cursor;
        }
        errno = 0;
        return negative ? -NAN : NAN;
    }

    if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        double value = parse_hex_number(cursor + 2, endptr);

        if (endptr != NULL && *endptr == text) {
            *endptr = (char *)text;
        }
        return negative ? -value : value;
    }

    while (*cursor >= '0' && *cursor <= '9') {
        mantissa = mantissa * 10.0L + (long double)(*cursor - '0');
        any = 1;
        cursor++;
    }
    if (*cursor == '.') {
        cursor++;
        while (*cursor >= '0' && *cursor <= '9') {
            mantissa = mantissa * 10.0L + (long double)(*cursor - '0');
            exponent--;
            any = 1;
            cursor++;
        }
    }
    if (!any) {
        if (endptr != NULL) {
            *endptr = (char *)text;
        }
        errno = 0;
        return 0.0;
    }

    if (*cursor == 'e' || *cursor == 'E') {
        int exponent_sign = 1;
        int parsed_exponent = 0;
        const char *exponent_start = cursor;

        cursor++;
        if (*cursor == '+' || *cursor == '-') {
            exponent_sign = *cursor == '-' ? -1 : 1;
            cursor++;
        }
        if (*cursor >= '0' && *cursor <= '9') {
            while (*cursor >= '0' && *cursor <= '9') {
                if (parsed_exponent < 100000) {
                    parsed_exponent =
                        parsed_exponent * 10 + (*cursor - '0');
                }
                cursor++;
            }
            exponent += exponent_sign * parsed_exponent;
        } else {
            cursor = exponent_start;
        }
    }

    if (endptr != NULL) {
        *endptr = (char *)cursor;
    }
    if (exponent > 400) {
        errno = ERANGE;
        return make_infinity(negative);
    }
    if (exponent < -400) {
        errno = ERANGE;
        return negative ? -0.0 : 0.0;
    }

    mantissa *= (long double)pow(10.0, (double)exponent);
    if (mantissa > (long double)HUGE_VAL) {
        errno = ERANGE;
        return make_infinity(negative);
    }
    errno = 0;
    return negative ? -(double)mantissa : (double)mantissa;
}

float strtof(const char *text, char **endptr)
{
    return (float)strtod(text, endptr);
}

long double strtold(const char *text, char **endptr)
{
    return (long double)strtod(text, endptr);
}

double atof(const char *text)
{
    return strtod(text, NULL);
}
