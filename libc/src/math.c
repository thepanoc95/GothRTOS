#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

static double absolute(double value)
{
    union {
        double value;
        uint64_t bits;
    } number;

    number.value = value;
    number.bits &= UINT64_C(0x7fffffffffffffff);
    return number.value;
}

double fabs(double value)
{
    return absolute(value);
}

double copysign(double magnitude, double sign)
{
    union {
        double value;
        uint64_t bits;
    } positive;
    union {
        double value;
        uint64_t bits;
    } source;

    positive.value = absolute(magnitude);
    source.value = sign;
    positive.bits = (positive.bits & UINT64_C(0x7fffffffffffffff)) |
                    (source.bits & UINT64_C(0x8000000000000000));
    return positive.value;
}

double nan(const char *tag)
{
    (void)tag;
    return NAN;
}

double fmin(double left, double right)
{
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left < right ? left : right;
}

double fmax(double left, double right)
{
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left > right ? left : right;
}

double trunc(double value)
{
    if (!isfinite(value) || absolute(value) >= 9223372036854775808.0) {
        return value;
    }
    return (double)(long long)value;
}

double floor(double value)
{
    double integer;

    if (!isfinite(value) || absolute(value) >= 4503599627370496.0) {
        return value;
    }
    integer = (double)(long long)value;
    if (integer > value) {
        integer -= 1.0;
    }
    return integer;
}

double ceil(double value)
{
    double integer;

    if (!isfinite(value) || absolute(value) >= 4503599627370496.0) {
        return value;
    }
    integer = (double)(long long)value;
    if (integer < value) {
        integer += 1.0;
    }
    return integer;
}

double round(double value)
{
    if (!isfinite(value) || absolute(value) >= 4503599627370496.0) {
        return value;
    }
    if (value < 0.0) {
        return ceil(value - 0.5);
    }
    return floor(value + 0.5);
}

double frexp(double value, int *exponent)
{
    int power = 0;

    if (exponent == NULL) {
        exponent = &power;
    }
    *exponent = 0;
    if (value == 0.0 || !isfinite(value)) {
        return value;
    }

    while (absolute(value) >= 1.0) {
        value *= 0.5;
        *exponent += 1;
    }
    while (absolute(value) < 0.5) {
        value *= 2.0;
        *exponent -= 1;
    }
    return value;
}

double ldexp(double value, int exponent)
{
    if (value == 0.0 || !isfinite(value)) {
        return value;
    }
    if (exponent > 2000) {
        return copysign(HUGE_VAL, value);
    }
    if (exponent < -2000) {
        return copysign(0.0, value);
    }
    while (exponent > 0) {
        value *= 2.0;
        exponent--;
        if (isinf(value)) {
            return value;
        }
    }
    while (exponent < 0) {
        value *= 0.5;
        exponent++;
        if (value == 0.0) {
            return value;
        }
    }
    return value;
}

double modf(double value, double *integer_part)
{
    double whole = trunc(value);
    double fraction = value - whole;

    if (integer_part != NULL) {
        *integer_part = whole;
    }
    return copysign(fraction, value);
}

#if defined(__x86_64__) || defined(__i386__)

static double x87_round_integer(double value)
{
    double result;

    __asm__ volatile("frndint" : "=t"(result) : "0"(value));
    return result;
}

static double x87_sin(double value)
{
    double result;

    __asm__ volatile("fsin" : "=t"(result) : "0"(value));
    return result;
}

static double x87_cos(double value)
{
    double result;

    __asm__ volatile("fcos" : "=t"(result) : "0"(value));
    return result;
}

static double x87_tan(double value)
{
    double result;

    __asm__ volatile("fptan\n\tfstp %%st(0)"
                     : "=t"(result)
                     : "0"(value)
                     : "st(1)");
    return result;
}

static double x87_atan2(double y, double x)
{
    double result;

    __asm__ volatile("fpatan"
                     : "=t"(result)
                     : "0"(x), "u"(y)
                     : "st(1)");
    return result;
}

static double x87_log2(double value)
{
    double result;

    __asm__ volatile("fyl2x"
                     : "=t"(result)
                     : "0"(value), "u"(1.0)
                     : "st(1)");
    return result;
}

static double x87_log(double value)
{
    double result;

    __asm__ volatile("fyl2x"
                     : "=t"(result)
                     : "0"(value), "u"(M_LN2)
                     : "st(1)");
    return result;
}

static double x87_log10(double value)
{
    double result;

    __asm__ volatile("fyl2x"
                     : "=t"(result)
                     : "0"(value), "u"(0.30102999566398119521)
                     : "st(1)");
    return result;
}

static double x87_exp2(double value)
{
    double integral;
    double fraction;
    double result;

    if (isnan(value)) {
        return value;
    }
    if (value > 1024.0) {
        return HUGE_VAL;
    }
    if (value < -1075.0) {
        return 0.0;
    }

    integral = x87_round_integer(value);
    fraction = value - integral;
    __asm__ volatile("f2xm1" : "=t"(result) : "0"(fraction));
    result += 1.0;
    return ldexp(result, (int)integral);
}

double sqrt(double value)
{
    double result;

    __asm__ volatile("sqrtsd %1, %0" : "=x"(result) : "x"(value));
    return result;
}

double sin(double value)
{
    return x87_sin(value);
}

double cos(double value)
{
    return x87_cos(value);
}

double tan(double value)
{
    return x87_tan(value);
}

double atan(double value)
{
    return x87_atan2(value, 1.0);
}

double atan2(double y, double x)
{
    if (x == 0.0 && y == 0.0) {
        return copysign(0.0, y);
    }
    return x87_atan2(y, x);
}

double exp(double value)
{
    return x87_exp2(value * M_LOG2E);
}

double log(double value)
{
    if (value < 0.0) {
        errno = EDOM;
        return NAN;
    }
    if (value == 0.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    return x87_log(value);
}

double log2(double value)
{
    if (value < 0.0) {
        errno = EDOM;
        return NAN;
    }
    if (value == 0.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    return x87_log2(value);
}

double log10(double value)
{
    if (value < 0.0) {
        errno = EDOM;
        return NAN;
    }
    if (value == 0.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    return x87_log10(value);
}

#else /* !x86 */

static double generic_exp(double value)
{
    int exponent;
    double remainder;
    double term = 1.0;
    double sum = 1.0;
    int index;

    if (value > 709.782712893384) {
        errno = ERANGE;
        return HUGE_VAL;
    }
    if (value < -745.133219101941) {
        return 0.0;
    }
    exponent = (int)(value / M_LN2);
    remainder = value - (double)exponent * M_LN2;
    for (index = 1; index < 32; index++) {
        term *= remainder / (double)index;
        sum += term;
    }
    return ldexp(sum, exponent);
}

static double generic_log(double value)
{
    int exponent;
    double mantissa = frexp(value, &exponent);
    double z;
    double z_squared;
    double term;
    double sum;
    int index;

    if (value <= 0.0) {
        errno = value == 0.0 ? ERANGE : EDOM;
        return value == 0.0 ? -HUGE_VAL : NAN;
    }
    if (mantissa < 0.7071067811865475244) {
        mantissa *= 2.0;
        exponent--;
    }
    z = (mantissa - 1.0) / (mantissa + 1.0);
    z_squared = z * z;
    term = z;
    sum = z;
    for (index = 3; index < 80; index += 2) {
        term *= z_squared;
        sum += term / (double)index;
    }
    return 2.0 * sum + (double)exponent * M_LN2;
}

static double generic_sin(double value)
{
    double term;
    double sum;
    double squared;
    int index;

    value = fmod(value, 2.0 * M_PI);
    term = value;
    sum = value;
    squared = value * value;
    for (index = 3; index < 40; index += 2) {
        term *= -squared / (double)((index - 1) * index);
        sum += term;
    }
    return sum;
}

static double generic_cos(double value)
{
    double term = 1.0;
    double sum = 1.0;
    double squared;
    int index;

    value = fmod(value, 2.0 * M_PI);
    squared = value * value;
    for (index = 2; index < 40; index += 2) {
        term *= -squared / (double)((index - 1) * index);
        sum += term;
    }
    return sum;
}

static double generic_atan(double value)
{
    double sign = 1.0;
    double term;
    double sum;
    double squared;
    int index;

    if (value < 0.0) {
        sign = -1.0;
        value = -value;
    }
    if (value > 1.0) {
        return sign * (M_PI_2 - generic_atan(1.0 / value));
    }
    if (value > 0.5) {
        return sign * (M_PI / 4.0 +
                       generic_atan((value - 1.0) / (value + 1.0)));
    }
    squared = value * value;
    term = value;
    sum = value;
    for (index = 3; index < 80; index += 2) {
        term *= -squared;
        sum += term / (double)index;
    }
    return sign * sum;
}

double sqrt(double value)
{
    double guess;

    if (!(value > 0.0) || !isfinite(value)) {
        return value == 0.0 ? value : NAN;
    }
    guess = value > 1.0 ? value : 1.0;
    for (int index = 0; index < 64; index++) {
        double next = 0.5 * (guess + value / guess);

        if (next == guess) {
            break;
        }
        guess = next;
    }
    return guess;
}

double sin(double value)
{
    return generic_sin(value);
}

double cos(double value)
{
    return generic_cos(value);
}

double tan(double value)
{
    return generic_sin(value) / generic_cos(value);
}

double atan(double value)
{
    return generic_atan(value);
}

double atan2(double y, double x)
{
    if (x > 0.0) {
        return generic_atan(y / x);
    }
    if (x < 0.0) {
        return y >= 0.0 ? generic_atan(y / x) + M_PI
                        : generic_atan(y / x) - M_PI;
    }
    if (y > 0.0) {
        return M_PI_2;
    }
    if (y < 0.0) {
        return -M_PI_2;
    }
    return 0.0;
}

double exp(double value)
{
    return generic_exp(value);
}

double log(double value)
{
    return generic_log(value);
}

double log2(double value)
{
    return generic_log(value) * M_LOG2E;
}

double log10(double value)
{
    return generic_log(value) * M_LOG10E;
}

#endif /* x86 */

double fmod(double left, double right)
{
    double absolute_left;
    double absolute_right;

    if (right == 0.0 || isnan(left) || isnan(right) || isinf(left)) {
        errno = EDOM;
        return NAN;
    }
    if (isinf(right)) {
        return left;
    }

    absolute_left = absolute(left);
    absolute_right = absolute(right);
    while (absolute_left >= absolute_right) {
        double step = absolute_right;

        while (step <= absolute_left * 0.5) {
            step *= 2.0;
        }
        absolute_left -= step;
    }
    return copysign(absolute_left, left);
}

double asin(double value)
{
    if (value < -1.0 || value > 1.0) {
        errno = EDOM;
        return NAN;
    }
    if (value == 1.0) {
        return M_PI_2;
    }
    if (value == -1.0) {
        return -M_PI_2;
    }
    return atan2(value, sqrt(1.0 - value * value));
}

double acos(double value)
{
    if (value < -1.0 || value > 1.0) {
        errno = EDOM;
        return NAN;
    }
    return atan2(sqrt(1.0 - value * value), value);
}

double pow(double base, double exponent)
{
    int exponent_is_integer = 0;
    long long integer_exponent = 0;
    int negative = 0;

    if (exponent == 0.0) {
        return 1.0;
    }
    if (base == 1.0) {
        return 1.0;
    }
    if (isnan(base) || isnan(exponent)) {
        return NAN;
    }
    if (exponent == (double)(long long)exponent) {
        exponent_is_integer = 1;
        integer_exponent = (long long)exponent;
    }

    if (base < 0.0) {
        if (!exponent_is_integer) {
            errno = EDOM;
            return NAN;
        }
        negative = (integer_exponent & 1LL) != 0;
        base = -base;
    }
    if (base == 0.0) {
        if (exponent < 0.0) {
            errno = ERANGE;
            return negative ? -HUGE_VAL : HUGE_VAL;
        }
        return negative ? -0.0 : 0.0;
    }

    {
        double result = exp(exponent * log(base));

        return negative ? -result : result;
    }
}

double hypot(double x, double y)
{
    return sqrt(x * x + y * y);
}
